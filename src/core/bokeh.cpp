#include "core/bokeh.hpp"
#include "core/noise.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace functional_fx_suite::core::bokeh
{
    // samples per kernel pixel along each axis, for smooth edges
    static constexpr int SUPERSAMPLE = 4;

    double edge(double a, int sides, double pointiness, double rotate)
    {
        if (sides < 3)
        {
            return 1.0;
        }
        const double sector = 2.0 * std::numbers::pi / sides;
        double s = std::fmod(a - rotate - (std::numbers::pi / 2.0), sector);
        s += s < 0.0 ? sector : 0.0;
        const double polygon = std::cos(sector / 2.0) / std::cos(s - (sector / 2.0));
        return 1.0 + (pointiness * (polygon - 1.0));
    }

    buffer kernel(const lens& l)
    {
        const double rx = std::abs(l.radius_x);
        const double ry = std::abs(l.radius_y);
        if (std::max(rx, ry) < 0.5)
        {
            buffer one(rect{0, 0, 1, 1}, 1);
            *one.view().at(0, 0) = 1.0f;
            return one;
        }
        // sides bent outward (pointiness below 0) reach past the unit circle
        const double reach = 1.0 + std::max(0.0, -l.pointiness);
        const int hx = static_cast<int>(std::ceil((rx * reach) + 0.5));
        const int hy = static_cast<int>(std::ceil((ry * reach) + 0.5));
        buffer out(rect{-hx, -hy, hx + 1, hy + 1}, 1);
        const image& k = out.view();

        const double turn = l.rotate * std::numbers::pi / 180.0;
        const std::uint32_t seed = noise::seed_bits(l.noise_seed);
        double total = 0.0;
        for (int y = -hy; y <= hy; ++y)
        {
            for (int x = -hx; x <= hx; ++x)
            {
                double sum = 0.0;
                for (int j = 0; j < SUPERSAMPLE; ++j)
                {
                    for (int i = 0; i < SUPERSAMPLE; ++i)
                    {
                        // normalized lens coordinates, y up
                        const double ex = (x + ((i + 0.5) / SUPERSAMPLE) - 0.5) / std::max(rx, 1e-9);
                        const double ey = -(y + ((j + 0.5) / SUPERSAMPLE) - 0.5) / std::max(ry, 1e-9);
                        const double u =
                            std::hypot(ex, ey) / std::max(edge(std::atan2(ey, ex), l.sides, l.pointiness, turn), 1e-9);
                        if (u >= 1.0)
                        {
                            continue;
                        }
                        double w = std::max(0.0, 1.0 + (l.bokeh * ((2.0 * u * u) - 1.0)));
                        if (l.noise_amp != 0.0)
                        {
                            const double n =
                                noise::fbm_2d(seed, ex * l.noise_freq_x * 0.5, ey * l.noise_freq_y * 0.5, 3);
                            w *= std::max(0.0, 1.0 + (l.noise_amp * ((2.0 * n) - 1.0)));
                        }
                        sum += w;
                    }
                }
                *k.at(x, y) = static_cast<float>(sum);
                total += sum;
            }
        }
        if (total <= 0.0)
        {
            *k.at(0, 0) = 1.0f;
            return out;
        }
        const auto scale = static_cast<float>(1.0 / total);
        for (int y = -hy; y <= hy; ++y)
        {
            std::transform(k.at(-hx, y), k.at(hx + 1, y), k.at(-hx, y), [scale](float v) { return v * scale; });
        }
        return out;
    }
} // namespace functional_fx_suite::core::bokeh
