#pragma once

namespace functional_fx_suite::core
{
    // `xy` coordinates span -1..1 across the frame width
    inline constexpr double XY_PER_FRAME_UNIT = 2.0;

    // maps normalized frame units (origin at frame centre, 1.0 = frame width, y up) to pixels
    struct frame
    {
        double cx = 0; // frame centre in pixel coordinates
        double cy = 0;
        double width = 1; // frame width in pixels

        [[nodiscard]] constexpr double to_px_x(double nx) const
        {
            return cx + nx * width;
        }
        [[nodiscard]] constexpr double to_px_y(double ny) const
        {
            return cy - ny * width;
        }
        [[nodiscard]] constexpr double to_nx(double px) const
        {
            return (px - cx) / width;
        }
        [[nodiscard]] constexpr double to_ny(double py) const
        {
            return (cy - py) / width;
        }
        // blur widths and distances are in frame units
        [[nodiscard]] constexpr double to_px(double n) const
        {
            return n * width;
        }
    };
} // namespace functional_fx_suite::core
