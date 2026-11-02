#pragma once

#include "core/frame.hpp"
#include "core/noise.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace functional_fx_suite::core::damage
{
    struct roll
    {
        double value = 0;
        double slope = 0; // per unit of t
    };

    // a sawtooth from lo to hi with a random phase per period plus weighted noise, running only part of the
    // time. Callers pass threshold = 1.2 / (always + 0.2) - 1, so it is turned back into the fraction of
    // periods that run.
    [[nodiscard]] inline roll sawtooth_plus_noise(double seed, double t, int octaves, double lo, double hi,
                                                  double weight_noise, double weight_saw, double threshold, double freq,
                                                  double noise_freq)
    {
        const double always = threshold > -1.0 ? 1.2 / (threshold + 1.0) - 0.2 : 1.0;
        const double u = t * freq;
        const std::uint32_t period = noise::cell(std::floor(u));
        const std::uint32_t s = noise::seed_bits(seed);
        if (noise::unit(noise::combine(s, period)) >= always)
        {
            return {};
        }
        const double phase = noise::unit(noise::combine(s + 1, period));
        const double saw = u + phase - std::floor(u + phase);
        const double n = noise::fbm_2d(s + 2, t * noise_freq, 0.0, std::max(1, octaves)) - 0.5;
        return {lo + (hi - lo) * (weight_saw * saw + weight_noise * n), (hi - lo) * weight_saw * freq};
    }

    // vignette: 1 inside, falling to 0 past radius + softness / 2 on an ellipse of
    // rel_height, eased by smooth curve 0.4 and dithered by `dither` (0..1 static) / 256; x, y in xy units
    [[nodiscard]] inline double vignette_keep(double x, double y, double radius, double rel_height, double softness,
                                              double dither)
    {
        const double a = radius - softness * 0.5;
        const double b = radius + softness * 0.5;
        const double lo = std::min(a, b);
        const double delta = std::max(0.001, std::max(a, b) - lo);
        const double linear = (std::hypot(x, y / rel_height) - lo) / delta;
        const double smooth = 0.5 + 0.5 * std::cos(2.0 * std::numbers::pi * (0.5 - 0.5 * std::clamp(linear, 0.0, 1.0)));
        const double eased = linear > 0.5 ? std::max(linear, smooth) : std::min(linear, smooth);
        constexpr double SMOOTH_CURVE = 0.4;
        const double g = linear + (eased - linear) * SMOOTH_CURVE;
        return std::clamp(1.0 - (g + (dither - 0.5) / 256.0), 0.0, 1.0);
    }

    // pixel indices counted from the frame centre, y up, so per-pixel noise doesn't depend on a host's pixel space
    [[nodiscard]] inline std::array<std::uint32_t, 2> frame_pixel(const frame& f, int x, int y)
    {
        return {static_cast<std::uint32_t>(x - static_cast<std::int64_t>(std::floor(f.cx))),
                static_cast<std::uint32_t>(static_cast<std::int64_t>(std::floor(f.cy)) - y)};
    }

    // a fresh uniform 0..1 per pixel and frame
    [[nodiscard]] inline double pixel_static(std::uint32_t frame_seed, const frame& f, int x, int y)
    {
        const auto [px, py] = frame_pixel(f, x, y);
        return noise::unit(noise::combine(noise::combine(frame_seed, px), py));
    }
} // namespace functional_fx_suite::core::damage
