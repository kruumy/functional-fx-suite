#pragma once

#include <cmath>
#include <cstdint>

namespace functional_fx_suite::core::noise
{
    [[nodiscard]] constexpr std::uint32_t hash(std::uint32_t x)
    {
        x ^= x >> 16;
        x *= 0x7feb352dU;
        x ^= x >> 15;
        x *= 0x846ca68bU;
        x ^= x >> 16;
        return x;
    }

    [[nodiscard]] constexpr std::uint32_t combine(std::uint32_t seed, std::uint32_t v)
    {
        return hash(seed ^ hash(v + 0x9e3779b9U));
    }

    // seeds are user floats; quantize so tiny float noise doesn't change the pattern
    [[nodiscard]] inline std::uint32_t seed_bits(double seed)
    {
        return hash(static_cast<std::uint32_t>(static_cast<std::int64_t>(std::floor(seed * 4096.0))));
    }

    // [0, 1)
    [[nodiscard]] constexpr double unit(std::uint32_t h)
    {
        return static_cast<double>(h >> 8) * (1.0 / 16777216.0);
    }

    // [-1, 1)
    [[nodiscard]] constexpr double signed_unit(std::uint32_t h)
    {
        return unit(h) * 2.0 - 1.0;
    }

    [[nodiscard]] constexpr double fade(double t)
    {
        return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
    }

    [[nodiscard]] inline std::uint32_t cell(double v)
    {
        return static_cast<std::uint32_t>(static_cast<std::int64_t>(v));
    }

    // smooth 2D value noise, [0, 1]
    [[nodiscard]] inline double value_2d(std::uint32_t seed, double x, double y)
    {
        const double ix = std::floor(x);
        const double iy = std::floor(y);
        const double ux = fade(x - ix);
        const double uy = fade(y - iy);
        const std::uint32_t row0 = combine(seed, cell(iy));
        const std::uint32_t row1 = combine(seed, cell(iy) + 1);
        const double v00 = unit(combine(row0, cell(ix)));
        const double v10 = unit(combine(row0, cell(ix) + 1));
        const double v01 = unit(combine(row1, cell(ix)));
        const double v11 = unit(combine(row1, cell(ix) + 1));
        const double a = v00 + (v10 - v00) * ux;
        const double b = v01 + (v11 - v01) * ux;
        return a + (b - a) * uy;
    }

    // smooth 3D value noise, [0, 1]
    [[nodiscard]] inline double value_3d(std::uint32_t seed, double x, double y, double z)
    {
        const double iz = std::floor(z);
        const double uz = fade(z - iz);
        const double a = value_2d(combine(seed, cell(iz)), x, y);
        const double b = value_2d(combine(seed, cell(iz) + 1), x, y);
        return a + (b - a) * uz;
    }

    // fractal sum of 2D value noise, amplitude halving per finer octave, [0, 1]
    [[nodiscard]] inline double fbm_2d(std::uint32_t seed, double x, double y, int octaves)
    {
        double sum = 0.0;
        double norm = 0.0;
        double amp = 1.0;
        for (int o = 0; o < octaves; ++o)
        {
            sum += amp * value_2d(seed + static_cast<std::uint32_t>(o), x, y);
            norm += amp;
            amp *= 0.5;
            x *= 2.0;
            y *= 2.0;
        }
        return norm > 0.0 ? sum / norm : 0.5;
    }

    // fractal sum of 3D value noise; gain scales each finer octave, result [0, 1]
    [[nodiscard]] inline double fbm_3d(std::uint32_t seed, double x, double y, double z, int octaves, double gain)
    {
        double sum = 0.0;
        double norm = 0.0;
        double amp = 1.0;
        for (int o = 0; o < octaves; ++o)
        {
            sum += amp * value_3d(seed + static_cast<std::uint32_t>(o), x, y, z);
            norm += amp;
            amp *= gain;
            x *= 2.0;
            y *= 2.0;
            z *= 2.0;
        }
        return norm > 0.0 ? sum / norm : 0.0;
    }
} // namespace functional_fx_suite::core::noise
