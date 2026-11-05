#include "effects/lensflare.hpp"
#include "core/blur.hpp"
#include "core/buffer.hpp"
#include "core/color.hpp"
#include "core/edges.hpp"
#include "core/flare.hpp"
#include "core/glow.hpp"
#include "core/mask.hpp"
#include "core/noise.hpp"
#include "core/parallel.hpp"
#include "core/sample.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace functional_fx_suite::effects::lensflare
{
    enum class p : std::uint8_t
    {
        lens,
        scale_widths,
        rel_heights,
        rays_rotate,
        hotspot,
        pivot,
        brightness,
        color,
        gamma,
        saturation,
        hue_shift,
        hotspot_bright,
        hotspot_color,
        rays_brightness,
        rays_num_scale,
        rays_length,
        rays_thickness,
        other_brightness,
        other_color,
        other_width,
        atmosphere,
        atmosphere_amp,
        atmosphere_freq,
        atmosphere_detail,
        atmosphere_seed,
        atmosphere_speed,
        atmosphere_end,
        flicker,
        flicker_amp,
        flicker_speed,
        flicker_randomness,
        flicker_end,
        blur_flare,
        bg_brightness,
        combine,
        tint_bg_whites,
        affect_alpha,
        edge_triggers,
        edge_width,
        edge_falloff,
        shift_out,
        one_way,
        edge_scale_brightness,
        edge_scale_widths,
        show_edge_zones,
        edge_triggers_end,
        center_triggers,
        center_radius,
        center_falloff,
        center_scale_brightness,
        center_scale_widths,
        show_center_zone,
        center_triggers_end,
        occlusion,
        occlusion_softness,
        occlusion_from,
        invert_occlusion,
        use_color,
        diffraction_glow,
        glow_width,
        glow_color,
        glow_radius,
        occlusion_end,
        occlusion_triggers,
        occlusion_scale_brightness,
        occlusion_scale_widths,
        occlusion_triggers_end,
        performance,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    // Lens picks one of the built-in lenses
    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::choice(1, "lens", "Lens", "Basic Glow|Prime|Zoom|Anamorphic|Sun|Hex Ghosts").value(1),
        param::scalar(2, "scale_widths", "Scale Widths").value(1.155).range(0, UNBOUNDED).slider(0, 5),
        param::scalar(3, "rel_heights", "Rel Heights").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0.1, 10),
        param::scalar(4, "rays_rotate", "Rays Rotate").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-180, 180),
        param::point(5, "hotspot", "Hotspot").value({-0.2222, 0, 0}),
        param::point(6, "pivot", "Pivot").value({0, 0, 0}),
        param::scalar(7, "brightness", "Brightness").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::color(8, "color", "Color").value({1, 1, 1}),
        param::scalar(9, "gamma", "Gamma").value(1).range(0.1, UNBOUNDED).slider(1, 3),
        param::scalar(10, "saturation", "Saturation").value(1).range(-2, 8).slider(0, 2),
        param::scalar(11, "hue_shift", "Hue Shift").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(12, "hotspot_bright", "Hotspot Bright").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::color(13, "hotspot_color", "Hotspot Color").value({1, 1, 1}),
        param::scalar(14, "rays_brightness", "Rays Brightness").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(15, "rays_num_scale", "Rays Num Scale").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(16, "rays_length", "Rays Length").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(17, "rays_thickness", "Rays Thickness").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(18, "other_brightness", "Other Brightness").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::color(19, "other_color", "Other Color").value({1, 1, 1}),
        param::scalar(20, "other_width", "Other Width").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::group(21, "atmosphere", "Atmosphere"),
        param::scalar(22, "atmosphere_amp", "Atmosphere Amp").value(0).range(0, UNBOUNDED).slider(0, 5),
        param::scalar(23, "atmosphere_freq", "Atmosphere Freq").value(1).range(0.1, 20),
        param::scalar(24, "atmosphere_detail", "Atmosphere Detail").value(0.6).range(0, 1),
        param::scalar(25, "atmosphere_seed", "Atmosphere Seed").value(0.123).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(26, "atmosphere_speed", "Atmosphere Speed").value(1).range(-UNBOUNDED, UNBOUNDED).slider(-5, 5),
        param::group_end(27, "atmosphere_end"),
        param::group(28, "flicker", "Flicker"),
        param::scalar(29, "flicker_amp", "Flicker Amp").value(0).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(30, "flicker_speed", "Flicker Speed").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 5),
        param::scalar(31, "flicker_randomness", "Flicker Randomness").value(0.6).range(-UNBOUNDED, 1).slider(0, 1),
        param::group_end(32, "flicker_end"),
        param::scalar(33, "blur_flare", "Blur Flare").value(0).range(0, UNBOUNDED).slider(0, 0.8),
        param::scalar(34, "bg_brightness", "Bg Brightness").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::choice(35, "combine", "Combine", "Screen|Add|Flare Only"),
        param::toggle(36, "tint_bg_whites", "Tint Bg Whites"),
        // 1 by default: on straight-alpha hosts such as AE, a flare
        // without alpha is dropped when the output is un-premultiplied
        param::scalar(37, "affect_alpha", "Affect Alpha").value(1).range(0, 1),
        param::group(38, "edge_triggers", "Edge Triggers"),
        param::scalar(39, "edge_width", "Edge Width").value(0.3).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(40, "edge_falloff", "Edge Falloff").value(2).range(0.01, UNBOUNDED).slider(0.01, 4),
        param::scalar(41, "shift_out", "Shift Out").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::toggle(42, "one_way", "One Way"),
        param::scalar(43, "edge_scale_brightness", "Edge Scale Brightness")
            .value(1.5)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 3),
        param::scalar(44, "edge_scale_widths", "Edge Scale Widths")
            .value(1.2)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 2.4),
        param::toggle(45, "show_edge_zones", "Show Edge Zones"),
        param::group_end(46, "edge_triggers_end"),
        param::group(47, "center_triggers", "Center Triggers"),
        param::scalar(48, "center_radius", "Center Radius").value(0.3).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(49, "center_falloff", "Center Falloff").value(1).range(0.01, UNBOUNDED).slider(0.01, 2),
        param::scalar(50, "center_scale_brightness", "Center Scale Brightness")
            .value(1.5)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 3),
        param::scalar(51, "center_scale_widths", "Center Scale Widths")
            .value(1.2)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 2.4),
        param::toggle(52, "show_center_zone", "Show Center Zone"),
        param::group_end(53, "center_triggers_end"),
        param::group(54, "occlusion", "Occlusion"),
        param::scalar(55, "occlusion_softness", "Occlusion Softness").value(0.0224).range(0, 1).slider(0, 0.5),
        param::choice(56, "occlusion_from", "Occlusion From", "None|Luma|Alpha").value(2),
        param::toggle(57, "invert_occlusion", "Invert Occlusion"),
        param::toggle(58, "use_color", "Use Color").value(1),
        param::scalar(59, "diffraction_glow", "Diffraction Glow").value(0).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(60, "glow_width", "Glow Width").value(0.4).range(0, UNBOUNDED).slider(0, 0.5),
        param::color(61, "glow_color", "Glow Color").value({1, 1, 1}),
        param::scalar(62, "glow_radius", "Glow Radius").value(0.2).range(0, UNBOUNDED).slider(0, 0.5),
        param::group_end(63, "occlusion_end"),
        param::group(64, "occlusion_triggers", "Occlusion Triggers"),
        param::scalar(65, "occlusion_scale_brightness", "Occlusion Scale Brightness")
            .value(1)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 2),
        param::scalar(66, "occlusion_scale_widths", "Occlusion Scale Widths")
            .value(1)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 2),
        param::group_end(67, "occlusion_triggers_end"),
        param::choice(68, "performance", "Performance", "Full Flare|Priority Only"),
    };

    // one flare unit is value x frame width / 5.5 pixels
    static constexpr double FLARE_UNITS_PER_WIDTH = 5.5;

    // 1 at distance 0, fading to 0 at `width` with power `falloff`; 0 when width is 0
    static double zone(double dist, double width, double falloff)
    {
        return width == 0.0 ? 0.0 : std::pow(std::max(0.0, 1.0 - (dist / width)), falloff);
    }

    // 1 minus the occluder's mean over a disc of `radius` px around the hotspot, weighted (1 - d/radius)^2
    static double visibility(const core::image& m, double hx, double hy, double radius)
    {
        const auto at = [&](double x, double y) {
            const float* p = core::sample::nearest(m, x, y);
            return p != nullptr ? static_cast<double>(*p) : 0.0;
        };
        if (radius < 1.0)
        {
            return 1.0 - at(hx, hy);
        }
        const int r = static_cast<int>(std::ceil(radius));
        const double cx = std::floor(hx) + 0.5;
        const double cy = std::floor(hy) + 0.5;
        double sum = 0.0;
        double total = 0.0;
        for (int dy = -r; dy <= r; ++dy)
        {
            for (int dx = -r; dx <= r; ++dx)
            {
                const double w = std::max(0.0, 1.0 - (std::hypot(cx + dx - hx, cy + dy - hy) / radius));
                sum += w * w * at(cx + dx, cy + dy);
                total += w * w;
            }
        }
        return 1.0 - (sum / total);
    }

    // smooth noise over time, sharpened by Randomness
    static double flicker(double amp, double speed, double randomness, double time)
    {
        if (amp == 0.0)
        {
            return 1.0;
        }
        static constexpr double SMOOTHNESS = 0.3;
        const double n =
            core::noise::fbm_3d(core::noise::seed_bits(0.234), 0.0, 0.0, time * speed, 4, 1.0 - SMOOTHNESS);
        const double t = (2.0 * n) - 1.0;
        const double shaped = std::copysign(std::pow(std::abs(t), std::max(0.05, 1.0 - randomness)), t);
        return std::max(0.0, 1.0 + (amp * shaped));
    }

    // Diffraction Glow: the occluder's edges near the hotspot, blurred, tinted and added to the flare
    static void add_diffraction(const core::image& flare, core::buffer occluder, const core::param_values& v,
                                const core::frame& f, double hx, double hy, const core::color::rgb& tint)
    {
        core::edges::prepare(occluder.view(), 0.0, 1.0, f);
        const core::buffer edges = core::edges::magnitude(occluder.view());
        const core::image& e = edges.view();
        const double rx =
            std::max(core::blur::amount_px(v.scalar(p::glow_radius), f) * v.scalar(p::scale_widths), 1e-6);
        const double ry = std::max(std::abs(rx * v.scalar(p::rel_heights)), 1e-6);
        core::parallel::for_pixels(e.bounds, [&](int x, int y) {
            const double near = std::max(0.0, 1.0 - std::hypot((x + 0.5 - hx) / rx, (y + 0.5 - hy) / ry));
            *e.at(x, y) *= static_cast<float>(near * near);
        });
        // blurring the one channel before tinting matches blurring the tinted rgb, at a third of the work
        const double width = core::blur::amount_px(v.scalar(p::glow_width), f);
        core::blur::gaussian(e, width, width, core::blur::edge::reflect);
        core::parallel::for_pixels(flare.bounds, [&](int x, int y) {
            if (e.bounds.contains(x, y))
            {
                float* o = flare.at(x, y);
                for (std::size_t c = 0; c < 3; ++c)
                {
                    o[c] += tint[c] * *e.at(x, y);
                }
            }
        });
    }

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const core::frame& f = args.geometry;
        const double hx = f.to_px_x(v.vec(p::hotspot)[0]);
        const double hy = f.to_px_y(v.vec(p::hotspot)[1]);
        const double px = f.to_px_x(v.vec(p::pivot)[0]);
        const double py = f.to_px_y(v.vec(p::pivot)[1]);

        // triggers work in -1..1 across the frame on both axes, y up, with zones at the frame edge and the pivot
        const double aspect = f.width / args.src.bounds.height();
        const auto trigger_at = [&](double x, double y) {
            return std::array{2.0 * f.to_nx(x), 2.0 * f.to_ny(y) * aspect};
        };
        const std::array<double, 2> pivot_t = trigger_at(px, py);
        const double shift_out = v.scalar(p::shift_out);
        const bool one_way = v.toggle(p::one_way);
        const double edge_width = v.scalar(p::edge_width);
        const double edge_falloff = v.scalar(p::edge_falloff);
        const double center_radius = v.scalar(p::center_radius);
        const double center_falloff = v.scalar(p::center_falloff);
        const auto edge_zone = [&](const std::array<double, 2>& t) {
            // distance inside the edge pushed out by Shift Out; outside it grows again unless One Way holds it at 0
            const double d = 1.0 + shift_out - std::max(std::abs(t[0]), std::abs(t[1]));
            return zone(one_way ? std::max(d, 0.0) : std::abs(d), edge_width, edge_falloff);
        };
        const auto center_zone = [&](const std::array<double, 2>& t) {
            return zone(std::hypot(t[0] - pivot_t[0], t[1] - pivot_t[1]), center_radius, center_falloff);
        };
        const std::array<double, 2> hotspot_t = trigger_at(hx, hy);
        const double edge = edge_zone(hotspot_t);
        const double center = center_zone(hotspot_t);
        double bright =
            std::lerp(1.0, v.scalar(p::edge_scale_brightness), edge) *
            std::lerp(1.0, v.scalar(p::center_scale_brightness), center) *
            flicker(v.scalar(p::flicker_amp), v.scalar(p::flicker_speed), v.scalar(p::flicker_randomness), args.time);
        double widths = std::lerp(1.0, v.scalar(p::edge_scale_widths), edge) *
                        std::lerp(1.0, v.scalar(p::center_scale_widths), center);

        // occlusion dims and narrows the flare by the hidden share; Use Color filters it by the mask's color
        core::buffer occluder;
        core::param_values::value mask_color{1, 1, 1};
        if (args.mask != nullptr)
        {
            if (const int from = v.choice(p::occlusion_from); from != 0)
            {
                occluder = core::mask::build(args, from == 1 ? core::mask::source::luma : core::mask::source::alpha,
                                             0.0, v.toggle(p::invert_occlusion));
                const double hidden = 1.0 - visibility(occluder.view(), hx, hy,
                                                       core::blur::amount_px(v.scalar(p::occlusion_softness), f));
                bright *= std::max(0.0, 1.0 - (v.scalar(p::occlusion_scale_brightness) * hidden));
                widths *= std::max(0.0, 1.0 - (v.scalar(p::occlusion_scale_widths) * hidden));
            }
            const float* m = v.toggle(p::use_color) ? core::sample::nearest(*args.mask, hx, hy) : nullptr;
            if (m != nullptr && m[3] > 0.0f)
            {
                mask_color = {m[0] / m[3], m[1] / m[3], m[2] / m[3]};
            }
        }

        // Brightness x Color x a group's color, times `amount` and filtered by `filter`
        const auto light = [&](p tint, double amount, const core::param_values::value& filter) {
            core::color::rgb out{};
            for (std::size_t c = 0; c < 3; ++c)
            {
                out[c] = static_cast<float>(v.scalar(p::brightness) * v.vec(p::color)[c] * v.vec(tint)[c] * amount *
                                            filter[c]);
            }
            return out;
        };
        const double hotspot = v.scalar(p::hotspot_bright) * bright;
        const core::glow::atmosphere atmosphere = core::glow::make_atmosphere(
            v.scalar(p::atmosphere_amp), v.scalar(p::atmosphere_freq), v.scalar(p::atmosphere_detail),
            v.scalar(p::atmosphere_seed), v.scalar(p::atmosphere_speed), args.time);
        const core::flare::settings settings{
            .hotspot_x = hx,
            .hotspot_y = hy,
            .pivot_x = px,
            .pivot_y = py,
            .unit = f.width * v.scalar(p::scale_widths) / FLARE_UNITS_PER_WIDTH,
            .rel_height = v.scalar(p::rel_heights),
            .width_scale = widths,
            .gamma = v.scalar(p::gamma),
            .saturation = v.scalar_f(p::saturation),
            .rays_rotate = v.scalar(p::rays_rotate),
            .rays_num_scale = v.scalar(p::rays_num_scale),
            .rays_length = v.scalar(p::rays_length),
            .rays_thickness = v.scalar(p::rays_thickness),
            .other_width = v.scalar(p::other_width),
            .hotspot_color = light(p::hotspot_color, hotspot, mask_color),
            .rays_color = light(p::hotspot_color, hotspot * v.scalar(p::rays_brightness), mask_color),
            .other_color = light(p::other_color, v.scalar(p::other_brightness) * bright, mask_color),
            .priority_only = v.choice(p::performance) == 1,
            .atmosphere = v.scalar(p::atmosphere_amp) != 0.0 ? &atmosphere : nullptr,
            .geometry = f,
        };

        // the flare over the output window, padded so Blur Flare reads real flare at the window edge
        const core::image& dst = args.dst;
        const double blur_px = core::blur::amount_px(v.scalar(p::blur_flare), f);
        const int pad = core::blur::reach(blur_px);
        core::buffer flare_buf(
            core::rect{dst.bounds.x0 - pad, dst.bounds.y0 - pad, dst.bounds.x1 + pad, dst.bounds.y1 + pad}, 3);
        const core::image& flare = flare_buf.view();
        core::flare::draw(flare, core::flare::lenses()[static_cast<std::size_t>(v.choice(p::lens))], settings);
        if (occluder.view().data != nullptr && v.scalar(p::diffraction_glow) != 0.0)
        {
            const double glow = v.scalar(p::hotspot_bright) * v.scalar(p::diffraction_glow);
            add_diffraction(flare, std::move(occluder), v, f, hx, hy,
                            light(p::hotspot_color, glow, v.vec(p::glow_color)));
        }
        if (const auto hue = v.scalar_f(p::hue_shift); hue != 0.0f)
        {
            core::parallel::for_pixels(flare.bounds, [&](int x, int y) {
                float* o = flare.at(x, y);
                const core::color::rgb shifted = core::color::hue_shift_preserve_luma({o[0], o[1], o[2]}, hue);
                std::ranges::copy(shifted, o);
            });
        }
        core::blur::gaussian(flare, blur_px, blur_px);

        // composite
        const auto affect_alpha = v.scalar_f(p::affect_alpha);
        const auto bg_scale = v.scalar_f(p::bg_brightness);
        const int combine = v.choice(p::combine);
        const bool tint_whites = v.toggle(p::tint_bg_whites);
        const bool show_edge = v.toggle(p::show_edge_zones);
        const bool show_center = v.toggle(p::show_center_zone);
        const auto merge = [combine](float a, float b) {
            return combine == 1 ? a + b : core::color::screen_super(a, b);
        };
        core::parallel::for_pixels(dst.bounds, [&](int x, int y) {
            const float* fl = flare.at(x, y);
            // alpha from the rgb max, capped at 1 so un-premultiplying keeps bright flares bright
            const std::array<float, 4> fr{fl[0], fl[1], fl[2],
                                          std::min(1.0f, affect_alpha * std::max({fl[0], fl[1], fl[2]}))};
            std::array<float, 4> bg{};
            if (args.src.bounds.contains(x, y))
            {
                const float* s = args.src.at(x, y);
                bg = {s[0] * bg_scale, s[1] * bg_scale, s[2] * bg_scale, s[3]};
            }
            std::array<float, 4> out{};
            if (show_edge || show_center)
            {
                // the trigger zones in gray with the flare screened over them
                const std::array<double, 2> t = trigger_at(x + 0.5, y + 0.5);
                const auto z = core::color::screen(show_edge ? static_cast<float>(edge_zone(t)) : 0.0f,
                                                   show_center ? static_cast<float>(center_zone(t)) : 0.0f);
                out = {core::color::screen_super(fr[0], z), core::color::screen_super(fr[1], z),
                       core::color::screen_super(fr[2], z), core::color::screen_super(fr[3], 1.0f)};
            }
            else if (combine == 2)
            {
                out = fr;
            }
            else if (tint_whites)
            {
                // the flare's luma merged, then its color added back on top of the clipped result
                const float luma =
                    core::color::luma(std::min(fr[0], 1.0f), std::min(fr[1], 1.0f), std::min(fr[2], 1.0f));
                for (std::size_t c = 0; c < 4; ++c)
                {
                    const float merged = std::min(merge(bg[c], c < 3 ? luma : fr[3]), 1.0f);
                    out[c] = merged + (c < 3 ? fr[c] - luma : 0.0f);
                }
            }
            else
            {
                for (std::size_t i = 0; i < 4; ++i)
                {
                    out[i] = merge(bg[i], fr[i]);
                }
            }
            std::ranges::copy(out, dst.at(x, y));
        });
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_lensflare",
            .name = "f_lensflare",
            .category = "Functional Lighting",
            .description = "Draws a lens flare from a hotspot through a pivot with built-in lenses, edge and centre "
                           "triggers and optional occlusion by a mask layer.",
            .params = PARAMS,
            .mask_input = "Occlusion Mask",
            .background_input = "",
            .time_varying = true,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::lensflare
