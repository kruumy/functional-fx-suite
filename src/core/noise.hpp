#pragma once

#include <array>
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

    // the values at the corners of lattice cell (ix, iy): v00, v10, v01, v11
    using corners = std::array<double, 4>;

    [[nodiscard]] inline corners corners_of(std::uint32_t seed, double ix, double iy)
    {
        const std::uint32_t row0 = combine(seed, cell(iy));
        const std::uint32_t row1 = combine(seed, cell(iy) + 1);
        return {unit(combine(row0, cell(ix))), unit(combine(row0, cell(ix) + 1)), unit(combine(row1, cell(ix))),
                unit(combine(row1, cell(ix) + 1))};
    }

    [[nodiscard]] inline double blend(const corners& v, double ux, double uy)
    {
        const double a = v[0] + (v[1] - v[0]) * ux;
        const double b = v[2] + (v[3] - v[2]) * ux;
        return a + (b - a) * uy;
    }

    // smooth 2D value noise, [0, 1]
    [[nodiscard]] inline double value_2d(std::uint32_t seed, double x, double y)
    {
        const double ix = std::floor(x);
        const double iy = std::floor(y);
        return blend(corners_of(seed, ix, iy), fade(x - ix), fade(y - iy));
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

    // value_3d that keeps the hashes of the last lattice cell it was asked for, so lookups close to the previous one,
    // as along a row of pixels, skip most of the hashing; same results as value_3d
    class cached_value_3d
    {
      public:
        [[nodiscard]] double at(std::uint32_t seed, double x, double y, double z)
        {
            const double iz = std::floor(z);
            if (seed != seed_ || iz != iz_)
            {
                seed_ = seed;
                iz_ = iz;
                layers_[0].seed = combine(seed, cell(iz));
                layers_[1].seed = combine(seed, cell(iz) + 1);
                layers_[0].valid = layers_[1].valid = false;
            }
            const double ix = std::floor(x);
            const double iy = std::floor(y);
            const double ux = fade(x - ix);
            const double uy = fade(y - iy);
            const double a = layers_[0].at(ix, iy, ux, uy);
            const double b = layers_[1].at(ix, iy, ux, uy);
            return a + (b - a) * fade(z - iz);
        }

      private:
        struct layer
        {
            std::uint32_t seed = 0;
            bool valid = false;
            double ix = 0;
            double iy = 0;
            corners v{};

            [[nodiscard]] double at(double cx, double cy, double ux, double uy)
            {
                if (!valid || cx != ix || cy != iy)
                {
                    v = corners_of(seed, cx, cy);
                    ix = cx;
                    iy = cy;
                    valid = true;
                }
                return blend(v, ux, uy);
            }
        };

        std::uint32_t seed_ = 0;
        double iz_ = std::nan("");
        std::array<layer, 2> layers_{};
    };

    // fractal sum of 2D value noise over octaves >= 1, amplitude halving per finer octave, [0, 1]
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
        return sum / norm;
    }

    // fractal sum of value(octave, x, y, z) for 3D value noise; gain scales each finer octave, result [0, 1]
    template <class Value>
    [[nodiscard]] double fbm_3d_of(const Value& value, double x, double y, double z, int octaves, double gain)
    {
        double sum = 0.0;
        double norm = 0.0;
        double amp = 1.0;
        for (int o = 0; o < octaves; ++o)
        {
            sum += amp * value(o, x, y, z);
            norm += amp;
            amp *= gain;
            x *= 2.0;
            y *= 2.0;
            z *= 2.0;
        }
        return norm > 0.0 ? sum / norm : 0.0;
    }

    [[nodiscard]] inline double fbm_3d(std::uint32_t seed, double x, double y, double z, int octaves, double gain)
    {
        return fbm_3d_of([seed](int o, double ox, double oy,
                                double oz) { return value_3d(seed + static_cast<std::uint32_t>(o), ox, oy, oz); },
                         x, y, z, octaves, gain);
    }

    // fbm_3d through one cached_value_3d per octave; octaves past the cache's are hashed as usual
    class cached_fbm_3d
    {
      public:
        static constexpr int OCTAVES = 8;

        [[nodiscard]] double at(std::uint32_t seed, double x, double y, double z, int octaves, double gain)
        {
            return fbm_3d_of(
                [&](int o, double ox, double oy, double oz) {
                    const std::uint32_t s = seed + static_cast<std::uint32_t>(o);
                    return o < OCTAVES ? octaves_[static_cast<std::size_t>(o)].at(s, ox, oy, oz)
                                       : value_3d(s, ox, oy, oz);
                },
                x, y, z, octaves, gain);
        }

      private:
        std::array<cached_value_3d, OCTAVES> octaves_{};
    };
} // namespace functional_fx_suite::core::noise
