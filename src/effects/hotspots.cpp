#include "effects/hotspots.hpp"
#include "core/blur.hpp"
#include "core/color.hpp"
#include "core/edges.hpp"
#include "core/glow.hpp"
#include "core/mask.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <array>

namespace functional_fx_suite::effects::hotspots
{
    enum class p : std::uint8_t
    {
        blur_input,
        threshold,
        threshold_add_color,
        saturation,
        brightness,
        mask_use,
        blur_mask,
        invert_mask,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "blur_input", "Blur Input").value(0).range(0, UNBOUNDED).slider(0, 0.8),
        param::scalar(2, "threshold", "Threshold").value(0.7).range(0, UNBOUNDED).slider(0, 1),
        param::color(3, "threshold_add_color", "Threshold Add Color").value({0, 0, 0}),
        param::scalar(4, "saturation", "Saturation").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(5, "brightness", "Brightness").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::choice(6, "mask_use", "Mask Use", "Luma|Alpha"),
        param::scalar(7, "blur_mask", "Blur Mask").value(0.05).range(0, 1000).slider(0, 2),
        param::toggle(8, "invert_mask", "Invert Mask"),
    };

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const core::buffer pic = core::edges::straight_rgb(args.src);
        const double amount = core::blur::amount_px(v.scalar(p::blur_input), args.geometry);
        core::blur::gaussian(pic.view(), amount, amount);

        const auto& add = v.vec(p::threshold_add_color);
        const double t = v.scalar(p::threshold);
        const core::glow::rgb th = core::glow::thresholds(t, add);
        core::glow::hotspots(pic.view(), th);
        constexpr double MIN_HEADROOM = 0.01;
        const auto gain = static_cast<float>(v.scalar(p::brightness) / core::glow::headroom(th, MIN_HEADROOM));
        const float saturation = v.scalar_f(p::saturation);

        const core::image& dst = args.dst;
        core::parallel::for_pixels(dst.bounds, [&](int x, int y) {
            float* d = dst.at(x, y);
            if (!args.src.bounds.contains(x, y))
            {
                std::fill_n(d, 4, 0.0f);
                return;
            }
            const float* h = pic.view().at(x, y);
            const float a = args.src.at(x, y)[3];
            const core::color::rgb out = core::color::sat_scale({h[0] * gain, h[1] * gain, h[2] * gain}, saturation);
            std::ranges::transform(out, d, [a](float c) { return c * a; });
            d[3] = a;
        });

        core::mask::apply(args, core::mask::luma_or_alpha(v.choice(p::mask_use)), v.scalar(p::blur_mask),
                          v.toggle(p::invert_mask));
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_hotspots",
            .name = "f_hotspots",
            .category = "Functional Adjust",
            .description = "Keeps the areas of the clip brighter than a threshold, in their own colors.",
            .params = PARAMS,
            .mask_input = "Mask",
            .background_input = "",
            .time_varying = false,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::hotspots
