#include "core/glow.hpp"
#include "core/mask.hpp"
#include "effects/glow.hpp"
#include <array>

namespace functional_fx_suite::effects::glow
{
    enum class p : std::uint8_t
    {
        brightness,
        color,
        threshold,
        threshold_add_color,
        glow_width,
        width_x,
        width_y,
        width_red,
        width_green,
        width_blue,
        subpixel,
        show,
        combine,
        edge_mode,
        affect_alpha,
        glow_from_alpha,
        glow_under_source,
        light_background,
        source_opacity,
        bg_brightness,
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
        param::scalar(1, "brightness", "Brightness").value(2).range(0, UNBOUNDED).slider(0, 4),
        param::color(2, "color", "Color").value({1, 1, 1}),
        param::scalar(3, "threshold", "Threshold").value(0.5).range(0, UNBOUNDED).slider(0, 1),
        param::color(4, "threshold_add_color", "Threshold Add Color").value({0, 0, 0}),
        param::scalar(5, "glow_width", "Glow Width").value(0.4).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(6, "width_x", "Width X").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(7, "width_y", "Width Y").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(8, "width_red", "Width Red").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(9, "width_green", "Width Green").value(1.2).range(0, UNBOUNDED).slider(0, 2.4),
        param::scalar(10, "width_blue", "Width Blue").value(1.4).range(0, UNBOUNDED).slider(0, 2.8),
        param::toggle(11, "subpixel", "Subpixel").value(1),
        param::choice(12, "show", "Show", "Result|Threshold"),
        param::choice(13, "combine", "Combine", "Mult|Add|Screen|Difference|Overlay").value(2),
        param::choice(14, "edge_mode", "Edge Mode", "Transparent|Reflect").value(1),
        param::scalar(15, "affect_alpha", "Affect Alpha").value(0).range(0, UNBOUNDED).slider(0, 1),
        param::scalar(16, "glow_from_alpha", "Glow From Alpha").value(0).range(0, 1),
        param::scalar(17, "glow_under_source", "Glow Under Source").value(0).range(0, 1),
        param::scalar(18, "light_background", "Light Background").value(0).range(0, 1),
        param::scalar(19, "source_opacity", "Source Opacity").value(1).range(0, 1),
        param::scalar(20, "bg_brightness", "Bg Brightness").value(1).range(0, UNBOUNDED).slider(0, 1),
        param::toggle(21, "invert_matte", "Invert Matte"),
        param::group(22, "atmosphere", "Atmosphere"),
        param::scalar(23, "atmosphere_amp", "Atmosphere Amp").value(0).range(0, UNBOUNDED).slider(0, 5),
        param::scalar(24, "atmosphere_freq", "Atmosphere Freq").value(1).range(0.1, 20),
        param::scalar(25, "atmosphere_detail", "Atmosphere Detail").value(0.6).range(0, 1),
        param::scalar(26, "atmosphere_seed", "Atmosphere Seed").value(0.123).range(0, UNBOUNDED).slider(0, 1),
        param::scalar(27, "atmosphere_speed", "Atmosphere Speed").value(1).range(-UNBOUNDED, UNBOUNDED).slider(-5, 5),
        param::group_end(28, "atmosphere_end"),
    };

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const core::buffer matte = core::mask::build(args, core::mask::source::color, 0.0, v.toggle(p::invert_matte));
        const core::image* matte_view = matte.view_if_set();
        const core::buffer glow = core::glow::source(args, matte_view, v.scalar(p::glow_from_alpha));
        const auto& add = v.vec(p::threshold_add_color);
        const double t = v.scalar(p::threshold);
        core::glow::threshold(glow.view(), core::glow::thresholds(t, add));

        // Show Threshold stops at the thresholded image
        const bool show = v.choice(p::show) == 1;
        if (!show)
        {
            const std::array channel{v.scalar(p::width_red), v.scalar(p::width_green), v.scalar(p::width_blue)};
            const auto& color = v.vec(p::color);
            core::glow::rgb ax{};
            core::glow::rgb ay{};
            core::glow::rgb gain{};
            for (int c = 0; c < 3; ++c)
            {
                const double px = core::blur::amount_px(v.scalar(p::glow_width) * channel[c], args.geometry);
                ax[c] = px * v.scalar(p::width_x);
                ay[c] = px * v.scalar(p::width_y);
                gain[c] = v.scalar(p::brightness) * color[c];
            }
            const auto edge = v.choice(p::edge_mode) == 1 ? core::blur::edge::reflect : core::blur::edge::transparent;
            core::glow::blur_channels(glow.view(), ax, ay, gain, edge);
        }
        const auto atmosphere = core::glow::make_atmosphere(
            v.scalar(p::atmosphere_amp), v.scalar(p::atmosphere_freq), v.scalar(p::atmosphere_detail),
            v.scalar(p::atmosphere_seed), v.scalar(p::atmosphere_speed), args.time);
        core::glow::apply(atmosphere, glow.view(), args.geometry);

        const core::glow::composite_settings settings{
            .affect_alpha = v.scalar(p::affect_alpha),
            .mode = static_cast<core::glow::combine>(v.choice(p::combine)),
            .glow_under_source = v.scalar(p::glow_under_source),
            .light_bg = v.scalar(p::light_background),
            .scale_background = v.scalar(p::bg_brightness),
            .scale_source = v.scalar(p::source_opacity),
            .show = show,
        };
        core::glow::composite(args, args.src, glow.view(), glow.view(), settings);
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_glow",
            .name = "f_glow",
            .category = "Functional Lighting",
            .description = "Glows light from the areas of the clip brighter than a threshold.",
            .params = PARAMS,
            .mask_input = "Matte",
            .background_input = "Background",
            .time_varying = true,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::glow
