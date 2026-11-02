#include "effects/glowdark.hpp"
#include "core/color.hpp"
#include "core/glow.hpp"
#include "core/mask.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <array>

namespace functional_fx_suite::effects::glowdark
{
    enum class p : std::uint8_t
    {
        darkness,
        threshold,
        glow_saturation,
        glow_width,
        width_x,
        width_y,
        subpixel,
        glow_from_alpha,
        glow_under_source,
        source_opacity,
        bg_brightness,
        show,
        invert_matte,
        atmosphere_group,
        atmosphere_amp,
        atmosphere_freq,
        atmosphere_detail,
        atmosphere_seed,
        atmosphere_speed,
        atmosphere_group_end,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "darkness", "Darkness").value(0.5).range(0, UNBOUNDED).slider(0, 1),
        param::scalar(2, "threshold", "Threshold").value(0.5).range(0, UNBOUNDED).slider(0, 1),
        param::scalar(3, "glow_saturation", "Glow Saturation").value(1).range(-2, 8).slider(0, 2),
        param::scalar(4, "glow_width", "Glow Width").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(5, "width_x", "Width X").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(6, "width_y", "Width Y").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::toggle(7, "subpixel", "Subpixel").value(1),
        param::scalar(8, "glow_from_alpha", "Glow From Alpha").value(0).range(0, 1),
        param::scalar(9, "glow_under_source", "Glow Under Source").value(0).range(0, 1),
        param::scalar(10, "source_opacity", "Source Opacity").value(1).range(0, 1),
        param::scalar(11, "bg_brightness", "Bg Brightness").value(1).range(0, UNBOUNDED).slider(0, 1),
        param::choice(12, "show", "Show", "Result|Threshold"),
        param::toggle(13, "invert_matte", "Invert Matte"),
        param::group(14, "atmosphere", "Atmosphere"),
        param::scalar(15, "atmosphere_amp", "Atmosphere Amp").value(0).range(0, UNBOUNDED).slider(0, 5),
        param::scalar(16, "atmosphere_freq", "Atmosphere Freq").value(1).range(0.1, 20),
        param::scalar(17, "atmosphere_detail", "Atmosphere Detail").value(0.6).range(0, 1),
        param::scalar(18, "atmosphere_seed", "Atmosphere Seed").value(0.123).range(0, UNBOUNDED).slider(0, 1),
        param::scalar(19, "atmosphere_speed", "Atmosphere Speed").value(1).range(-UNBOUNDED, UNBOUNDED).slider(-5, 5),
        param::group_end(20, "atmosphere_end"),
    };

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const core::buffer matte = core::mask::build(args, core::mask::source::color, 0.0, v.toggle(p::invert_matte));
        const core::image* m = matte.view().data != nullptr ? &matte.view() : nullptr;
        const core::buffer s = core::glow::source(args, nullptr, v.scalar(p::glow_from_alpha));
        const core::rect& b = s.view().bounds;
        const auto th = v.scalar_f(p::threshold);

        // how far below the threshold each channel is, 0..1; the matte lifts what it excludes out of the darks
        core::buffer blacks(b, 4);
        core::parallel::for_range(b.y0, b.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                for (int x = b.x0; x < b.x1; ++x)
                {
                    const float* c = s.view().at(x, y);
                    float* d = blacks.view().at(x, y);
                    for (int k = 0; k < 3; ++k)
                    {
                        const float keep = m != nullptr && m->bounds.contains(x, y) ? m->at(x, y)[k] : 1.0f;
                        const float lifted = core::color::screen_super(c[k], 1.0f - keep);
                        d[k] = th > 0.0f ? std::max(0.0f, th - lifted) / th : 0.0f;
                    }
                }
            }
        });
        const bool show = v.choice(p::show) == 1;

        // the darks spread by a gaussian, scaled by Darkness, optionally broken up by the atmosphere
        core::buffer spread(b, 4);
        std::copy_n(blacks.view().data, static_cast<std::size_t>(b.width()) * b.height() * 4, spread.view().data);
        const double px = core::blur::amount_px(v.scalar(p::glow_width), args.geometry);
        core::blur::gaussian(spread.view(), px * v.scalar(p::width_x), px * v.scalar(p::width_y));
        const float gain = 2.0f * v.scalar_f(p::darkness);
        const auto saturation = v.scalar_f(p::glow_saturation);
        const auto atmosphere = core::glow::make_atmosphere(
            v.scalar(p::atmosphere_amp), v.scalar(p::atmosphere_freq), v.scalar(p::atmosphere_detail),
            v.scalar(p::atmosphere_seed), v.scalar(p::atmosphere_speed), args.time);
        const auto source_opacity = v.scalar_f(p::source_opacity);
        const auto bg_brightness = v.scalar_f(p::bg_brightness);
        const auto under = v.scalar_f(p::glow_under_source);
        const core::image* bg = args.background;
        const core::image& dst = args.dst;
        core::parallel::for_range(dst.bounds.y0, dst.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                for (int x = dst.bounds.x0; x < dst.bounds.x1; ++x)
                {
                    float* d = dst.at(x, y);
                    if (!b.contains(x, y))
                    {
                        std::fill_n(d, 4, 0.0f);
                        continue;
                    }
                    if (show)
                    {
                        std::copy_n(blacks.view().at(x, y), 3, d);
                        d[3] = 1.0f;
                        continue;
                    }
                    const float* sp = spread.view().at(x, y);
                    core::color::rgb dark =
                        core::color::sat_scale({sp[0] * gain, sp[1] * gain, sp[2] * gain}, saturation);
                    if (atmosphere.amp != 0.0)
                    {
                        const float k = atmosphere.at(args.geometry.to_nx(x + 0.5), args.geometry.to_ny(y + 0.5));
                        std::ranges::for_each(dark, [&](float& c) { c = std::clamp(k * c, 0.0f, th * gain); });
                    }
                    const float* src = args.src.at(x, y);
                    std::array<float, 4> back{};
                    if (bg != nullptr && bg->bounds.contains(x, y))
                    {
                        const float* bp = bg->at(x, y);
                        back = {bp[0] * bg_brightness, bp[1] * bg_brightness, bp[2] * bg_brightness, bp[3]};
                    }
                    const float a = src[3] * source_opacity;
                    for (int k = 0; k < 3; ++k)
                    {
                        const float front = src[k] * source_opacity;
                        const float over = front + back[k] * (1.0f - a);
                        d[k] = over * (1.0f - dark[k]) + under * front * dark[k];
                    }
                    d[3] = a + back[3] * (1.0f - a);
                }
            }
        });
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_glowdark",
            .name = "f_glowdark",
            .category = "Functional Lighting",
            .description = "Spreads the dark areas of the clip over their surroundings for a deep, smoky look.",
            .params = PARAMS,
            .mask_input = "Matte",
            .background_input = "Background",
            .time_varying = true,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::glowdark
