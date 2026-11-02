#include "core/flare.hpp"
#include "core/bokeh.hpp"
#include "core/noise.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace functional_fx_suite::core::flare
{
    // our own lens designs; sizes in flare units (Scale Widths), positions along the hotspot-pivot axis
    static constexpr std::array BASIC_GLOW{
        element{.shape = kind::glow,
                .steer = role::hotspot,
                .size = 0.6,
                .color = {1, 0.85f, 0.65f},
                .brightness = 0.6f,
                .priority = true},
        element{.shape = kind::glow, .steer = role::hotspot, .size = 0.12, .brightness = 1, .priority = true},
    };

    static constexpr std::array PRIME{
        element{.shape = kind::glow,
                .steer = role::hotspot,
                .size = 0.5,
                .color = {1, 0.9f, 0.75f},
                .brightness = 0.5f,
                .priority = true},
        element{.shape = kind::glow, .steer = role::hotspot, .size = 0.08, .brightness = 1, .priority = true},
        element{.shape = kind::star,
                .steer = role::rays,
                .size = 0.45,
                .color = {1, 0.95f, 0.9f},
                .brightness = 0.5f,
                .rays = 6,
                .thickness = 0.012,
                .vary = 0.3,
                .priority = true},
        element{.shape = kind::ghost,
                .position = 0.55,
                .size = 0.05,
                .color = {0.6f, 0.9f, 0.7f},
                .brightness = 0.3f,
                .softness = 0.3},
        element{.shape = kind::ghost,
                .position = 0.25,
                .size = 0.09,
                .color = {0.7f, 0.6f, 1},
                .brightness = 0.2f,
                .rim = 0.4,
                .chroma = 0.05},
        element{.shape = kind::ghost, .position = -0.15, .size = 0.04, .color = {1, 0.7f, 0.5f}, .brightness = 0.375f},
        element{.shape = kind::ghost,
                .position = -0.45,
                .size = 0.14,
                .color = {0.5f, 0.8f, 1},
                .brightness = 0.15f,
                .rim = 0.6,
                .chroma = 0.08},
        element{.shape = kind::ghost, .position = -0.8, .size = 0.07, .color = {0.9f, 0.6f, 1}, .brightness = 0.25f},
        element{.shape = kind::ghost,
                .position = -1.1,
                .size = 0.22,
                .color = {0.6f, 1, 0.8f},
                .brightness = 0.1f,
                .rim = 0.8},
    };

    static constexpr std::array ZOOM{
        element{.shape = kind::glow,
                .steer = role::hotspot,
                .size = 0.4,
                .color = {1, 0.95f, 0.85f},
                .brightness = 0.5f,
                .priority = true},
        element{.shape = kind::glow, .steer = role::hotspot, .size = 0.06, .brightness = 1, .priority = true},
        element{.shape = kind::star,
                .steer = role::rays,
                .size = 0.3,
                .brightness = 0.35f,
                .rays = 8,
                .thickness = 0.015,
                .vary = 0.5,
                .priority = true},
        element{.shape = kind::ghost,
                .position = 0.8,
                .size = 0.03,
                .color = {1, 0.8f, 0.5f},
                .brightness = 0.5f,
                .sides = 6,
                .rotate = 15},
        element{.shape = kind::ghost,
                .position = 0.6,
                .size = 0.06,
                .color = {0.6f, 1, 0.6f},
                .brightness = 0.25f,
                .sides = 6,
                .rotate = 15},
        element{.shape = kind::ghost,
                .position = 0.35,
                .size = 0.12,
                .color = {0.5f, 0.7f, 1},
                .brightness = 0.175f,
                .sides = 6,
                .rotate = 15,
                .rim = 0.3},
        element{.shape = kind::ghost,
                .position = 0.1,
                .size = 0.05,
                .color = {1, 0.5f, 0.4f},
                .brightness = 0.3f,
                .sides = 6,
                .rotate = 15},
        element{.shape = kind::ghost,
                .position = -0.2,
                .size = 0.18,
                .color = {0.8f, 0.6f, 1},
                .brightness = 0.125f,
                .sides = 6,
                .rotate = 15,
                .rim = 0.5,
                .chroma = 0.04},
        element{.shape = kind::ghost,
                .position = -0.4,
                .size = 0.08,
                .color = {0.5f, 1, 0.9f},
                .brightness = 0.25f,
                .sides = 6,
                .rotate = 15},
        element{.shape = kind::ghost,
                .position = -0.65,
                .size = 0.25,
                .color = {1, 0.85f, 0.6f},
                .brightness = 0.1f,
                .sides = 6,
                .rotate = 15,
                .rim = 0.6},
        element{.shape = kind::ghost,
                .position = -0.9,
                .size = 0.1,
                .color = {0.6f, 0.6f, 1},
                .brightness = 0.2f,
                .sides = 6,
                .rotate = 15},
        element{.shape = kind::ghost,
                .position = -1.2,
                .size = 0.05,
                .color = {1, 0.6f, 0.8f},
                .brightness = 0.3f,
                .sides = 6,
                .rotate = 15},
    };

    static constexpr std::array ANAMORPHIC{
        element{.shape = kind::streak,
                .steer = role::rays,
                .size = 2.5,
                .color = {0.45f, 0.65f, 1},
                .brightness = 0.6f,
                .thickness = 0.004,
                .priority = true},
        element{.shape = kind::streak,
                .steer = role::rays,
                .size = 1,
                .color = {0.6f, 0.8f, 1},
                .brightness = 0.3f,
                .thickness = 0.012,
                .priority = true},
        element{.shape = kind::glow,
                .steer = role::hotspot,
                .size = 0.35,
                .color = {0.7f, 0.85f, 1},
                .brightness = 0.5f,
                .aspect = 0.6,
                .priority = true},
        element{.shape = kind::glow, .steer = role::hotspot, .size = 0.06, .brightness = 1, .priority = true},
        element{.shape = kind::ghost,
                .position = 0.4,
                .size = 0.08,
                .color = {0.4f, 0.6f, 1},
                .brightness = 0.25f,
                .aspect = 0.6},
        element{.shape = kind::ghost,
                .position = -0.3,
                .size = 0.14,
                .color = {0.5f, 0.7f, 1},
                .brightness = 0.15f,
                .aspect = 0.6,
                .rim = 0.5},
        element{.shape = kind::ghost,
                .position = -0.7,
                .size = 0.05,
                .color = {0.6f, 0.8f, 1},
                .brightness = 0.375f,
                .aspect = 0.6},
        element{.shape = kind::ghost,
                .position = -1.1,
                .size = 0.2,
                .color = {0.3f, 0.5f, 1},
                .brightness = 0.1f,
                .aspect = 0.6,
                .rim = 0.7},
    };

    static constexpr std::array SUN{
        element{.shape = kind::glow,
                .steer = role::hotspot,
                .size = 1,
                .color = {1, 0.8f, 0.55f},
                .brightness = 0.35f,
                .priority = true},
        element{.shape = kind::glow,
                .steer = role::hotspot,
                .size = 0.25,
                .color = {1, 0.95f, 0.8f},
                .brightness = 0.8f,
                .priority = true},
        element{.shape = kind::glow, .steer = role::hotspot, .size = 0.05, .brightness = 1.5f, .priority = true},
        element{.shape = kind::star,
                .steer = role::rays,
                .size = 0.8,
                .color = {1, 0.9f, 0.7f},
                .brightness = 0.6f,
                .rays = 12,
                .thickness = 0.01,
                .vary = 0.5,
                .priority = true},
        element{.shape = kind::star,
                .steer = role::rays,
                .size = 0.5,
                .color = {1, 0.95f, 0.85f},
                .brightness = 0.3f,
                .rotate = 5,
                .rays = 36,
                .thickness = 0.004,
                .vary = 0.8,
                .priority = true},
        element{.shape = kind::ghost, .position = -0.3, .size = 0.1, .color = {1, 0.7f, 0.4f}, .brightness = 0.15f},
        element{.shape = kind::ghost,
                .position = -0.7,
                .size = 0.2,
                .color = {0.8f, 0.9f, 0.5f},
                .brightness = 0.075f,
                .rim = 0.5},
    };

    static constexpr std::array HEX_GHOSTS{
        element{.shape = kind::glow,
                .steer = role::hotspot,
                .size = 0.3,
                .color = {1, 0.95f, 0.9f},
                .brightness = 0.4f,
                .priority = true},
        element{.shape = kind::glow, .steer = role::hotspot, .size = 0.05, .brightness = 1, .priority = true},
        element{.shape = kind::ghost,
                .position = 0.7,
                .size = 0.04,
                .color = {0.6f, 1, 0.7f},
                .brightness = 0.375f,
                .sides = 6,
                .softness = 0.1,
                .rim = 0.25,
                .chroma = 0.03},
        element{.shape = kind::ghost,
                .position = 0.45,
                .size = 0.07,
                .color = {0.5f, 0.8f, 1},
                .brightness = 0.25f,
                .sides = 6,
                .softness = 0.1,
                .rim = 0.25,
                .chroma = 0.03},
        element{.shape = kind::ghost,
                .position = 0.2,
                .size = 0.1,
                .color = {0.8f, 0.6f, 1},
                .brightness = 0.2f,
                .sides = 6,
                .softness = 0.1,
                .rim = 0.25,
                .chroma = 0.03},
        element{.shape = kind::ghost,
                .position = -0.05,
                .size = 0.06,
                .color = {1, 0.7f, 0.5f},
                .brightness = 0.3f,
                .sides = 6,
                .softness = 0.1,
                .rim = 0.25,
                .chroma = 0.03},
        element{.shape = kind::ghost,
                .position = -0.3,
                .size = 0.13,
                .color = {0.5f, 1, 0.9f},
                .brightness = 0.15f,
                .sides = 6,
                .softness = 0.1,
                .rim = 0.25,
                .chroma = 0.03},
        element{.shape = kind::ghost,
                .position = -0.55,
                .size = 0.09,
                .color = {1, 0.6f, 0.7f},
                .brightness = 0.225f,
                .sides = 6,
                .softness = 0.1,
                .rim = 0.25,
                .chroma = 0.03},
        element{.shape = kind::ghost,
                .position = -0.8,
                .size = 0.17,
                .color = {0.6f, 0.7f, 1},
                .brightness = 0.125f,
                .sides = 6,
                .softness = 0.1,
                .rim = 0.25,
                .chroma = 0.03},
        element{.shape = kind::ghost,
                .position = -1.1,
                .size = 0.11,
                .color = {1, 0.9f, 0.5f},
                .brightness = 0.2f,
                .sides = 6,
                .softness = 0.1,
                .rim = 0.25,
                .chroma = 0.03},
    };

    static constexpr std::array LENSES{
        lens{"Basic Glow", BASIC_GLOW}, lens{"Prime", PRIME}, lens{"Zoom", ZOOM},
        lens{"Anamorphic", ANAMORPHIC}, lens{"Sun", SUN},     lens{"Hex Ghosts", HEX_GHOSTS},
    };

    std::span<const lens> lenses()
    {
        return LENSES;
    }

    // one element resolved to pixels
    struct placed
    {
        const element* e = nullptr;
        double cx = 0; // pixels, y down
        double cy = 0;
        double rx = 0; // radius or length, pixels
        double ry = 0;
        double width = 0;  // star and streak half width, pixels
        double rotate = 0; // radians
        int rays = 0;
        double reach_x = 0;
        double reach_y = 0;
        color::rgb color{};
        std::uint32_t seed = 0;
    };

    // the element's 0..1 profile per channel at (dx, dy) pixels from its centre, y up
    static color::rgb profile(const placed& p, double dx, double dy, double gamma)
    {
        const element& e = *p.e;
        const auto shaped = [gamma](double v) { return static_cast<float>(std::pow(std::max(0.0, v), gamma)); };
        const double ex = dx / p.rx;
        const double ey = dy / p.ry;
        switch (e.shape)
        {
        case kind::glow: {
            const double f = std::max(0.0, 1.0 - std::hypot(ex, ey));
            const float v = shaped(f * f);
            return {v, v, v};
        }
        case kind::ghost: {
            const double r =
                std::hypot(ex, ey) / std::max(bokeh::edge(std::atan2(ey, ex), e.sides, 1.0, p.rotate), 1e-9);
            color::rgb out{};
            for (int c = 0; c < 3; ++c)
            {
                const double u = r / (1.0 + (e.chroma * (1 - c)));
                const double inside = std::clamp((1.0 - u) / std::max(e.softness, 1e-6), 0.0, 1.0);
                const double v = inside * std::max(0.0, 1.0 + (e.rim * ((2.0 * u * u) - 1.0)));
                out[static_cast<std::size_t>(c)] = shaped(v);
            }
            return out;
        }
        case kind::star: {
            const double r = std::hypot(dx, dy);
            const double sector = 2.0 * std::numbers::pi / p.rays;
            const double a = std::atan2(dy, dx) - p.rotate;
            const double j = std::round(a / sector);
            const double delta = a - (j * sector);
            if (std::cos(delta) <= 0.0)
            {
                return {};
            }
            const auto ray = static_cast<std::uint32_t>(((static_cast<long long>(j) % p.rays) + p.rays) % p.rays);
            const double length = p.rx * (1.0 - (e.vary * noise::unit(noise::combine(p.seed, ray))));
            const double along = std::max(0.0, 1.0 - (r / std::max(length, 1e-6)));
            const double across = std::max(0.0, 1.0 - (std::abs(std::sin(delta)) * r / p.width));
            const float v = shaped(along * along * across);
            return {v, v, v};
        }
        case kind::streak: {
            const double along = (dx * std::cos(p.rotate)) + (dy * std::sin(p.rotate));
            const double across = (-dx * std::sin(p.rotate)) + (dy * std::cos(p.rotate));
            const double f = std::max(0.0, 1.0 - (std::abs(along) / p.rx));
            const double g = std::max(0.0, 1.0 - (std::abs(across) / p.width));
            const float v = shaped(f * f * f * g * g);
            return {v, v, v};
        }
        }
        return {};
    }

    void draw(const image& dst, const lens& l, const settings& s)
    {
        std::vector<placed> items;
        std::uint32_t index = 0;
        for (const element& e : l.elements)
        {
            ++index;
            if (s.priority_only && !e.priority)
            {
                continue;
            }
            placed p{.e = &e};
            p.cx = s.pivot_x + (e.position * (s.hotspot_x - s.pivot_x));
            p.cy = s.pivot_y + (e.position * (s.hotspot_y - s.pivot_y));
            double size = e.size * s.unit * s.width_scale;
            const color::rgb* steer = &s.other_color;
            p.rotate = e.rotate * std::numbers::pi / 180.0;
            if (e.steer == role::hotspot)
            {
                steer = &s.hotspot_color;
            }
            else if (e.steer == role::rays)
            {
                steer = &s.rays_color;
                size *= s.rays_length;
                p.rotate += s.rays_rotate * std::numbers::pi / 180.0;
            }
            else
            {
                size *= s.other_width;
            }
            p.rx = std::max(std::abs(size), 1e-6);
            p.ry = std::max(std::abs(size * e.aspect * s.rel_height), 1e-6);
            // thin rays keep at least a pixel so they do not vanish
            p.width = std::max(0.75, e.thickness * p.rx * s.rays_thickness);
            p.rays = std::max(1, static_cast<int>(std::lround(e.rays * s.rays_num_scale)));
            p.seed = noise::hash(index);
            const double spread = (1.0 + std::abs(e.chroma)) * (1.0 + e.softness);
            const bool line = e.shape == kind::star || e.shape == kind::streak;
            p.reach_x = line ? p.rx + p.width : p.rx * spread;
            p.reach_y = line ? p.rx + p.width : p.ry * spread;
            const color::rgb tint = color::sat_scale(e.color, s.saturation);
            for (int c = 0; c < 3; ++c)
            {
                const auto i = static_cast<std::size_t>(c);
                p.color[i] = tint[i] * (*steer)[i] * e.brightness;
            }
            items.push_back(p);
        }

        const rect& b = dst.bounds;
        parallel::for_range(b.y0, b.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                const double py = y + 0.5;
                for (const placed& p : items)
                {
                    if (std::abs(py - p.cy) > p.reach_y)
                    {
                        continue;
                    }
                    const int x0 = std::max(b.x0, static_cast<int>(std::floor(p.cx - p.reach_x)));
                    const int x1 = std::min(b.x1, static_cast<int>(std::ceil(p.cx + p.reach_x)) + 1);
                    const bool atmosphere = p.e->steer == role::rays && s.atmosphere != nullptr;
                    for (int x = x0; x < x1; ++x)
                    {
                        const double px = x + 0.5;
                        color::rgb v = profile(p, px - p.cx, p.cy - py, s.gamma);
                        const float k =
                            atmosphere ? s.atmosphere->at(s.geometry.to_nx(px), s.geometry.to_ny(py)) : 1.0f;
                        float* d = dst.at(x, y);
                        for (int c = 0; c < 3; ++c)
                        {
                            const auto i = static_cast<std::size_t>(c);
                            d[c] += p.color[i] * v[i] * k;
                        }
                    }
                }
            }
        });
    }
} // namespace functional_fx_suite::core::flare
