#include "effects/vignette.hpp"
#include "core/blur.hpp"
#include "core/color.hpp"
#include "core/edges.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace functional_fx_suite::effects::vignette
{
    enum class p : std::uint8_t
    {
        center,
        squareness,
        radius,
        rel_height,
        rel_width,
        rotate,
        edge_softness,
        smooth_curve,
        color,
        opacity,
        blur_amount,
        blur_inside,
        source_brightness,
        combine,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    // Rotate is in degrees
    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::point(1, "center", "Center").value({0, 0, 0}),
        param::scalar(2, "squareness", "Squareness").value(0).range(0, 1),
        param::scalar(3, "radius", "Radius").value(0.9).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(4, "rel_height", "Rel Height").value(0.75).range(0.05, UNBOUNDED).slider(0.05, 2),
        param::scalar(5, "rel_width", "Rel Width").value(1).range(0.05, UNBOUNDED).slider(0.05, 2),
        param::scalar(6, "rotate", "Rotate").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-180, 180),
        param::scalar(7, "edge_softness", "Edge Softness").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(8, "smooth_curve", "Smooth Curve").value(0.4).range(-UNBOUNDED, 1).slider(0, 1),
        param::color(9, "color", "Color").value({0, 0, 0}),
        param::scalar(10, "opacity", "Opacity").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(11, "blur_amount", "Blur Amount").value(0).range(0, UNBOUNDED).slider(0, 2),
        param::toggle(12, "blur_inside", "Blur Inside"),
        param::scalar(13, "source_brightness", "Source Brightness").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::choice(14, "combine", "Combine",
                      "Composite|Mult|Add|Screen|Subtract Inv|Vignette Only|Vignette Only Inv"),
    };

    struct shape
    {
        double cx = 0;
        double cy = 0;
        double cos_t = 1;
        double sin_t = 0;
        double rel_width = 1;
        double rel_height = 1;
        double inner = 0; // radius - softness / 2, where the vignette starts
        double softness = 1;
        double corner = 0;
        double ellipse_amount = 1;
        double smooth_curve = 0;

        // 0 inside, rising to 1 past the soft edge
        [[nodiscard]] double at(double nx, double ny) const
        {
            // the shape params are in xy units
            const double dx = (nx - cx) * core::XY_PER_FRAME_UNIT;
            const double dy = (ny - cy) * core::XY_PER_FRAME_UNIT;
            const double mx = std::abs((cos_t * dx - sin_t * dy) / rel_width);
            const double my = std::abs((sin_t * dx + cos_t * dy) / rel_height);
            const double rounded =
                std::min(mx, my) > corner ? corner + std::hypot(mx - corner, my - corner) : std::max(mx, my);
            constexpr double FAR = 15.0;
            const double ellipse = std::hypot(std::min(FAR, mx), std::min(FAR, my));
            const double d = rounded + (ellipse - rounded) * ellipse_amount;
            const double linear = softness != 0.0 ? std::clamp((d - inner) / softness, 0.0, 1.0)
                                  : d > inner     ? 1.0
                                                  : 0.0;
            const double smooth = 0.5 + 0.5 * std::cos(2.0 * std::numbers::pi * (0.5 - 0.5 * linear));
            return linear + (smooth - linear) * smooth_curve;
        }
    };

    static shape read_shape(const core::param_values& v)
    {
        const double angle = -v.scalar(p::rotate) * std::numbers::pi / 180.0;
        const double softness = v.scalar(p::edge_softness);
        const double inner = v.scalar(p::radius) - 0.5 * softness;
        const double square = v.scalar(p::squareness);
        return shape{v.vec(p::center)[0],
                     v.vec(p::center)[1],
                     std::cos(angle),
                     std::sin(angle),
                     v.scalar(p::rel_width),
                     v.scalar(p::rel_height),
                     inner,
                     softness,
                     inner * square,
                     (1.0 - square) * (1.0 - square),
                     v.scalar(p::smooth_curve)};
    }

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const shape sh = read_shape(v);
        const auto& color = v.vec(p::color);
        const float opacity = v.scalar_f(p::opacity);
        const float brightness = v.scalar_f(p::source_brightness);
        const int mode = v.choice(p::combine);
        const bool blur_inside = v.toggle(p::blur_inside);

        // Blur Amount scaled by the vignette, approximated by blending fixed blur steps
        constexpr int BLUR_STEPS = 3;
        const core::buffer straight = core::edges::straight_rgb(args.src);
        std::vector<core::buffer> steps;
        if (const double amount = core::blur::amount_px(v.scalar(p::blur_amount), args.geometry); amount > 0.0)
        {
            for (int k = 1; k <= BLUR_STEPS; ++k)
            {
                core::buffer& b = steps.emplace_back(core::buffer::copy_of(straight.view()));
                const double a = amount * k / BLUR_STEPS;
                core::blur::gaussian(b.view(), a, a);
            }
        }

        const core::image& dst = args.dst;
        core::parallel::for_range(dst.bounds.y0, dst.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                const double ny = args.geometry.to_ny(y + 0.5);
                for (int x = dst.bounds.x0; x < dst.bounds.x1; ++x)
                {
                    float* d = dst.at(x, y);
                    if (!args.src.bounds.contains(x, y))
                    {
                        std::fill_n(d, 4, 0.0f);
                        continue;
                    }
                    const auto g = static_cast<float>(sh.at(args.geometry.to_nx(x + 0.5), ny));
                    const float a = args.src.at(x, y)[3];
                    std::array<float, 3> s{};
                    std::copy_n(straight.view().at(x, y), 3, s.begin());
                    if (!steps.empty())
                    {
                        const float level = (blur_inside ? 1.0f - g : g) * BLUR_STEPS;
                        const int k = std::clamp(static_cast<int>(level), 0, BLUR_STEPS - 1);
                        const float* lo = k == 0 ? s.data() : steps[static_cast<std::size_t>(k) - 1].view().at(x, y);
                        const float* hi = steps[static_cast<std::size_t>(k)].view().at(x, y);
                        const float t = std::clamp(level - static_cast<float>(k), 0.0f, 1.0f);
                        std::array<float, 3> mixed{};
                        for (int c = 0; c < 3; ++c)
                        {
                            mixed[c] = core::color::lerp(lo[c], hi[c], t);
                        }
                        s = mixed;
                    }
                    const float alpha = opacity * g;
                    for (int c = 0; c < 3; ++c)
                    {
                        const float sc = s[c] * brightness;
                        const auto col = static_cast<float>(color[c]);
                        float out = 0.0f;
                        switch (mode)
                        {
                        case 1:
                            out = sc * core::color::lerp(1.0f, col, alpha);
                            break;
                        case 2:
                            out = sc + col * alpha;
                            break;
                        case 3:
                            out = core::color::screen_super(sc, col * alpha);
                            break;
                        case 4:
                            out = sc - (1.0f - col) * alpha;
                            break;
                        case 5:
                            out = g;
                            break;
                        case 6:
                            out = 1.0f - g;
                            break;
                        default:
                            out = core::color::lerp(sc, col, alpha);
                            break;
                        }
                        d[c] = out * a;
                    }
                    d[3] = a;
                }
            }
        });
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_vignette",
            .name = "f_vignette",
            .category = "Functional Stylize",
            .description =
                "Darkens, tints or blurs the border of the clip around an elliptical or rounded-rectangle area.",
            .params = PARAMS,
            .mask_input = "",
            .background_input = "",
            .time_varying = false,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::vignette
