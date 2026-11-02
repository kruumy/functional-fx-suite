#include "effects/ultraglow.hpp"
#include "core/color.hpp"
#include "core/glow.hpp"
#include "core/mask.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace functional_fx_suite::effects::ultraglow
{
    enum class p : std::uint8_t
    {
        primary_group,
        brightness,
        color,
        threshold,
        threshold_add_color,
        glow_width,
        glow_falloff,
        glow_bias,
        width_x,
        width_y,
        width_red,
        width_green,
        width_blue,
        subpixel,
        show,
        primary_group_end,
        after_glow_group,
        after_glow_width,
        after_glow_color,
        after_glow_stretch_x,
        after_glow_stretch_y,
        horizontal_streaks,
        vertical_streaks,
        after_glow_group_end,
        edges_group,
        edge_detect,
        edge_combine,
        edge_smooth,
        edge_mode,
        edge_fill,
        edge_thin,
        edges_group_end,
        atmosphere_group,
        atmosphere,
        atmosphere_amp,
        atmosphere_freq,
        atmosphere_detail,
        atmosphere_speed,
        atmosphere_lights,
        atmosphere_darks,
        atmosphere_seed,
        apply_pre_glow,
        atmosphere_group_end,
        composite_group,
        combine,
        affect_alpha,
        glow_from_alpha,
        glow_under_source,
        light_background,
        source_opacity,
        bg_brightness,
        invert_matte,
        composite_group_end,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    // ids 44-60 are kept for a Highlights group
    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::group(1, "primary_glow", "Primary Glow"),
        param::scalar(2, "brightness", "Brightness").value(1.8).range(0, UNBOUNDED).slider(0, 3.6),
        param::color(3, "color", "Color").value({1, 1, 1}),
        param::scalar(4, "threshold", "Threshold").value(0.4).range(0, UNBOUNDED).slider(0, 1),
        param::color(5, "threshold_add_color", "Threshold Add Color").value({0, 0, 0}),
        param::scalar(6, "glow_width", "Glow Width").value(0.370833).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(7, "glow_falloff", "Glow Falloff").value(0.35).range(-2, 2),
        param::scalar(8, "glow_bias", "Glow Bias").value(0).range(-3, 3),
        param::scalar(9, "width_x", "Width X").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(10, "width_y", "Width Y").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(11, "width_red", "Width Red").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(12, "width_green", "Width Green").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(13, "width_blue", "Width Blue").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::toggle(14, "subpixel", "Subpixel").value(1),
        param::choice(15, "show", "Show", "Result|Threshold"),
        param::group_end(16, "primary_glow_end"),
        param::group(17, "after_glow", "After Glow"),
        param::scalar(18, "after_glow_width", "After Glow Width").value(0.808333).range(0, UNBOUNDED).slider(0, 2),
        param::color(19, "after_glow_color", "After Glow Color").value({1, 1, 1}),
        param::scalar(20, "after_glow_stretch_x", "After Glow Stretch X").value(0.3).range(0, UNBOUNDED).slider(0, 1),
        param::scalar(21, "after_glow_stretch_y", "After Glow Stretch Y").value(0.1).range(0, UNBOUNDED).slider(0, 1),
        param::scalar(22, "horizontal_streaks", "Horizontal Streaks").value(0.25).range(0, UNBOUNDED).slider(0, 1),
        param::scalar(23, "vertical_streaks", "Vertical Streaks").value(0.25).range(0, UNBOUNDED).slider(0, 1),
        param::group_end(24, "after_glow_end"),
        param::group(25, "edges", "Edges"),
        param::toggle(26, "edge_detect", "Edge Detect"),
        param::choice(27, "edge_combine", "Edge Combine", "Screen|Add|Edges Only"),
        param::scalar(28, "edge_smooth", "Edge Smooth").value(0).range(0, UNBOUNDED).slider(0, 1),
        param::choice(29, "edge_mode", "Edge Mode", "Transparent|Reflect").value(1),
        param::toggle(30, "edge_fill", "Edge Fill").value(1),
        param::scalar(31, "edge_thin", "Edge Thin").value(0).range(0, UNBOUNDED).slider(0, 15),
        param::group_end(32, "edges_end"),
        param::group(33, "atmosphere_group", "Atmosphere"),
        param::toggle(34, "atmosphere", "Atmosphere"),
        param::scalar(35, "atmosphere_amp", "Atmosphere Amp").value(1).range(0, UNBOUNDED).slider(0, 5),
        param::scalar(36, "atmosphere_freq", "Atmosphere Freq").value(11.558).range(0.1, 20),
        param::scalar(37, "atmosphere_detail", "Atmosphere Detail").value(0.506).range(0, 1),
        param::scalar(38, "atmosphere_speed", "Atmosphere Speed").value(1).range(-UNBOUNDED, UNBOUNDED).slider(-5, 5),
        param::scalar(39, "atmosphere_lights", "Atmosphere Lights").value(0.5).range(0, UNBOUNDED).slider(0, 1),
        param::scalar(40, "atmosphere_darks", "Atmosphere Darks").value(0).range(0, UNBOUNDED).slider(0, 1),
        param::scalar(41, "atmosphere_seed", "Atmosphere Seed").value(0.123).range(0, UNBOUNDED).slider(0, 1),
        param::toggle(42, "apply_pre_glow", "Apply Pre-Glow"),
        param::group_end(43, "atmosphere_end"),
        param::group(61, "composite", "Composite"),
        param::choice(62, "combine", "Combine", "Mult|Add|Screen|Difference|Overlay").value(2),
        param::scalar(63, "affect_alpha", "Affect Alpha").value(0).range(0, UNBOUNDED).slider(0, 1),
        param::scalar(64, "glow_from_alpha", "Glow From Alpha").value(0).range(0, 1),
        param::scalar(65, "glow_under_source", "Glow Under Source").value(0).range(0, 1),
        param::scalar(66, "light_background", "Light Background").value(0).range(0, 1),
        param::scalar(67, "source_opacity", "Source Opacity").value(1).range(0, 1),
        param::scalar(68, "bg_brightness", "Bg Brightness").value(1).range(0, UNBOUNDED).slider(0, 1),
        param::toggle(69, "invert_matte", "Invert Matte"),
        param::group_end(70, "composite_end"),
    };

    using core::glow::rgb;

    // runs fn(x, y) over every pixel of `area`, rows in parallel
    template <class Fn> static void each_pixel(const core::rect& area, Fn&& fn)
    {
        core::parallel::for_range(area.y0, area.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                for (int x = area.x0; x < area.x1; ++x)
                {
                    fn(x, y);
                }
            }
        });
    }

    static core::buffer copy(const core::image& img)
    {
        core::buffer out(img.bounds, img.channels);
        std::copy_n(img.data, static_cast<std::size_t>(img.bounds.width()) * img.bounds.height() * img.channels,
                    out.view().data);
        return out;
    }

    static float matte_luma(const core::image* matte, int x, int y)
    {
        if (matte == nullptr || !matte->bounds.contains(x, y))
        {
            return 1.0f;
        }
        const float* m = matte->at(x, y);
        return core::color::luma(m[0], m[1], m[2]);
    }

    // edges: gradient magnitude of the pre-blurred source,
    // scaled by the derivative width, minus Edge Thin; combined with the source by Edge Combine
    static void edges(const core::render_args& args, const core::image& s, const core::image* matte)
    {
        const core::param_values& v = *args.params;
        const core::buffer blurred = copy(s);
        const core::image& e = blurred.view();
        const double smooth = core::blur::amount_px(v.scalar(p::edge_smooth), args.geometry);
        core::blur::gaussian(e, smooth, smooth);
        constexpr double DERIVATIVE_OFFSET = 0.1;
        const auto scale =
            static_cast<float>(core::blur::amount_px(v.scalar(p::edge_smooth) + DERIVATIVE_OFFSET, args.geometry));
        const auto thin = v.scalar_f(p::edge_thin);
        const bool fill = v.toggle(p::edge_fill);
        const int mode = v.choice(p::edge_combine);
        const core::rect& b = e.bounds;
        each_pixel(b, [&](int x, int y) {
            const auto at = [&](int px, int py, int c) {
                return e.at(std::clamp(px, b.x0, b.x1 - 1), std::clamp(py, b.y0, b.y1 - 1))[c];
            };
            float* d = s.at(x, y);
            std::array<float, 4> edge{};
            for (int c = 0; c < 3; ++c)
            {
                const float gx = (at(x + 1, y, c) - at(x - 1, y, c)) * 0.5f;
                const float gy = (at(x, y + 1, c) - at(x, y - 1, c)) * 0.5f;
                edge[c] = std::clamp(std::hypot(gx, gy) * scale - thin, 0.0f, 1.0f);
            }
            edge[3] = fill ? d[3] : core::color::luma(edge[0], edge[1], edge[2]);
            const float m = matte_luma(matte, x, y);
            for (int c = 0; c < 4; ++c)
            {
                const float edged = core::color::lerp(d[c], edge[c], m);
                d[c] = mode >= 2 ? edged : mode == 1 ? edged + d[c] : core::color::screen_super(edged, d[c]);
            }
        });
    }

    // layer blends: the base b moved toward its blend with f by a
    static float layer(core::glow::combine mode, float a, float b, float f)
    {
        float blended = 0.0f;
        switch (mode)
        {
        case core::glow::combine::mult:
            blended = b * f;
            break;
        case core::glow::combine::add:
            blended = std::min(1.0f, b + f);
            break;
        case core::glow::combine::screen:
            blended = core::color::screen_super(b, f);
            break;
        case core::glow::combine::difference:
            blended = std::abs(b - f);
            break;
        case core::glow::combine::overlay:
            blended = core::color::overlay_super(f, b);
            break;
        }
        return core::color::lerp(b, blended, a);
    }

    // Apply Pre-Glow: the atmosphere layered onto the source, weighted by the source's brightness
    static void pre_glow_atmosphere(const core::render_args& args, const core::image& s,
                                    const core::glow::atmosphere& atm)
    {
        const core::param_values& v = *args.params;
        const auto lights = v.scalar_f(p::atmosphere_lights);
        const auto darks = v.scalar_f(p::atmosphere_darks);
        const auto mode = static_cast<core::glow::combine>(std::clamp(v.choice(p::combine), 0, 4));
        each_pixel(s.bounds, [&](int x, int y) {
            const float tex = atm.at(args.geometry.to_nx(x + 0.5), args.geometry.to_ny(y + 0.5));
            if (tex <= 0.0f)
            {
                return;
            }
            float* d = s.at(x, y);
            const float l = core::color::luma(std::min(d[0], 1.0f), std::min(d[1], 1.0f), std::min(d[2], 1.0f));
            const float a = static_cast<float>(atm.amp) * l;
            const float f = std::clamp(l > 0.0f ? tex / l : 1.0f, 0.0f, 1.0f) * (lights - darks) + darks;
            for (int c = 0; c < 3; ++c)
            {
                d[c] = layer(mode, a, d[c], f);
            }
            d[3] = a + d[3] - a * d[3];
        });
    }

    // Glow Bias: a small reflected gaussian with gain, around a pivot near black (bias > 0, hot areas spread) or
    // near white (bias < 0, only the hottest cores stay); unclamped
    static void bias(const core::render_args& args, const core::image& g)
    {
        const double b = args.params->scalar(p::glow_bias) / 120.0;
        if (b == 0.0)
        {
            return;
        }
        constexpr double MAX_GAIN = 300.0;
        constexpr double GAIN_PER_BIAS = 200.0;
        constexpr double PIVOT = 0.02;
        const double gain = std::min(1.0 + GAIN_PER_BIAS * std::abs(b), MAX_GAIN);
        const auto pivot = static_cast<float>(b > 0.0 ? PIVOT : 1.0 - PIVOT);
        const auto k = static_cast<float>(gain);
        const double px = core::blur::amount_px(std::abs(b), args.geometry);
        core::buffer plane(g.bounds, 1);
        const core::image& pl = plane.view();
        for (int c = 0; c < 3; ++c)
        {
            each_pixel(g.bounds, [&](int x, int y) { *pl.at(x, y) = g.at(x, y)[c]; });
            core::blur::gaussian(pl, px, px, core::blur::edge::reflect);
            each_pixel(g.bounds, [&](int x, int y) { g.at(x, y)[c] = (*pl.at(x, y) - pivot) * k + pivot; });
        }
    }

    struct pass
    {
        double amount; // scales width and brightness; 0 skips the pass
        double width;  // frame units, per amount
        double width_x;
        double width_y;
        double brightness; // per amount
        rgb color;
    };

    // the start of one glow: the matted, thresholded copy of `from`
    static core::buffer thresholded(const core::render_args& args, const core::image& from, const core::image* matte)
    {
        const core::param_values& v = *args.params;
        core::buffer out = copy(from);
        const core::image& g = out.view();
        if (matte != nullptr)
        {
            each_pixel(g.bounds, [&](int x, int y) {
                float* d = g.at(x, y);
                const float* m = matte->bounds.contains(x, y) ? matte->at(x, y) : nullptr;
                for (int c = 0; c < 3; ++c)
                {
                    d[c] *= m != nullptr ? m[c] : 1.0f;
                }
            });
        }
        const auto& add = v.vec(p::threshold_add_color);
        const double t = v.scalar(p::threshold);
        core::glow::threshold(g, {t + add[0], t + add[1], t + add[2]});
        return out;
    }

    // the rest of glow(): a gaussian per channel; Glow Falloff narrows the primary one
    static void blur_pass(const core::render_args& args, const core::image& g, const pass& ps, const rgb& channel,
                          double falloff)
    {
        const auto& v = *args.params;
        rgb ax{};
        rgb ay{};
        rgb gain{};
        for (int c = 0; c < 3; ++c)
        {
            const double px = core::blur::amount_px(ps.width * ps.amount * channel[c], args.geometry) / falloff;
            ax[c] = px * ps.width_x;
            ay[c] = px * ps.width_y;
            gain[c] = ps.brightness * ps.amount * ps.color[c];
        }
        const auto edge = v.choice(p::edge_mode) == 1 ? core::blur::edge::reflect : core::blur::edge::transparent;
        core::glow::blur_channels(g, ax, ay, gain, edge);
    }

    // rgb times the atmosphere texture, optionally remapped to lights/darks
    static void texture(const core::render_args& args, const core::image& g, const core::glow::atmosphere& atm,
                        float lights, float darks)
    {
        each_pixel(g.bounds, [&](int x, int y) {
            const float tex = atm.at(args.geometry.to_nx(x + 0.5), args.geometry.to_ny(y + 0.5));
            const float k = (lights - darks) * (tex + darks);
            float* d = g.at(x, y);
            std::for_each(d, d + 3, [k](float& c) { c *= k; });
        });
    }

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const core::buffer matte = core::mask::build(args, core::mask::source::color, 0.0, v.toggle(p::invert_matte));
        const core::image* matte_view = matte.view().data != nullptr ? &matte.view() : nullptr;
        const core::buffer source = core::glow::source(args, nullptr, v.scalar(p::glow_from_alpha));
        const core::image& s = source.view();
        if (v.toggle(p::edge_detect))
        {
            edges(args, s, matte_view);
        }
        const bool atmosphere = v.toggle(p::atmosphere);
        const bool pre_glow = v.toggle(p::apply_pre_glow);
        const auto atm = core::glow::make_atmosphere(v.scalar(p::atmosphere_amp), v.scalar(p::atmosphere_freq),
                                                     v.scalar(p::atmosphere_detail), v.scalar(p::atmosphere_seed),
                                                     v.scalar(p::atmosphere_speed), args.time);
        if (atmosphere && pre_glow)
        {
            pre_glow_atmosphere(args, s, atm);
        }

        const bool show = v.choice(p::show) == 1;
        const double brightness = v.scalar(p::brightness);
        const rgb channel{v.scalar(p::width_red), v.scalar(p::width_green), v.scalar(p::width_blue)};
        const auto& color = v.vec(p::color);
        const pass primary{1.0,        v.scalar(p::glow_width),       v.scalar(p::width_x), v.scalar(p::width_y),
                           brightness, {color[0], color[1], color[2]}};
        core::buffer glow = thresholded(args, s, matte_view);
        bias(args, glow.view());
        if (!show)
        {
            blur_pass(args, glow.view(), primary, channel, std::pow(10.0, v.scalar(p::glow_falloff)));
        }
        const bool post_texture = atmosphere && !pre_glow;
        if (post_texture)
        {
            texture(args, glow.view(), atm, v.scalar_f(p::atmosphere_lights), v.scalar_f(p::atmosphere_darks));
        }

        // after glows: medium and huge soft glows, then vertical and horizontal streaks, each over the last result
        const double aw = v.scalar(p::after_glow_width);
        const double sx = v.scalar(p::after_glow_stretch_x);
        const double sy = v.scalar(p::after_glow_stretch_y);
        const double half = brightness / 2.0;
        const auto& ac = v.vec(p::after_glow_color);
        const rgb after_color{ac[0], ac[1], ac[2]};
        const std::array<pass, 4> after{{
            {aw, 0.69, 1.0 + sx, 1.0 + sy, half * (1.4 + sx + sy), after_color},
            {0.5 * aw * aw, 3.0, 1.0 + 2.0 * sx, 1.0 + 2.0 * sy, half * (2.4 + 2.0 * sx + 2.0 * sy), after_color},
            {v.scalar(p::vertical_streaks), 0.44, 0.089, 3.182, half * 0.714, after_color},
            {v.scalar(p::horizontal_streaks), 0.44, 3.058, 0.09, half * 0.714, after_color},
        }};
        constexpr rgb SAME_WIDTHS{1.0, 1.0, 1.0};
        for (const pass& ps : after)
        {
            if (ps.amount == 0.0)
            {
                continue;
            }
            core::buffer stage = thresholded(args, glow.view(), matte_view);
            if (!show)
            {
                blur_pass(args, stage.view(), ps, SAME_WIDTHS, 1.0);
            }
            if (post_texture)
            {
                texture(args, stage.view(), atm, 1.0f, 0.0f);
            }
            if (!show)
            {
                // screened onto the glow so far
                const core::image& g = glow.view();
                each_pixel(g.bounds, [&](int x, int y) {
                    float* d = g.at(x, y);
                    const float* n = stage.view().at(x, y);
                    for (int c = 0; c < 3; ++c)
                    {
                        d[c] = core::color::screen_super(n[c], d[c]);
                    }
                });
            }
            else
            {
                glow = std::move(stage);
            }
        }

        const core::glow::composite_settings settings{
            .affect_alpha = v.scalar(p::affect_alpha),
            .mode = static_cast<core::glow::combine>(std::clamp(v.choice(p::combine), 0, 4)),
            .glow_under_source = v.scalar(p::glow_under_source),
            .light_bg = v.scalar(p::light_background),
            .scale_background = v.scalar(p::bg_brightness),
            .scale_source = v.scalar(p::source_opacity),
            .show = show,
            .mult_overlay = true,
        };
        core::glow::composite(args, s, glow.view(), glow.view(), settings);
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_ultraglow",
            .name = "f_ultraglow",
            .category = "Functional Lighting",
            .description = "Glows light from the bright areas of the clip, with a shaped falloff and wide after glows.",
            .params = PARAMS,
            .mask_input = "Matte",
            .background_input = "Background",
            .time_varying = true,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::ultraglow
