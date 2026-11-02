#include "effects/pseudocolor.hpp"
#include "core/color.hpp"
#include "core/mask.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <array>

namespace functional_fx_suite::effects::pseudocolor
{
    enum class p : std::uint8_t
    {
        frequency,
        hue_shift,
        saturation,
        brightness,
        scale_by_source,
        scale_by_src_amp,
        mix_with_source,
        mask_use,
        blur_mask,
        invert_mask,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "frequency", "Frequency").value(2).range(-UNBOUNDED, UNBOUNDED).slider(0.1, 50),
        param::scalar(2, "hue_shift", "Hue Shift").value(0).range(-1, 1),
        param::scalar(3, "saturation", "Saturation").value(1).range(-2, 8).slider(0, 2),
        param::scalar(4, "brightness", "Brightness").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(5, "scale_by_source", "Scale By Source").value(1).range(0, 1),
        param::scalar(6, "scale_by_src_amp", "Scale By Src Amp").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 10),
        param::scalar(7, "mix_with_source", "Mix With Source").value(0).range(0, 1),
        param::choice(8, "mask_use", "Mask Use", "Luma|Alpha"),
        param::scalar(9, "blur_mask", "Blur Mask").value(0.05).range(0, 1000).slider(0, 2),
        param::toggle(10, "invert_mask", "Invert Mask"),
    };

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const double frequency = v.scalar(p::frequency);
        const double hue_shift = v.scalar(p::hue_shift);
        const double saturation = v.scalar(p::saturation);
        const double brightness = v.scalar(p::brightness);
        const float scale_by_source = v.scalar_f(p::scale_by_source);
        const float amp = v.scalar_f(p::scale_by_src_amp);
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
                    // the brightness picks a hue, wrapping Frequency times from black to white
                    const core::color::rgb c =
                        core::color::hsv_to_rgb((frequency * bw) - hue_shift, saturation, brightness);
                    const float k = core::color::lerp(1.0f, std::min(1.0f, bw * amp), scale_by_source);
                    for (int i = 0; i < 3; ++i)
                    {
                        d[i] = core::color::lerp(c[i] * k, s[i] * inv, mix) * a;
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
            .match_name = "f_pseudocolor",
            .name = "f_pseudocolor",
            .category = "Functional Adjust",
            .description = "Maps the brightness of the clip onto a repeating rainbow of hues.",
            .params = PARAMS,
            .mask_input = "Mask",
            .background_input = "",
            .time_varying = false,
            .reads_outside = false,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::pseudocolor
