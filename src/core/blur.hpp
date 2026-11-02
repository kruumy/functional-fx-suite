#pragma once

#include "core/frame.hpp"
#include "core/image.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace functional_fx_suite::core::blur
{
    // a "blur width" parameter (frame units) in pixels: width * 0.125 * frame width
    inline constexpr double PIXELS_PER_FRAME_UNIT = 0.125;

    [[nodiscard]] constexpr double amount_px(double width, const frame& f)
    {
        return f.to_px(width) * PIXELS_PER_FRAME_UNIT;
    }

    // what the blur sees past the image edge; order matches the "Edge Mode" popup
    enum class edge : std::uint8_t
    {
        transparent,
        reflect
    };

    using radii = std::array<int, 3>;

    // split of a total reach (pixels) over three box passes
    [[nodiscard]] radii split(int total);

    // pixels a blur of this amount reaches on either side
    [[nodiscard]] int reach(double amount);

    // three box passes over n contiguous pixels of `channels` floats, zero outside; result left in `line`
    void box_line(std::vector<float>& line, std::vector<float>& scratch, int n, int channels, const radii& r);

    // in-place gaussian blur: amount * 4/3 split over three box passes
    void gaussian(const image& img, double amount_x, double amount_y, edge mode = edge::transparent);
} // namespace functional_fx_suite::core::blur
