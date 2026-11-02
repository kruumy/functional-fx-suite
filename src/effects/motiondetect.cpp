#include "effects/motiondetect.hpp"
#include "core/color.hpp"
#include "core/mask.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace functional_fx_suite::effects::motiondetect
{
    enum class p : std::uint8_t
    {
        delay_frames,
        brightness,
        offset_darks,
        saturation,
        motion,
        combine,
        output_opacity,
        mask_use,
        blur_mask,
        invert_mask,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "delay_frames", "Delay Frames").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 30).digits(0),
        param::scalar(2, "brightness", "Brightness").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(3, "offset_darks", "Offset Darks").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-5, 1),
        param::scalar(4, "saturation", "Saturation").value(1).range(-UNBOUNDED, 10).slider(0, 2),
        param::choice(5, "motion", "Motion", "All|Brighter|Darker"),
        param::choice(6, "combine", "Combine", "Motion Only|Mult|Add|Screen|Difference|Overlay|Subtract"),
        param::choice(10, "output_opacity", "Output Opacity", "All Opaque|Copy From Input"),
        param::choice(7, "mask_use", "Mask Use", "Luma|Alpha"),
        param::scalar(8, "blur_mask", "Blur Mask").value(0.05).range(0, 1000).slider(0, 2),
        param::toggle(9, "invert_mask", "Invert Mask"),
    };

    enum class motion : std::uint8_t
    {
        all,
        brighter,
        darker
    };

    enum class combine : std::uint8_t
    {
        motion_only,
        mult,
        add,
        screen,
        difference,
        overlay,
        subtract
    };

    static int delay(const core::param_values& v)
    {
        return static_cast<int>(std::lround(std::clamp(v.scalar(p::delay_frames), -UNBOUNDED, UNBOUNDED)));
    }

    static std::vector<int> frame_offsets(const core::param_values& v, int /*frame*/)
    {
        return delay(v) != 0 ? std::vector<int>{-delay(v)} : std::vector<int>{};
    }

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const int n = delay(v);
        const core::buffer fetched = n != 0 ? args.frame_at(-n) : core::buffer{};
        const core::image& before = n != 0 ? fetched.view() : args.src;
        const auto kind = static_cast<motion>(std::clamp(v.choice(p::motion), 0, 2));
        const auto mode = static_cast<combine>(std::clamp(v.choice(p::combine), 0, 6));
        const float brightness = v.scalar_f(p::brightness);
        const float darks = v.scalar_f(p::offset_darks);
        const float saturation = v.scalar_f(p::saturation);
        const bool opaque = v.choice(p::output_opacity) == 0;

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
                    // a frame the host has none of is transparent
                    const float* b = before.data != nullptr && before.bounds.contains(x, y) ? before.at(x, y) : nullptr;
                    core::color::rgb m{};
                    for (int c = 0; c < 3; ++c)
                    {
                        const float diff = s[c] - (b != nullptr ? b[c] : 0.0f);
                        m[c] = kind == motion::darker ? -diff : kind == motion::brighter ? diff : std::abs(diff);
                    }
                    m = core::color::sat_scale(m, saturation);
                    for (int c = 0; c < 3; ++c)
                    {
                        const float l = core::color::scale_lights_offset_darks(m[c], brightness, darks);
                        switch (mode)
                        {
                        case combine::motion_only:
                            d[c] = l;
                            break;
                        case combine::mult:
                            d[c] = s[c] * l;
                            break;
                        case combine::add:
                            d[c] = s[c] + l;
                            break;
                        case combine::screen:
                            d[c] = core::color::screen_super(s[c], l);
                            break;
                        case combine::difference:
                            d[c] = std::abs(s[c] - std::clamp(l, 0.0f, 1.0f));
                            break;
                        case combine::overlay:
                            d[c] = core::color::overlay_super(s[c], l);
                            break;
                        case combine::subtract:
                            d[c] = s[c] - l;
                            break;
                        }
                    }
                    // opaque is the result over black; Copy From Input keeps the clip's alpha and cuts the result to it
                    const float a = opaque ? 1.0f : s[3];
                    std::for_each(d, d + 3, [a](float& c) { c *= a; });
                    d[3] = a;
                }
            }
        });

        const core::buffer m =
            core::mask::build(args, v.choice(p::mask_use) == 1 ? core::mask::source::alpha : core::mask::source::luma,
                              v.scalar(p::blur_mask), v.toggle(p::invert_mask));
        if (m.view().data != nullptr)
        {
            core::mask::dissolve(args, m.view());
        }
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_motiondetect",
            .name = "f_motiondetect",
            .category = "Functional Time",
            .description = "Shows what changed since an earlier frame of the clip, alone or combined with the clip.",
            .params = PARAMS,
            .mask_input = "Mask",
            .background_input = "",
            .time_varying = false,
            .reads_outside = false,
            .render = render,
            .frame_offsets = frame_offsets,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::motiondetect
