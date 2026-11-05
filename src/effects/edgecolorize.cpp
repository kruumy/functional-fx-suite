#include "effects/edgecolorize.hpp"
#include "core/color.hpp"
#include "core/edges.hpp"
#include "core/mask.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace functional_fx_suite::effects::edgecolorize
{
    enum class p : std::uint8_t
    {
        edge_smooth,
        subpixel_smooth,
        brightness,
        rotate_colors,
        background,
        top,
        right,
        bottom,
        left,
        mask_use,
        blur_mask,
        invert_mask,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    // Rotate Colors is in degrees
    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "edge_smooth", "Edge Smooth").value(0.0224).range(0, UNBOUNDED).slider(0, 1),
        param::toggle(2, "subpixel_smooth", "Subpixel Smooth").value(1),
        param::scalar(3, "brightness", "Brightness").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 10),
        param::scalar(4, "rotate_colors", "Rotate Colors").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-180, 180),
        param::color(5, "background", "Background").value({0, 0, 0}),
        param::color(6, "top", "Top").value({1, 0.85, 0.5}),
        param::color(7, "right", "Right").value({0, 0.1, 0.5}),
        param::color(8, "bottom", "Bottom").value({0.3, 0.3, 0.3}),
        param::color(9, "left", "Left").value({0.5, 0, 0}),
        param::choice(10, "mask_use", "Mask Use", "Luma|Alpha"),
        param::scalar(11, "blur_mask", "Blur Mask").value(0.05).range(0, 1000).slider(0, 2),
        param::toggle(12, "invert_mask", "Invert Mask"),
    };

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const core::buffer straight = core::edges::straight_rgb(args.src);
        const core::image& s = straight.view();
        core::buffer luma(s.bounds, 1);
        const core::image& l = luma.view();
        core::parallel::for_pixels(s.bounds, [&](int x, int y) {
            const float* c = s.at(x, y);
            *l.at(x, y) = core::color::luma(c[0], c[1], c[2]);
        });
        const double smooth = v.scalar(p::edge_smooth);
        core::edges::prepare(l, smooth, v.scalar(p::brightness) * (smooth + 0.1), args.geometry);

        // the gradient is rotated against Rotate Colors, so the colors turn with it
        const double angle = -v.scalar(p::rotate_colors) * std::numbers::pi / 180.0;
        const auto cos_t = static_cast<float>(std::cos(angle));
        const auto sin_t = static_cast<float>(std::sin(angle));
        const auto& bg = v.vec(p::background);
        const auto& top = v.vec(p::top);
        const auto& right = v.vec(p::right);
        const auto& bottom = v.vec(p::bottom);
        const auto& left = v.vec(p::left);

        const core::image& dst = args.dst;
        core::parallel::for_pixels(dst.bounds, [&](int x, int y) {
            float* d = dst.at(x, y);
            if (!args.src.bounds.contains(x, y))
            {
                std::fill_n(d, 4, 0.0f);
                return;
            }
            const auto [ux, uy] = core::edges::gradient(l, x, y, 0);
            const float gx = cos_t * ux - sin_t * uy;
            const float gy = sin_t * ux + cos_t * uy;
            // brighter to the right lights a left-facing edge, brighter above a bottom-facing one
            const auto& horizontal = gx > 0.0f ? left : right;
            const auto& vertical = gy > 0.0f ? bottom : top;
            const float a = args.src.at(x, y)[3];
            for (int c = 0; c < 3; ++c)
            {
                const auto b = static_cast<float>(bg[c]);
                const float out = b + (static_cast<float>(horizontal[c]) - b) * std::abs(gx) +
                                  (static_cast<float>(vertical[c]) - b) * std::abs(gy);
                d[c] = out * a;
            }
            d[3] = a;
        });

        core::mask::apply(args, core::mask::luma_or_alpha(v.choice(p::mask_use)), v.scalar(p::blur_mask),
                          v.toggle(p::invert_mask));
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_edgecolorize",
            .name = "f_edgecolorize",
            .category = "Functional Stylize",
            .description = "Colors the edges of the clip by the direction they face.",
            .params = PARAMS,
            .mask_input = "Mask",
            .background_input = "",
            .time_varying = false,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::edgecolorize
