#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace functional_fx_suite::core::color
{
    using rgb = std::array<float, 3>;

    // luma weights of the monochrome conversions
    [[nodiscard]] constexpr float luma(float r, float g, float b)
    {
        return 0.3f * r + 0.59f * g + 0.11f * b;
    }

    // screen that stays additive above 1.0 so super-white values aren't inverted
    [[nodiscard]] constexpr float screen(float a, float b)
    {
        return a + b - std::min(a, 1.0f) * std::min(b, 1.0f);
    }

    [[nodiscard]] constexpr float lerp(float a, float b, float t)
    {
        return a + (b - a) * t;
    }

    // dissolve: a lerp clamped to the two ends
    [[nodiscard]] constexpr float dissolve(float a, float b, float t)
    {
        return t <= 0.0f ? a : t >= 1.0f ? b : lerp(a, b, t);
    }

    // sat_scale and the hue shift pivot on these weights, not on the monochrome ones
    [[nodiscard]] constexpr float sat_luma(const rgb& c)
    {
        return 0.3125f * c[0] + 0.5f * c[1] + 0.1875f * c[2];
    }

    // 0 is gray, 1 keeps the color, above 1 saturates
    [[nodiscard]] constexpr rgb sat_scale(const rgb& c, float s)
    {
        const float l = sat_luma(c);
        return {lerp(l, c[0], s), lerp(l, c[1], s), lerp(l, c[2], s)};
    }

    // plain screen, but never below either input
    [[nodiscard]] constexpr float screen_super(float a, float b)
    {
        return std::max(a + b - a * b, std::max(a, b));
    }

    // overlay of a onto base b, the brighter one once either is super-white
    [[nodiscard]] constexpr float overlay_super(float a, float b)
    {
        if (b <= 0.5f)
        {
            return 2.0f * a * b;
        }
        if (a > 1.0f || b > 1.0f)
        {
            return std::max(a, b);
        }
        return 1.0f - 2.0f * (1.0f - a) * (1.0f - b);
    }

    // one channel: x sign(L) max(0, |L| - D) + D
    [[nodiscard]] constexpr float scale_lights_offset_darks(float x, float lights, float darks)
    {
        const float m = std::max(0.0f, (lights < 0.0f ? -lights : lights) - darks);
        return x * (lights > 0.0f ? m : lights < 0.0f ? -m : 0.0f) + darks;
    }

    // a power inside 0..1, values outside pass through
    [[nodiscard]] inline float gamma(float v, float power)
    {
        return v < 0.0f || v > 1.0f ? v : std::pow(v, power);
    }

    // a sine in turns mapped to 0..1
    [[nodiscard]] inline double nsin(double x)
    {
        return 0.5 + 0.5 * std::sin(2.0 * std::numbers::pi * x);
    }

    // hue in turns; linear in saturation, so values outside 0..1 extrapolate
    [[nodiscard]] rgb hsv_to_rgb(double hue, double saturation, double value);

    // HSV hue rotation by `turns`, rescaled to keep the positive sat_luma
    [[nodiscard]] rgb hue_shift_preserve_luma(const rgb& c, float turns);
} // namespace functional_fx_suite::core::color
