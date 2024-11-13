#include <stdio.h>
#include <time.h>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>

#include "cxxopts.hpp"
#include "kdu_compressed.h"
#include "kdu_elementary.h"
#include "kdu_file_io.h"
#include "kdu_messaging.h"
#include "kdu_params.h"
#include "kdu_sample_processing.h"
#include "kdu_stripe_compressor.h"
#include "kdu_stripe_decompressor.h"

using namespace kdu_supp;

std::vector<char> read_file(const std::string &path) {
  constexpr int READ_SZ = 4096;

  auto is = std::ifstream(path);
  is.exceptions(std::ios_base::badbit);

  auto out = std::vector<char>();
  auto buf = std::vector<char>(READ_SZ);

  while (true) {
    is.read(&buf[0], READ_SZ);
    out.insert(out.end(), buf.begin(), buf.begin() + is.gcount());

    if (!is) break;
  }

  return out;
}

class error_message_handler : public kdu_core::kdu_message {
 public:
  void put_text(const char *msg) { std::cout << msg; }

  virtual void flush(bool end_of_message = false) {
    if (end_of_message) {
      std::cout << std::endl;
    }
  }
};

static error_message_handler error_handler;

void run(int repetitions, const std::vector<char> &cs_buf, double &avg_time) {
  kdu_compressed_source_buffered buffer((kdu_byte *)cs_buf.data(),
                                        cs_buf.size());

  kdu_codestream c;
  c.create(&buffer);
  c.enable_restart();

  kdu_dims dims;
  c.get_dims(0, dims);
  int height = dims.size.y;
  int width = dims.size.x;

  int num_comps = c.get_num_components();

  constexpr int COMP_COUNT = 3;

  if (num_comps != COMP_COUNT)
    throw std::runtime_error("Bad number of components");

  bool is_planar = false;
  int bit_depth = c.get_bit_depth(0);

  for (int i = 0; i < num_comps; i++) {
    kdu_core::kdu_coords coords;

    c.get_subsampling(i, coords);

    if ((i == 0 && coords.x != 1) || (coords.x != 1 && coords.x != 2))
      throw std::runtime_error("Unsupported horizontal component subsampling");

    if ((i == 0 && coords.y != 1) || (coords.y != 1 && coords.y != 2))
      throw std::runtime_error("Unsupported vertical component subsampling");

    if (coords.x > 1 || coords.y > 1) is_planar = true;

    if (c.get_bit_depth(i) != bit_depth)
      throw std::runtime_error("Not all components have the same bit depth");
  }

  int component_sz = bit_depth > 8 ? 2 : 1;

  std::vector<uint8_t> planes_buf[3];

  int precisions[COMP_COUNT];
  bool is_signed[COMP_COUNT];

  const int max_stripe_height = 64;

  if (is_planar) {
    for (int i = 0; i < num_comps; i++) {
      kdu_core::kdu_coords coords;
      c.get_subsampling(i, coords);

      planes_buf[i].resize(width / coords.x * max_stripe_height * component_sz);
      precisions[i] = (int)bit_depth;
      is_signed[i] = false;
    }
  } else {
    planes_buf[0].resize(width * max_stripe_height * component_sz * num_comps);
    precisions[0] = (int)bit_depth;
    is_signed[0] = false;
  }

  kdu_stripe_decompressor d;
  int stripe_heights[COMP_COUNT];

  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < repetitions; i++) {
    d.start(c, /* force_precise */ false,
            /* want_fastest */ true);

    d.get_recommended_stripe_heights(8, max_stripe_height, stripe_heights, NULL);

    bool more_samples = true;
    while (more_samples) {

      if (is_planar) {
        if (component_sz > 1) {
          kdu_int16 *planes[3] = {(kdu_int16 *)planes_buf[0].data(),
                                  (kdu_int16 *)planes_buf[1].data(),
                                  (kdu_int16 *)planes_buf[2].data()};

          more_samples = d.pull_stripe(planes, stripe_heights, NULL, NULL, precisions, is_signed);
        } else {
          kdu_byte *planes[3] = {(kdu_byte *)planes_buf[0].data(),
                                  (kdu_byte *)planes_buf[1].data(),
                                  (kdu_byte *)planes_buf[2].data()};

          more_samples = d.pull_stripe(planes, stripe_heights, NULL, NULL, precisions);
        }
      } else {
        more_samples = d.pull_stripe(planes_buf[0].data(), stripe_heights);
      }
    }

    d.finish();

    buffer.seek(0);
    c.restart(&buffer);
  }

  avg_time = std::chrono::duration<double>(
                 std::chrono::high_resolution_clock::now() - start)
                 .count() /
             repetitions;
}

int main(int argc, char *argv[]) {
  cxxopts::Options options("kdu_perf", "KDU SDK performance tester");

  options.add_options()("r,repetitions", "Number of repetitions per thread",
                        cxxopts::value<int>()->default_value("100"))(
      "t,threads", "Number of threads",
      cxxopts::value<int>()->default_value("1"))(
      "codestream", "Path to input codestream", cxxopts::value<std::string>());

  options.parse_positional({"codestream"});

  options.positional_help("<path to j2c codestream>");

  cxxopts::ParseResult result;

  try {
    result = options.parse(argc, argv);
    if (1 == argc) {
      std::cout << options.help() << std::endl;
      exit(0);
    }
  } catch (cxxopts::exceptions::invalid_option_syntax e) {
    std::cout << options.help() << std::endl;
    exit(0);
  }

  kdu_core::kdu_customize_errors(&error_handler);

  std::vector<char> cs_buf = read_file(result["codestream"].as<std::string>());

  int repetitions = result["repetitions"].as<int>();

  int num_threads = result["threads"].as<int>();

  std::vector<double> avg_times(num_threads);

  std::vector<std::thread> threads(0);

  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < num_threads; i++) {
    threads.push_back(
        std::thread(run, repetitions, cs_buf, std::ref(avg_times[i])));
  }

  for (int i = 0; i < avg_times.size(); i++) {
    threads[i].join();
  }

  auto total_dur = std::chrono::duration<double>(
                       std::chrono::high_resolution_clock::now() - start)
                       .count();

  std::cout << "Average decodes per thread per second: "
            << num_threads /
                   std::accumulate(avg_times.begin(), avg_times.end(), 0.0)
            << std::endl;

  std::cout << "Aggregate decodes per second: "
            << repetitions * num_threads / total_dur << std::endl;
}
