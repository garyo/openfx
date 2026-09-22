// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#include "ImageIO.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>

namespace testhost {

namespace fs = std::filesystem;

namespace {

std::string token(std::istream& in) {
  std::string t;
  in >> t;
  while (t.starts_with('#')) {  // comment to end of line
    std::string rest;
    std::getline(in, rest);
    in >> t;
  }
  return t;
}

}  // namespace

std::shared_ptr<ImageBuffer> readImage(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open " + path.string());
  std::string magic = token(in);
  int width = std::stoi(token(in)), height = std::stoi(token(in));
  if (width <= 0 || height <= 0) throw std::runtime_error(path.string() + ": bad image size");
  auto image = ImageBuffer::create({0, 0, width, height}, Components::RGBA, Depth::Float);

  if (magic == "P6") {
    int maxval = std::stoi(token(in));
    in.get();  // single whitespace after the header
    std::vector<unsigned char> row(static_cast<size_t>(width) * 3);
    for (int y = height - 1; y >= 0; --y) {  // PPM is top-down; OFX is bottom-up
      in.read(reinterpret_cast<char*>(row.data()), static_cast<std::streamsize>(row.size()));
      for (int x = 0; x < width; ++x)
        image->setPixel(x, y, {row[x * 3] / float(maxval), row[x * 3 + 1] / float(maxval), row[x * 3 + 2] / float(maxval), 1.f});
    }
  } else if (magic == "PF" || magic == "Pf") {
    int channels = magic == "PF" ? 3 : 1;
    float scale = std::stof(token(in));
    in.get();
    bool littleEndian = scale < 0;
    std::vector<float> row(static_cast<size_t>(width) * channels);
    for (int y = 0; y < height; ++y) {  // PFM is bottom-up, like OFX
      in.read(reinterpret_cast<char*>(row.data()), static_cast<std::streamsize>(row.size() * sizeof(float)));
      if (!littleEndian)
        for (float& f : row) {
          auto* b = reinterpret_cast<unsigned char*>(&f);
          std::swap(b[0], b[3]);
          std::swap(b[1], b[2]);
        }
      for (int x = 0; x < width; ++x) {
        const float* p = &row[static_cast<size_t>(x) * channels];
        image->setPixel(x, y, channels == 3 ? std::array<float, 4>{p[0], p[1], p[2], 1.f} : std::array<float, 4>{p[0], p[0], p[0], 1.f});
      }
    }
  } else {
    throw std::runtime_error(path.string() + ": unsupported format (need P6 PPM or PF/Pf PFM)");
  }
  if (!in) throw std::runtime_error(path.string() + ": truncated image data");
  return image;
}

void writeImage(const fs::path& path, const ImageBuffer& image) {
  std::ofstream out(path, std::ios::binary);
  if (!out) throw std::runtime_error("cannot write " + path.string());
  int width = image.width(), height = image.height();
  std::string ext = path.extension().string();
  if (ext == ".pfm") {
    out << "PF\n" << width << " " << height << "\n-1.0\n";  // little-endian
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x) {
        auto p = image.pixel(x, y);
        out.write(reinterpret_cast<const char*>(p.data()), 3 * sizeof(float));
      }
  } else {
    out << "P6\n" << width << " " << height << "\n255\n";
    for (int y = height - 1; y >= 0; --y)
      for (int x = 0; x < width; ++x) {
        auto p = image.pixel(x, y);
        for (int c = 0; c < 3; ++c) out.put(static_cast<char>(std::lround(std::clamp(p[c], 0.f, 1.f) * 255.f)));
      }
  }
}

std::shared_ptr<ImageBuffer> solidImage(int width, int height, std::array<float, 4> rgba) {
  auto image = ImageBuffer::create({0, 0, width, height}, Components::RGBA, Depth::Float);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) image->setPixel(x, y, rgba);
  return image;
}

std::shared_ptr<ImageBuffer> rampImage(int width, int height) {
  auto image = ImageBuffer::create({0, 0, width, height}, Components::RGBA, Depth::Float);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
      image->setPixel(x, y, {width > 1 ? x / float(width - 1) : 0.f, height > 1 ? y / float(height - 1) : 0.f, 0.5f, 1.f});
  return image;
}

}  // namespace testhost
