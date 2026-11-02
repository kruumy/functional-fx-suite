#include "effects/glowaura.hpp"
#include "core/color.hpp"
#include "core/glow.hpp"
#include "core/mask.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace functional_fx_suite::effects::glowaura
{
    enum class p : std::uint8_t
    {
        brightness,
        color,
        outer_brightness,
        glow_saturation,
        threshold,
        threshold_add_color,
        glow_width,
        subpixel,
        frequency,
        frequency_red,
        frequency_green,
        frequency_blue,
        phase,
        phase_speed,
        phase_red,
        phase_green,
        phase_blue,
        twist,
        show,
        combine,
        affect_alpha,
        glow_from_alpha,
        glow_under_source,
        light_background,
        source_opacity,
        bg_brightness,
        invert_matte,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "brightness", "Brightness").value(0.8).range(0, UNBOUNDED).slider(0, 2),
        param::color(2, "color", "Color").value({1, 1, 1}),
        param::scalar(3, "outer_brightness", "Outer Brightness").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(4, "glow_saturation", "Glow Saturation").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(5, "threshold", "Threshold").value(0).range(0, UNBOUNDED).slider(0, 1),
        param::color(6, "threshold_add_color", "Threshold Add Color").value({0, 0, 0}),
        param::scalar(7, "glow_width", "Glow Width").value(0.4).range(0, UNBOUNDED).slider(0, 2),
        param::toggle(8, "subpixel", "Subpixel").value(1),
        param::scalar(9, "frequency", "Frequency").value(8).range(0, UNBOUNDED).slider(0.1, 50),
        param::scalar(10, "frequency_red", "Frequency Red").value(1).range(0, UNBOUNDED).slider(0.1, 5),
        param::scalar(11, "frequency_green", "Frequency Green").value(1).range(0, UNBOUNDED).slider(0.1, 5),
        param::scalar(12, "frequency_blue", "Frequency Blue").value(1).range(0, UNBOUNDED).slider(0.1, 5),
        param::scalar(13, "phase", "Phase").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(14, "phase_speed", "Phase Speed").value(1).range(-UNBOUNDED, UNBOUNDED).slider(-10, 10),
        param::scalar(15, "phase_red", "Phase Red").value(0.2).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(16, "phase_green", "Phase Green").value(0.1).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(17, "phase_blue", "Phase Blue").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(18, "twist", "Twist").value(1).range(-UNBOUNDED, UNBOUNDED).slider(-10, 10),
        param::choice(19, "show", "Show", "Result|Threshold"),
        param::choice(20, "combine", "Combine", "Mult|Add|Screen|Difference|Overlay").value(2),
        param::scalar(21, "affect_alpha", "Affect Alpha").value(0).range(0, UNBOUNDED).slider(0, 1),
        param::scalar(22, "glow_from_alpha", "Glow From Alpha").value(0).range(0, 1),
        param::scalar(23, "glow_under_source", "Glow Under Source").value(0).range(0, 1),
        param::scalar(24, "light_background", "Light Background").value(0).range(0, 1),
        param::scalar(25, "source_opacity", "Source Opacity").value(1).range(0, 1),
        param::scalar(26, "bg_brightness", "Bg Brightness").value(1).range(0, UNBOUNDED).slider(0, 1),
        param::toggle(27, "invert_matte", "Invert Matte"),
    };

    // the gradient angle in turns, [0, 1)
    static double direction(const core::image& pic, int x, int y)
    {
        const core::rect& b = pic.bounds;
        const auto at = [&](int px, int py) {
            return static_cast<double>(*pic.at(std::clamp(px, b.x0, b.x1 - 1), std::clamp(py, b.y0, b.y1 - 1)));
        };
        const double gx = at(x + 1, y) - at(x - 1, y);
        const double gy = at(x, y - 1) - at(x, y + 1);
        const double turns = std::atan2(gy, gx) / (2.0 * std::numbers::pi);
        return turns - std::floor(turns);
    }

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const core::buffer matte = core::mask::build(args, core::mask::source::color, 0.0, v.toggle(p::invert_matte));
        const core::image* matte_view = matte.view().data != nullptr ? &matte.view() : nullptr;
        const core::buffer s = core::glow::source(args, matte_view, v.scalar(p::glow_from_alpha));
        const core::rect& b = s.view().bounds;
        const auto& add = v.vec(p::threshold_add_color);
        const double t = v.scalar(p::threshold);
        const core::glow::rgb th{t + add[0], t + add[1], t + add[2]};
        constexpr double MIN_HEADROOM = 0.01;
        constexpr double PIC_GAIN = 4.0;
        const auto scale_pic = static_cast<float>(PIC_GAIN / std::max(MIN_HEADROOM, 1.0 - std::ranges::min(th)));

        // mono excess over the threshold: shown by Show Threshold, blurred into the aura's shape
        core::buffer shown(b, 4);
        core::buffer pic(b, 1);
        core::parallel::for_range(b.y0, b.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                for (int x = b.x0; x < b.x1; ++x)
                {
                    const float* c = s.view().at(x, y);
                    const float bw = std::max(0.0f, core::color::luma(c[0] - static_cast<float>(th[0]),
                                                                      c[1] - static_cast<float>(th[1]),
                                                                      c[2] - static_cast<float>(th[2])));
                    *pic.view().at(x, y) = bw;
                    std::fill_n(shown.view().at(x, y), 3, bw * scale_pic);
                }
            }
        });
        const double width = v.scalar(p::glow_width);
        const double px = core::blur::amount_px(width, args.geometry);
        core::blur::gaussian(pic.view(), px, px);

        const double twist = width * v.scalar(p::twist) / 4.0;
        const double phase = v.scalar(p::phase) + v.scalar(p::phase_speed) * args.time;
        const std::array phases{phase + v.scalar(p::phase_red), phase + v.scalar(p::phase_green),
                                phase + v.scalar(p::phase_blue)};
        const double f = v.scalar(p::frequency);
        const std::array freqs{f * v.scalar(p::frequency_red), f * v.scalar(p::frequency_green),
                               f * v.scalar(p::frequency_blue)};
        const double outer = PIC_GAIN * v.scalar(p::outer_brightness) * width;
        const auto saturation = v.scalar_f(p::glow_saturation);
        const auto& color = v.vec(p::color);
        const double brightness = v.scalar(p::brightness);
        core::buffer aura(b, 4);
        core::parallel::for_range(b.y0, b.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                for (int x = b.x0; x < b.x1; ++x)
                {
                    const double level = *pic.view().at(x, y) * scale_pic;
                    // bands follow the gradient direction, twisted by the glow's own level
                    const double angle = direction(pic.view(), x, y) + level * twist;
                    core::color::rgb band{};
                    for (int c = 0; c < 3; ++c)
                    {
                        const double turns = angle * freqs[c] - phases[c];
                        band[c] = static_cast<float>(0.5 + 0.5 * std::sin(2.0 * std::numbers::pi * turns));
                    }
                    band = core::color::sat_scale(band, saturation);
                    const double fade = std::min(1.0, level * outer);
                    float* d = aura.view().at(x, y);
                    for (int c = 0; c < 3; ++c)
                    {
                        d[c] = static_cast<float>(band[c] * fade * brightness * color[c]);
                    }
                }
            }
        });

        const core::glow::composite_settings settings{
            .affect_alpha = v.scalar(p::affect_alpha),
            .mode = static_cast<core::glow::combine>(std::clamp(v.choice(p::combine), 0, 4)),
            .glow_under_source = v.scalar(p::glow_under_source),
            .light_bg = v.scalar(p::light_background),
            .scale_background = v.scalar(p::bg_brightness),
            .scale_source = v.scalar(p::source_opacity),
            .show = v.choice(p::show) == 1,
        };
        core::glow::composite(args, args.src, aura.view(), shown.view(), settings);
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_glowaura",
            .name = "f_glowaura",
            .category = "Functional Lighting",
            .description = "Glows colored aura lines that follow the shapes of the bright areas of the clip.",
            .params = PARAMS,
            .mask_input = "Matte",
            .background_input = "Background",
            .time_varying = true,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::glowaura
