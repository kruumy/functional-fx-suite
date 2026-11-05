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

    // build() then dissolve(); nothing when no mask is connected
    void apply(const render_args& args, source mode, double blur_width, bool invert);

    // the source picked by a "Luma|Alpha" popup
    [[nodiscard]] constexpr source luma_or_alpha(int choice)
    {
        return choice == 1 ? source::alpha : source::luma;
    }

    // the source picked by a "Luma|Color|Alpha" popup
    [[nodiscard]] constexpr source luma_color_alpha(int choice)
    {
        return choice == 1 ? source::color : choice == 2 ? source::alpha : source::luma;
    }
} // namespace functional_fx_suite::core::mask
