#include "effects/halftonecolor.hpp"
#include "core/blur.hpp"
#include "core/color.hpp"
#include "core/edges.hpp"
#include "core/halftone.hpp"
#include "core/mask.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace functional_fx_suite::effects::halftonecolor
{
    enum class p : std::uint8_t
    {
        dots_color,
        dots_frequency,
        dots_angle,
        dots_rel_width,
        dots_sharpness,
        dots_lighten,
        smooth_source,
        saturation,
        dots_shift_x,
        dots_shift_y,
        dots_shift_rgb,
        shift_red_x,
        shift_red_y,
        shift_green_x,
        shift_green_y,
        shift_blue_x,
        shift_blue_y,
        dots_shift_rgb_end,
        mask_use,
        blur_mask,
        invert_mask,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    static constexpr param shift(std::uint16_t id, std::string_view name, std::string_view label, double def)
    {
        return param::scalar(id, name, label).value(def).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1);
    }

    // Dots Angle is in degrees, shifts in dots
    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::choice(1, "dots_color", "Dots Color", "CMY|RGB"),
        param::scalar(2, "dots_frequency", "Dots Frequency").value(50).range(-UNBOUNDED, UNBOUNDED).slider(1, 200),
        param::scalar(3, "dots_angle", "Dots Angle").value(30).range(-UNBOUNDED, UNBOUNDED).slider(-180, 180),
        param::scalar(4, "dots_rel_width", "Dots Rel Width").value(1).range(0.01, UNBOUNDED).slider(0.1, 10),
        param::scalar(5, "dots_sharpness", "Dots Sharpness").value(4).range(-0.99, UNBOUNDED).slider(0, 10),
        param::scalar(6, "dots_lighten", "Dots Lighten").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(7, "smooth_source", "Smooth Source").value(0).range(0, UNBOUNDED).slider(0, 0.2),
        param::scalar(8, "saturation", "Saturation").value(1).range(-2, 10).slider(0, 2),
        shift(9, "dots_shift_x", "Dots Shift X", 0).slider(-10, 10),
        shift(10, "dots_shift_y", "Dots Shift Y", 0).slider(-10, 10),
        param::group(11, "dots_shift_rgb", "Dots Shift RGB"),
        shift(12, "shift_red_x", "Shift Red X", 0),
        shift(13, "shift_red_y", "Shift Red Y", 0.5),
        shift(14, "shift_green_x", "Shift Green X", 0),
        shift(15, "shift_green_y", "Shift Green Y", 0),
        shift(16, "shift_blue_x", "Shift Blue X", 0),
        shift(17, "shift_blue_y", "Shift Blue Y", -0.5),
        param::group_end(18, "dots_shift_rgb_end"),
        param::choice(19, "mask_use", "Mask Use", "Luma|Alpha"),
        param::scalar(20, "blur_mask", "Blur Mask").value(0.05).range(0, 1000).slider(0, 2),
        param::toggle(21, "invert_mask", "Invert Mask"),
    };

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const core::frame& f = args.geometry;
        const core::buffer straight = core::edges::straight_rgb(args.src);
        const core::image& s = straight.view();
        const double amount = core::blur::amount_px(v.scalar(p::smooth_source), f);
        core::blur::gaussian(s, amount, amount);

        // one dot grid per channel, each offset by its own shift (y shifts point down), rotated against Dots Angle
        const double freq = v.scalar(p::dots_frequency);
        const double scale_x = core::XY_PER_FRAME_UNIT * freq / v.scalar(p::dots_rel_width);
        const double scale_y = core::XY_PER_FRAME_UNIT * freq;
        const std::array shift_x{v.scalar(p::dots_shift_x) + v.scalar(p::shift_red_x),
                                 v.scalar(p::dots_shift_x) + v.scalar(p::shift_green_x),
                                 v.scalar(p::dots_shift_x) + v.scalar(p::shift_blue_x)};
        const std::array shift_y{-v.scalar(p::dots_shift_y) - v.scalar(p::shift_red_y),
                                 -v.scalar(p::dots_shift_y) - v.scalar(p::shift_green_y),
                                 -v.scalar(p::dots_shift_y) - v.scalar(p::shift_blue_y)};
        const double angle = -v.scalar(p::dots_angle) * std::numbers::pi / 180.0;
        const double cos_t = std::cos(angle);
        const double sin_t = std::sin(angle);
        const bool rgb_dots = v.choice(p::dots_color) == 1;
        const float s1 = v.scalar_f(p::dots_sharpness) + 1.0f;
        const float darken = -v.scalar_f(p::dots_lighten);
        const float saturation = v.scalar_f(p::saturation);

        const core::image& dst = args.dst;
        core::parallel::for_range(dst.bounds.y0, dst.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                const double ny = f.to_ny(y + 0.5) * scale_y;
                for (int x = dst.bounds.x0; x < dst.bounds.x1; ++x)
                {
                    float* d = dst.at(x, y);
                    if (!args.src.bounds.contains(x, y))
                    {
                        std::fill_n(d, 4, 0.0f);
                        continue;
                    }
                    const double nx = f.to_nx(x + 0.5) * scale_x;
                    const float* in = s.at(x, y);
                    core::color::rgb out{};
                    for (int c = 0; c < 3; ++c)
                    {
                        const double gx = nx - shift_x[c];
                        const double gy = ny - shift_y[c];
                        const float grad = core::halftone::dot(gx, gy, cos_t, sin_t);
                        const float tone = core::color::lerp(-1.0f, 1.0f / s1, in[c] - darken);
                        out[c] = std::clamp(((rgb_dots ? 1.0f - grad : grad) + tone) * s1, 0.0f, 1.0f);
                    }
                    out = core::color::sat_scale(out, saturation);
                    const float a = args.src.at(x, y)[3];
                    std::ranges::transform(out, d, [a](float c) { return c * a; });
                    d[3] = a;
                }
            }
        });

        core::mask::apply(args, core::mask::luma_or_alpha(v.choice(p::mask_use)), v.scalar(p::blur_mask),
                          v.toggle(p::invert_mask));
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_halftonecolor",
            .name = "f_halftonecolor",
            .category = "Functional Stylize",
            .description = "Renders each color channel of the clip as its own offset halftone dot screen.",
            .params = PARAMS,
            .mask_input = "Mask",
            .background_input = "",
            .time_varying = false,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::halftonecolor
