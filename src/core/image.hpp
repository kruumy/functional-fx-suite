#pragma once

#include "core/rect.hpp"
#include <cstddef>

namespace functional_fx_suite::core
{
    // non-owning view of float pixels; `channels` interleaved values per pixel
    struct image
    {
        float* data = nullptr;
        rect bounds;
        std::ptrdiff_t stride = 0; // floats per row
        int channels = 4;

        [[nodiscard]] float* row(int y) const
        {
            return data + (y - bounds.y0) * stride - static_cast<std::ptrdiff_t>(bounds.x0) * channels;
        }
        [[nodiscard]] float* at(int x, int y) const
        {
            return row(y) + static_cast<std::ptrdiff_t>(x) * channels;
        }
    };
} // namespace functional_fx_suite::core
