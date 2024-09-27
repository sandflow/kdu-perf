#include <stdio.h>
#include <time.h>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>

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

  void put_text(const char* msg) {
    std::cout << msg;
  }

  virtual void flush(bool end_of_message = false) {
    if (end_of_message) {
        std::cout << std::endl;
    }
  }
};

static error_message_handler error_handler;


int main(int argc, char *argv[]) {
  cxxopts::Options options("kdu_perf", "KDU SDK performance tester");

  options.add_options()("r,repetitions", "Codesteeam directory path",
                        cxxopts::value<int>()->default_value("1000"))(
      "codestream", "Path to input codestream", cxxopts::value<std::string>());

  options.parse_positional({"codestream"});

  auto result = options.parse(argc, argv);

  kdu_core::kdu_customize_errors(&error_handler);

  std::vector<char> cs_buf = read_file(result["codestream"].as<std::string>());

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

  int stripe_heights[COMP_COUNT] = {(int)dims.size.y, (int)dims.size.y,
                                    (int)dims.size.y};

  bool is_planar = false;
  int bit_depth = c.get_bit_depth(0);

  for (int i = 0; i < num_comps; i++) {
    kdu_core::kdu_coords coords;

    c.get_subsampling(i, coords);

    if (coords.x > 1 || coords.y > 1) is_planar = true;

    if (c.get_bit_depth(i) != bit_depth)
      throw std::runtime_error("Not all components have the same bit depth");
  }

  int component_sz = bit_depth > 8 ? 2 : 1;

  std::vector<uint8_t> planes_buf[3];

  int precisions[COMP_COUNT];
  bool is_signed[COMP_COUNT];

  if (is_planar) {
    for (int i = 0; i < num_comps; i++) {
      kdu_core::kdu_coords coords;
      c.get_subsampling(i, coords);

      planes_buf[i].resize(width * height * component_sz / coords.x / coords.y);
      precisions[i] = (int)bit_depth;
      is_signed[i] = false;
    }
  } else {
    planes_buf[0].resize(width * height * component_sz * num_comps);
    precisions[0] = (int)bit_depth;
    is_signed[0] = false;
  }

  /* gather statistics */

  int repetitions = result["repetitions"].as<int>();

  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < repetitions; i++) {
    kdu_stripe_decompressor d;

    d.start(c);

    if (is_planar) {
      kdu_int16 *planes[3] = {(kdu_int16 *)planes_buf[0].data(),
                              (kdu_int16 *)planes_buf[1].data(),
                              (kdu_int16 *)planes_buf[2].data()};

      d.pull_stripe(planes, stripe_heights, NULL, NULL, precisions, is_signed);
    } else {
      d.pull_stripe(planes_buf[0].data(), stripe_heights);
    }

    d.finish();

    buffer.seek(0);
    c.restart(&buffer);
  }

  auto total_time = std::chrono::high_resolution_clock::now() - start;

  std::cout << "Decodes per second: " << repetitions / std::chrono::duration<double>(total_time).count() << std::endl;
}
