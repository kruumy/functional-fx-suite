#include "effects/tritone.hpp"
#include "core/color.hpp"
#include "core/mask.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <array>

namespace functional_fx_suite::effects::tritone
{
    enum class p : std::uint8_t
    {
        color2,
        color1,
        color0,
        softness,
        softness_12,
        softness_01,
        color2_at_bright,
        color1_at_bright,
        color0_at_bright,
        mix_with_source,
        mask_use,
        blur_mask,
        invert_mask,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::color(1, "color2", "Color2").value({1, 1, 1}),
        param::color(2, "color1", "Color1").value({0.5, 0.5, 0.5}),
        param::color(3, "color0", "Color0").value({0, 0, 0}),
        param::scalar(4, "softness", "Softness").value(1).range(0.001, UNBOUNDED).slider(0.001, 1),
        param::scalar(5, "softness_12", "Softness 12").value(1).range(0.001, UNBOUNDED).slider(0.001, 1),
        param::scalar(6, "softness_01", "Softness 01").value(1).range(0.001, UNBOUNDED).slider(0.001, 1),
        param::scalar(7, "color2_at_bright", "Color2 At Bright").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(8, "color1_at_bright", "Color1 At Bright").value(0.5).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(9, "color0_at_bright", "Color0 At Bright").value(0).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(10, "mix_with_source", "Mix With Source").value(0).range(0, 1),
        param::choice(11, "mask_use", "Mask Use", "Luma|Alpha"),
        param::scalar(12, "blur_mask", "Blur Mask").value(0.05).range(0, 1000).slider(0, 2),
        param::toggle(13, "invert_mask", "Invert Mask"),
    };

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const auto& c0 = v.vec(p::color0);
        const auto& c1 = v.vec(p::color1);
        const auto& c2 = v.vec(p::color2);
        const double b1 = v.scalar(p::color1_at_bright);
        const double b0 = std::min(v.scalar(p::color0_at_bright), b1);
        const double b2 = std::max(v.scalar(p::color2_at_bright), b1);
        // each half of the ramp maps its brightness span onto 0..1, sharpened around the middle by its softness
        constexpr double MIN_SPAN = 0.01;
        const auto scale01 = static_cast<float>(1.0 / std::max(b1 - b0, MIN_SPAN));
        const auto scale12 = static_cast<float>(1.0 / std::max(b2 - b1, MIN_SPAN));
        const auto soft01 = static_cast<float>(v.scalar(p::softness_01) * v.scalar(p::softness));
        const auto soft12 = static_cast<float>(v.scalar(p::softness_12) * v.scalar(p::softness));
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
                    const float bw = core::color::luma(s[0] * inv, s[1] * inv, s[2] * inv);
                    const bool upper = bw > b1;
                    const float r = upper ? ((bw - static_cast<float>(b1)) * scale12 - 0.5f) / soft12 + 0.5f
                                          : ((bw - static_cast<float>(b0)) * scale01 - 0.5f) / soft01 + 0.5f;
                    const auto& lo = upper ? c1 : c0;
                    const auto& hi = upper ? c2 : c1;
                    for (int c = 0; c < 3; ++c)
                    {
                        const float tone =
                            core::color::dissolve(static_cast<float>(lo[c]), static_cast<float>(hi[c]), r);
                        d[c] = core::color::lerp(tone, s[c] * inv, mix) * a;
                    }
                    d[3] = a;
                }
            }
        });

        const auto mode = v.choice(p::mask_use) == 1 ? core::mask::source::alpha : core::mask::source::luma;
        const core::buffer m = core::mask::build(args, mode, v.scalar(p::blur_mask), v.toggle(p::invert_mask));
        if (m.view().data != nullptr)
        {
            core::mask::dissolve(args, m.view());
        }
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_tritone",
            .name = "f_tritone",
            .category = "Functional Adjust",
            .description = "Maps the brightness of the clip onto a ramp through three colors.",
            .params = PARAMS,
            .mask_input = "Mask",
            .background_input = "",
            .time_varying = false,
            .reads_outside = false,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::tritone
