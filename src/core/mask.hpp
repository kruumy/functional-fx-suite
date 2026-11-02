#pragma once

#include "core/buffer.hpp"
#include "core/render_args.hpp"
#include <cstdint>

namespace functional_fx_suite::core::mask
{
    enum class source : std::uint8_t
    {
        luma,
        alpha,
        color
    };

    // processed mask covering at least the source bounds: 1 channel, or 3 for source::color
    // returns an empty buffer view (data == nullptr) when no mask is connected
    [[nodiscard]] buffer build(const render_args& args, source mode, double blur_width, bool invert);

    // dst = src + (dst - src) * mask, for a 1-channel mask from build()
    void dissolve(const render_args& args, const image& mask);
} // namespace functional_fx_suite::core::mask
