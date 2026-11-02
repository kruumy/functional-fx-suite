#pragma once

#include "core/buffer.hpp"
#include "core/frame.hpp"
#include "core/image.hpp"
#include <array>

namespace functional_fx_suite::core::edges
{
    // edge strength unit: scale * 0.05 * frame width
    inline constexpr double SCALE_PER_FRAME_UNIT = 0.05;

    // straight (unpremultiplied) rgb of `src` over its bounds; effects that leave alpha alone work on this
    [[nodiscard]] buffer straight_rgb(const image& src);

    // the input of edge detection: every channel gaussian-blurred by `smooth` (frame units,
    // reflected edges) and times scale * 0.05 * frame width
    void prepare(const image& img, double smooth, double scale, const frame& f);

    // channel c's neighbour differences two pixels apart (one-sided and doubled at the border), y up
    [[nodiscard]] std::array<float, 2> gradient(const image& img, int x, int y, int c);

    // edge detection after prepare(): the gradient magnitude of every channel
    [[nodiscard]] buffer magnitude(const image& img);
} // namespace functional_fx_suite::core::edges
