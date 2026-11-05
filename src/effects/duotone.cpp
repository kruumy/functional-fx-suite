#include "effects/duotone.hpp"
#include "core/color.hpp"
#include "core/mask.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <array>

namespace functional_fx_suite::effects::duotone
{
    enum class p : std::uint8_t
    {
        color1,
        color0,
        invert,
        threshold,
        softness,
        mix_with_source,
        mask_use,
        blur_mask,
        invert_mask,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::color(1, "color1", "Color1").value({1, 0.9, 0.8}),
        param::color(2, "color0", "Color0").value({0, 0, 0.2}),
        param::toggle(3, "invert", "Invert"),
        param::scalar(4, "threshold", "Threshold").value(0.5).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(5, "softness", "Softness").value(1).range(0.001, UNBOUNDED).slider(0.001, 1),
        param::scalar(6, "mix_with_source", "Mix With Source").value(0).range(0, 1),
        param::choice(7, "mask_use", "Mask Use", "Luma|Alpha"),
        param::scalar(8, "blur_mask", "Blur Mask").value(0.05).range(0, 1000).slider(0, 2),
        param::toggle(9, "invert_mask", "Invert Mask"),
    };

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const auto& c0 = v.vec(p::color0);
        const auto& c1 = v.vec(p::color1);
        const bool invert = v.toggle(p::invert);
        const double softness = v.scalar(p::softness);
        const auto scale = static_cast<float>(1.0 / softness);
        const auto offset = static_cast<float>(0.5 - v.scalar(p::threshold) / softness);
        const float mix = v.scalar_f(p::mix_with_source);

        const core::image& dst = args.dst;
        core::parallel::for_range(dst.bounds.y0, dst.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                for (int x = dst.bounds.x0; x < dst.bounds.x1; ++x)
                {
                    float* d = dst.at(x, y);
                    if (!args.src.bounds.contains(x, y))
                    {
                        std::fill_n(d, 4, 0.0f);
                        continue;
                    }
                    const float* s = args.src.at(x, y);
                    const float a = s[3];
                    const float inv = a > 0.0f ? 1.0f / a : 0.0f;
                    const float bw = core::color::luma(s[0] * inv, s[1] * inv, s[2] * inv) * scale + offset;
                    const float t = invert ? 1.0f - bw : bw;
                    for (int c = 0; c < 3; ++c)
                    {
                        const float tone =
                            core::color::dissolve(static_cast<float>(c0[c]), static_cast<float>(c1[c]), t);
                        d[c] = core::color::lerp(tone, s[c] * inv, mix) * a;
                    }
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
            .match_name = "f_duotone",
            .name = "f_duotone",
            .category = "Functional Adjust",
            .description = "Maps the brightness of the clip onto a ramp between two colors.",
            .params = PARAMS,
            .mask_input = "Mask",
            .background_input = "",
            .time_varying = false,
            .reads_outside = false,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::duotone
