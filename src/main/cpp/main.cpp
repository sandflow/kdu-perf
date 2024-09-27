#include "cxxopts.hpp"
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdio.h>
#include <time.h>

#include "kdu_compressed.h"
#include "kdu_elementary.h"
#include "kdu_file_io.h"
#include "kdu_messaging.h"
#include "kdu_params.h"
#include "kdu_sample_processing.h"
#include "kdu_stripe_compressor.h"
#include "kdu_stripe_decompressor.h"

using namespace kdu_supp;

struct ImageComponents {
  std::string name;
  uint8_t num_comps;

  ImageComponents(uint8_t num_comps, std::string name): num_comps(num_comps), name(name) { }

  ImageComponents() {}

  bool operator==(const ImageComponents& other) const {
    return &other == this || other.name == this->name;
  }

  static ImageComponents RGBA;
  static ImageComponents RGB;
  static ImageComponents YUV;
};


struct ImageFormat {
  uint8_t bit_depth;
  ImageComponents comps;
  bool is_planar;
  std::array<uint8_t, 4> x_sub_factor;
  std::array<uint8_t, 4> y_sub_factor;

  ImageFormat(uint8_t bit_depth, const ImageComponents &comps, bool is_planar, const std::array<uint8_t, 4> &x_sub_factor, const std::array<uint8_t, 4> &y_sub_factor):
    bit_depth(bit_depth), comps(comps), is_planar(is_planar), x_sub_factor(x_sub_factor), y_sub_factor(y_sub_factor) {}

  ImageFormat() {}

  uint8_t num_planes() const {
    return this->is_planar ? this->comps.num_comps : 1;
  }

  bool operator==(const ImageFormat& other) const {
    if (&other == this)
      return true;

    return other.bit_depth == this->bit_depth
            && other.comps == this->comps
            && other.is_planar == this->is_planar
            && other.x_sub_factor == this->x_sub_factor
            && other.y_sub_factor == this->y_sub_factor;
  }

  static ImageFormat RGBA8;
  static ImageFormat RGB8;
  static ImageFormat YUV422P10;
};


struct ImageContext {
  uint32_t width;
  uint32_t height;
  ImageFormat format;
  union {
    uint8_t* planes8[4];
    uint16_t* planes16[4];
  };

  ImageContext() : planes8 {NULL} {}

  int component_size() const {
    return this->is_plane16() ? 2 : 1;
  }

  bool is_plane16() const {
    return this->format.bit_depth > 8;
  }

  size_t plane_size(int i) const {
    if (this->format.is_planar) {
      return this->width * this->height * this->component_size() / this->format.x_sub_factor[i] / this->format.y_sub_factor[i];
    } else {
      return this->width * this->height * this->component_size() * this->format.comps.num_comps / this->format.x_sub_factor[i] / this->format.y_sub_factor[i];
    }
  }

  uint32_t plane_height(int i) const {
    return this->height / this->format.y_sub_factor[i];
  }

  uint32_t line_size(int i) const {
    if (this->format.is_planar) {
      return this->width * this->component_size() / this->format.x_sub_factor[i];
    } else {
      return this->width * this->component_size() * this->format.comps.num_comps / this->format.x_sub_factor[i];
    }
  }

  size_t total_bits() const {
    size_t total = 0;

    for(uint8_t i = 0; i < this->format.num_planes(); i++) {
      total += (this->width / this->format.x_sub_factor[i]) * (this->height / this->format.y_sub_factor[i])
               * this->format.comps.num_comps * this->format.bit_depth;
    }

    return total;
  }

};

std::vector<char> read_file(const std::string &path)
{
    constexpr int READ_SZ = 4096;

    auto is = std::ifstream(path);
    is.exceptions(std::ios_base::badbit);

    auto out = std::vector<char>();
    auto buf = std::vector<char>(READ_SZ);

    while (true) {
        is.read(&buf[0], READ_SZ);
        out.insert(out.end(), buf.begin(), buf.begin() + is.gcount());

        if (!is)
            break;
    }

    return out;
}

int main(int argc, char *argv[])
{
    cxxopts::Options options("kdu_perf", "KDU SDK performance tester");

    options.add_options()("r,repetitions", "Codesteeam directory path", cxxopts::value<int>()->default_value("1000"))("codestream", "Path to input codestream", cxxopts::value<std::string>());

    options.parse_positional({"codestream"});

    auto result = options.parse(argc, argv);

    std::vector<char> cs_buf = read_file(result["codestream"].as<std::string>());

    kdu_compressed_source_buffered buffer((kdu_byte *)cs_buf.data(), cs_buf.size());

    kdu_codestream c;
    c.create(&buffer);

    ImageContext image;

    kdu_dims dims;
    c.get_dims(0, dims);
    image.height = dims.size.y;
    image.width = dims.size.x;

    int num_comps = c.get_num_components();

    if (num_comps < 3 || num_comps > 4) {
        throw std::runtime_error("Bad number of components");
    }

    bool is_planar = false;

    for (int i = 0; i < num_comps; i++) {
        kdu_core::kdu_coords coords;

        c.get_subsampling(i, coords);

        image.format.x_sub_factor[i] = coords.x;
        image.format.y_sub_factor[i] = coords.y;

        if (coords.x > 1 || coords.y > 1)
            image.format.is_planar = true;
    }

    image.format.bit_depth = c.get_bit_depth(0);

    int stripe_heights[4] = {(int)dims.size.y, (int)dims.size.y, (int)dims.size.y, (int)dims.size.y};

    std::vector<uint8_t> planes_buf[3];

    int precisions[4];
    bool is_signed[4];

    kdu_int16 *planes[3] = {(kdu_int16 *)planes_buf[0].data(),
                        (kdu_int16 *)planes_buf[1].data(),
                        (kdu_int16 *)planes_buf[2].data()};

    if (image.format.is_planar) {

        for (int i = 0; i < num_comps; i++) {
            planes_buf[i].resize(image.plane_size(i));
            image.planes16[i] = (uint16_t *)planes_buf[i].data();
            precisions[i] = (int)image.format.bit_depth;
            is_signed[i] = false;
        }

        if (!image.is_plane16()) {
            throw std::runtime_error("Only YUV 10 bits supported.");
        }

    } else {

        planes_buf[0].resize(image.plane_size(0));
        image.planes8[0] = planes_buf[0].data();
    }

    /* gather statistics */

    auto start = std::chrono::high_resolution_clock::now();

    int repetitions = result["repetitions"].as<int>();

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < repetitions; i++) {

        kdu_stripe_decompressor d;

        d.start(c);

        if (image.format.is_planar) {

            d.pull_stripe(planes, stripe_heights, NULL, NULL, precisions, is_signed);

        } else {

            d.pull_stripe(planes_buf[0].data(), stripe_heights);
        }

        d.finish();
    }

    auto total_time = std::chrono::high_resolution_clock::now() - start;

    std::cout << total_time.count();
}
