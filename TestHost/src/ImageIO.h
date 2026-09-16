// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <array>
#include <filesystem>
#include <memory>

#include "Effect.h"

namespace testhost {

// Reads a binary PPM (P6, 8-bit RGB) or PFM (float RGB / grey) into an RGBA float buffer.
std::shared_ptr<ImageBuffer> readImage(const std::filesystem::path& path);

// Writes PPM (8-bit RGB, by .ppm extension) or PFM (float RGB, by .pfm extension).
void writeImage(const std::filesystem::path& path, const ImageBuffer& image);

// A constant-colour RGBA float image.
std::shared_ptr<ImageBuffer> solidImage(int width, int height, std::array<float, 4> rgba);

// A horizontal ramp in red, vertical in green, constant blue and alpha: 0..1 across the image.
std::shared_ptr<ImageBuffer> rampImage(int width, int height);

}  // namespace testhost
