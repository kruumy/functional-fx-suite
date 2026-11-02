#include "effects/halftone.hpp"
#include "core/blur.hpp"
#include "core/color.hpp"
#include "core/edges.hpp"
#include "core/mask.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace functional_fx_suite::effects::halftone
{
    enum class p : std::uint8_t
    {
        dots,
        dots_frequency,
        dots_angle,
        dots_rel_width,
        dots_sharpness,
        dots_lighten,
        smooth_source,
        color1,
        color0,
        dots_shift_x,
        dots_shift_y,
        mask_use,
        blur_mask,
        invert_mask,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    // Dots Angle is in degrees, Dots Shift in dots
    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::choice(1, "dots", "Dots", "Black|White"),
        param::scalar(2, "dots_frequency", "Dots Frequency").value(50).range(-UNBOUNDED, UNBOUNDED).slider(1, 200),
        param::scalar(3, "dots_angle", "Dots Angle").value(30).range(-UNBOUNDED, UNBOUNDED).slider(-180, 180),
        param::scalar(4, "dots_rel_width", "Dots Rel Width").value(1).range(0.01, UNBOUNDED).slider(0.1, 10),
        param::scalar(5, "dots_sharpness", "Dots Sharpness").value(4).range(-UNBOUNDED, UNBOUNDED).slider(0, 10),
        param::scalar(6, "dots_lighten", "Dots Lighten").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(7, "smooth_source", "Smooth Source").value(0).range(0, UNBOUNDED).slider(0, 0.2),
        param::color(8, "color1", "Color1").value({1, 1, 1}),
        param::color(9, "color0", "Color0").value({0, 0, 0}),
        param::scalar(10, "dots_shift_x", "Dots Shift X").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-10, 10),
        param::scalar(11, "dots_shift_y", "Dots Shift Y").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-10, 10),
        param::choice(12, "mask_use", "Mask Use", "Luma|Alpha"),
        param::scalar(13, "blur_mask", "Blur Mask").value(0.05).range(0, 1000).slider(0, 2),
        param::toggle(14, "invert_mask", "Invert Mask"),
    };

    // |v| folded back and forth into 0..1
    static double bounce(double v)
    {
        const double r = std::fmod(std::abs(v), 2.0);
        return r > 1.0 ? 2.0 - r : r;
    }

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const core::frame& f = args.geometry;
        const core::buffer straight = core::edges::straight_rgb(args.src);
        const core::image& s = straight.view();
        core::buffer mono(s.bounds, 1);
        const core::image& bw = mono.view();
        core::parallel::for_range(s.bounds.y0, s.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                for (int x = s.bounds.x0; x < s.bounds.x1; ++x)
                {
                    const float* c = s.at(x, y);
                    *bw.at(x, y) = core::color::luma(c[0], c[1], c[2]);
                }
            }
        });
        const double amount = core::blur::amount_px(v.scalar(p::smooth_source), f);
        core::blur::gaussian(bw, amount, amount);

        // dot grid: one dot per two xy units, rotated against Dots Angle
        const double freq = v.scalar(p::dots_frequency);
        const double scale_x = core::XY_PER_FRAME_UNIT * freq / v.scalar(p::dots_rel_width);
        const double scale_y = core::XY_PER_FRAME_UNIT * freq;
        const double shift_x = v.scalar(p::dots_shift_x);
        const double shift_y = -v.scalar(p::dots_shift_y);
        const double angle = -v.scalar(p::dots_angle) * std::numbers::pi / 180.0;
        const double cos_t = std::cos(angle);
        const double sin_t = std::sin(angle);
        const bool white = v.choice(p::dots) == 1;
        const float s1 = v.scalar_f(p::dots_sharpness) + 1.0f;
        const float darken = -v.scalar_f(p::dots_lighten);
        const auto& c0 = v.vec(p::color0);
        const auto& c1 = v.vec(p::color1);

        const core::image& dst = args.dst;
        core::parallel::for_range(dst.bounds.y0, dst.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                const double gy = f.to_ny(y + 0.5) * scale_y - shift_y;
                for (int x = dst.bounds.x0; x < dst.bounds.x1; ++x)
                {
                    float* d = dst.at(x, y);
                    if (!args.src.bounds.contains(x, y))
                    {
                        std::fill_n(d, 4, 0.0f);
                        continue;
                    }
                    const double gx = f.to_nx(x + 0.5) * scale_x - shift_x;
                    constexpr double HALF_SQRT2 = 0.707;
                    const auto grad = static_cast<float>(
                        std::hypot(bounce(cos_t * gx - sin_t * gy), bounce(sin_t * gx + cos_t * gy)) * HALF_SQRT2);
                    const float tone = core::color::lerp(-1.0f, 1.0f / s1, *bw.at(x, y) - darken);
                    const float t = ((white ? 1.0f - grad : grad) + tone) * s1;
                    const float a = args.src.at(x, y)[3];
                    for (int c = 0; c < 3; ++c)
                    {
                        d[c] = core::color::dissolve(static_cast<float>(c0[c]), static_cast<float>(c1[c]), t) * a;
                    }
                    d[3] = a;
                }
            }
        });

        const auto mode = v.choice(p::mask_use) == 1 ? core::mask::source::alpha : core::mask::source::luma;
        const core::buffer m = core::mask::build(args, mode, v.scalar(p::blur_mask), v.toggle(p::invert_mask));
        if (m.view().data != nullptr)
        {
            core::mask::dissolve(args, m.view());
        }
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_halftone",
            .name = "f_halftone",
            .category = "Functional Stylize",
            .description = "Renders the brightness of the clip as a printed halftone dot screen in two colors.",
            .params = PARAMS,
            .mask_input = "Mask",
            .background_input = "",
            .time_varying = false,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::halftone
