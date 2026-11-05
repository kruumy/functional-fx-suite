#include "effects/glowrings.hpp"
#include "core/glow.hpp"
#include "core/mask.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <array>

namespace functional_fx_suite::effects::glowrings
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
        thickness_red,
        thickness_green,
        thickness_blue,
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
        param::scalar(1, "brightness", "Brightness").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::color(2, "color", "Color").value({1, 1, 1}),
        param::scalar(3, "threshold", "Threshold").value(0).range(0, UNBOUNDED).slider(0, 1),
        param::color(4, "threshold_add_color", "Threshold Add Color").value({0, 0, 0}),
        param::scalar(5, "glow_width", "Glow Width").value(0.4).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(6, "width_x", "Width X").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(7, "width_y", "Width Y").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(8, "width_red", "Width Red").value(0.5).range(0, UNBOUNDED).slider(0, 1),
        param::scalar(9, "width_green", "Width Green").value(0.75).range(0, UNBOUNDED).slider(0, 1.5),
        param::scalar(10, "width_blue", "Width Blue").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::toggle(11, "subpixel", "Subpixel").value(1),
        param::scalar(12, "thickness_red", "Thickness Red").value(0.5).range(0, 1),
        param::scalar(13, "thickness_green", "Thickness Green").value(0.5).range(0, 1),
        param::scalar(14, "thickness_blue", "Thickness Blue").value(0.5).range(0, 1),
        param::choice(15, "show", "Show", "Result|Threshold"),
        param::choice(16, "combine", "Combine", "Mult|Add|Screen|Difference|Overlay").value(2),
        param::scalar(17, "affect_alpha", "Affect Alpha").value(0).range(0, UNBOUNDED).slider(0, 1),
        param::scalar(18, "glow_from_alpha", "Glow From Alpha").value(0).range(0, 1),
        param::scalar(19, "glow_under_source", "Glow Under Source").value(0).range(0, 1),
        param::scalar(20, "light_background", "Light Background").value(0).range(0, 1),
        param::scalar(21, "source_opacity", "Source Opacity").value(1).range(0, 1),
        param::scalar(22, "bg_brightness", "Bg Brightness").value(1).range(0, UNBOUNDED).slider(0, 1),
        param::toggle(23, "invert_matte", "Invert Matte"),
    };

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const core::buffer matte = core::mask::build(args, core::mask::source::color, 0.0, v.toggle(p::invert_matte));
        const core::image* matte_view = matte.view_if_set();
        const core::buffer pic = core::glow::source(args, matte_view, v.scalar(p::glow_from_alpha));
        const auto& add = v.vec(p::threshold_add_color);
        const double t = v.scalar(p::threshold);
        const core::glow::rgb th = core::glow::thresholds(t, add);
        core::glow::hotspots(pic.view(), th);

        // a ring is a wide glow minus a narrower one; Thickness sets how much narrower
        constexpr double MIN_HEADROOM = 0.01;
        constexpr double RING_GAIN = 5.0;
        const double headroom = core::glow::headroom(th, MIN_HEADROOM);
        const std::array channel{v.scalar(p::width_red), v.scalar(p::width_green), v.scalar(p::width_blue)};
        const std::array thickness{v.scalar(p::thickness_red), v.scalar(p::thickness_green),
                                   v.scalar(p::thickness_blue)};
        const auto& color = v.vec(p::color);
        core::glow::rgb outer_x{};
        core::glow::rgb outer_y{};
        core::glow::rgb inner_x{};
        core::glow::rgb inner_y{};
        core::glow::rgb gain{};
        for (int c = 0; c < 3; ++c)
        {
            const double w = v.scalar(p::glow_width) * channel[c];
            const double outer = core::blur::amount_px(w, args.geometry);
            const double inner = core::blur::amount_px(w * (1.0 - thickness[c]), args.geometry);
            outer_x[c] = outer * v.scalar(p::width_x);
            outer_y[c] = outer * v.scalar(p::width_y);
            inner_x[c] = inner * v.scalar(p::width_x);
            inner_y[c] = inner * v.scalar(p::width_y);
            gain[c] = v.scalar(p::brightness) * RING_GAIN * color[c] / headroom;
        }
        const core::rect& b = pic.view().bounds;
        const core::buffer ring = core::buffer::copy_of(pic.view());
        const core::buffer inner = core::buffer::copy_of(pic.view());
        core::glow::blur_channels(ring.view(), outer_x, outer_y, gain, core::blur::edge::transparent);
        core::glow::blur_channels(inner.view(), inner_x, inner_y, gain, core::blur::edge::transparent);
        core::parallel::for_pixels(b, [&](int x, int y) {
            float* r = ring.view().at(x, y);
            const float* i = inner.view().at(x, y);
            for (int c = 0; c < 3; ++c)
            {
                r[c] = std::max(0.0f, r[c] - i[c]);
            }
        });

        const core::glow::composite_settings settings{
            .affect_alpha = v.scalar(p::affect_alpha),
            .mode = static_cast<core::glow::combine>(v.choice(p::combine)),
            .glow_under_source = v.scalar(p::glow_under_source),
            .light_bg = v.scalar(p::light_background),
            .scale_background = v.scalar(p::bg_brightness),
            .scale_source = v.scalar(p::source_opacity),
            .show = v.choice(p::show) == 1,
        };
        core::glow::composite(args, args.src, ring.view(), pic.view(), settings);
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_glowrings",
            .name = "f_glowrings",
            .category = "Functional Lighting",
            .description = "Glows colored rings around the areas of the clip brighter than a threshold.",
            .params = PARAMS,
            .mask_input = "Matte",
            .background_input = "Background",
            .time_varying = false,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::glowrings
