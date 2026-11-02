#pragma once

#include "core/buffer.hpp"
#include <cstddef>
#include <cstdint>

namespace functional_fx_suite::hosts::ofx::pixels
{
    enum class depth : std::uint8_t
    {
        byte,
        word,
        real
    };

    // a host image: OFX pixel coordinates (y up), bounds half-open
    struct host_image
    {
        void* data = nullptr;
        int x1 = 0;
        int y1 = 0;
        int x2 = 0;
        int y2 = 0;
        std::ptrdiff_t row_bytes = 0;
        depth bit_depth = depth::real;
        int components = 4; // 4 RGBA or 1 alpha
        bool premultiplied = true;
    };

    // OFX rect (y up) to core rect (y down); core y = -1 - ofx y
    [[nodiscard]] constexpr core::rect to_core(int x1, int y1, int x2, int y2)
    {
        return core::rect{x1, -y2, x2, -y1};
    }

    // premultiplied float RGBA copy of the image in core coordinates
    [[nodiscard]] core::buffer read(const host_image& img);

    // writes the part of src inside `window` (core coordinates) into the host image
    void write(const core::image& src, const host_image& dst, core::rect window);
} // namespace functional_fx_suite::hosts::ofx::pixels
