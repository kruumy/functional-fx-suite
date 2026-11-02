#pragma once

#include "core/buffer.hpp"

namespace functional_fx_suite::core::bokeh
{
    // lens kernel arguments, radii in pixels
    struct lens
    {
        double radius_x = 0;
        double radius_y = 0;
        int sides = 0;         // 0 is a circle
        double pointiness = 0; // 0 round, 1 straight polygon sides, above 1 sides bent inward
        double rotate = 0;     // degrees
        double bokeh = 0;      // above 0 brightens the rim, below 0 the centre
        double noise_amp = 0;
        double noise_freq_x = 40; // noise cells across the kernel
        double noise_freq_y = 40;
        double noise_seed = 0;
    };

    // distance from the centre to the edge at angle a (radians, y up): 1 for a circle (sides below 3), else a polygon
    // of unit circumradius with a vertex up at rotate 0 (radians, counter-clockwise), sides bent by pointiness
    [[nodiscard]] double edge(double a, int sides, double pointiness, double rotate);

    // 1-channel kernel summing to 1, odd size, centred on its middle pixel; a single pixel when the lens is that small
    [[nodiscard]] buffer kernel(const lens& l);
} // namespace functional_fx_suite::core::bokeh
