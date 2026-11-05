#pragma once

#include <cmath>

namespace functional_fx_suite::core::halftone
{
    // |v| folded back and forth into 0..1
    [[nodiscard]] inline double bounce(double v)
    {
        const double r = std::fmod(std::abs(v), 2.0);
        return r > 1.0 ? 2.0 - r : r;
    }

    // distance to the nearest dot centre at grid point (gx, gy), the grid rotated by (cos_t, sin_t): 0 at a centre
    [[nodiscard]] inline float dot(double gx, double gy, double cos_t, double sin_t)
    {
        constexpr double HALF_SQRT2 = 0.707;
        return static_cast<float>(std::hypot(bounce(cos_t * gx - sin_t * gy), bounce(sin_t * gx + cos_t * gy)) *
                                  HALF_SQRT2);
    }
} // namespace functional_fx_suite::core::halftone
