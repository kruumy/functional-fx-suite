#include "effects/tint.hpp"
#include "core/color.hpp"
#include "core/mask.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace functional_fx_suite::effects::tint
{
    enum class p : std::uint8_t
    {
        tint_lights,
        tint_darks,
        source_saturation,
        scale_lights,
        offset_darks,
        mask_use,
        blur_mask,
        invert_mask,
        count
    };

    using core::param;

    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::color(1, "tint_lights", "Tint Lights").value({1, 1, 1}),
        param::color(2, "tint_darks", "Tint Darks").value({0, 0, 0}),
        param::scalar(3, "source_saturation", "Source Saturation").value(1).range(-2, 8).slider(0, 2),
        param::scalar(4, "scale_lights", "Scale Lights").value(1).range(-1000, 1000).slider(0, 2),
        param::scalar(5, "offset_darks", "Offset Darks").value(0).range(-8, 2).slider(-5, 1),
        param::choice(6, "mask_use", "Mask Use", "Luma|Alpha"),
        param::scalar(7, "blur_mask", "Blur Mask").value(0.05).range(0, 1000).slider(0, 2),
        param::toggle(8, "invert_mask", "Invert Mask"),
    };

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const auto& lights = v.vec(p::tint_lights);
        const auto& darks = v.vec(p::tint_darks);
        const float saturation = v.scalar_f(p::source_saturation);
        std::array<float, 3> scale{};
        std::array<float, 3> offset{};
        for (int c = 0; c < 3; ++c)
        {
            const double l = lights[c] * v.scalar(p::scale_lights);
            const double d = darks[c] + v.scalar(p::offset_darks);
            // lights scale keeps its sign while darks eat into its magnitude
            scale[c] = l == 0.0 ? 0.0f : static_cast<float>(std::copysign(std::max(0.0, std::abs(l) - d), l));
            offset[c] = static_cast<float>(d);
        }

        const core::image& dst = args.dst;
        core::parallel::for_pixels(dst.bounds, [&](int x, int y) {
            float* d = dst.at(x, y);
            if (!args.src.bounds.contains(x, y))
            {
                std::fill_n(d, 4, 0.0f);
                return;
            }
            const float* s = args.src.at(x, y);
            const float a = s[3];
            // the math works on straight color, so unpremultiply around it
            const float inv = a > 0.0f ? 1.0f / a : 0.0f;
            const core::color::rgb saturated = core::color::sat_scale({s[0] * inv, s[1] * inv, s[2] * inv}, saturation);
            for (int c = 0; c < 3; ++c)
            {
                d[c] = (saturated[c] * scale[c] + offset[c]) * a;
            }
            d[3] = a;
        });

        core::mask::apply(args, core::mask::luma_or_alpha(v.choice(p::mask_use)), v.scalar(p::blur_mask),
                          v.toggle(p::invert_mask));
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_tint",
            .name = "f_tint",
            .category = "Functional Adjust",
            .description = "Tints the dark and light regions of the clip towards two colors.",
            .params = PARAMS,
            .mask_input = "Mask",
            .background_input = "",
            .time_varying = false,
            .reads_outside = false,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::tint
