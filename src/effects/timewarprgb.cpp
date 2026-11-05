#include "effects/timewarprgb.hpp"
#include "core/color.hpp"
#include "core/mask.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace functional_fx_suite::effects::timewarprgb
{
    enum class p : std::uint8_t
    {
        red_shift_frames,
        green_shift_frames,
        blue_shift_frames,
        clamp_chroma,
        mask_use,
        blur_mask,
        invert_mask,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    // the shifts are whole frames
    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "red_shift_frames", "Red Shift Frames")
            .value(1)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(-30, 30)
            .digits(0),
        param::scalar(2, "green_shift_frames", "Green Shift Frames")
            .value(0)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(-30, 30)
            .digits(0),
        param::scalar(3, "blue_shift_frames", "Blue Shift Frames")
            .value(-1)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(-30, 30)
            .digits(0),
        param::scalar(4, "clamp_chroma", "Clamp Chroma").value(1).range(-UNBOUNDED, 1).slider(0, 1),
        param::choice(5, "mask_use", "Mask Use", "Luma|Alpha"),
        param::scalar(6, "blur_mask", "Blur Mask").value(0.05).range(0, 1000).slider(0, 2),
        param::toggle(7, "invert_mask", "Invert Mask"),
    };

    static constexpr std::array SHIFTS{p::red_shift_frames, p::green_shift_frames, p::blue_shift_frames};

    static std::vector<int> frame_offsets(const core::param_values& v, int /*frame*/)
    {
        std::vector<int> offsets;
        for (const p channel : SHIFTS)
        {
            const int n = v.whole(channel);
            if (n != 0 && std::ranges::find(offsets, n) == offsets.end())
            {
                offsets.push_back(n);
            }
        }
        return offsets;
    }

    // straight color of one channel, so frames with different alpha combine; 0 outside a frame or where it's clear
    static float straight(const core::image& img, int x, int y, int c)
    {
        if (img.data == nullptr || !img.bounds.contains(x, y))
        {
            return 0.0f;
        }
        const float* s = img.at(x, y);
        return s[3] > 0.0f ? s[c] / s[3] : 0.0f;
    }

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        // each channel's frame, fetched once per distinct shift
        const std::array n{v.whole(p::red_shift_frames), v.whole(p::green_shift_frames), v.whole(p::blue_shift_frames)};
        std::array<core::buffer, 3> fetched;
        std::array<core::image, 3> frames;
        for (std::size_t i = 0; i < n.size(); ++i)
        {
            const auto first = static_cast<std::size_t>(std::ranges::find(n, n[i]) - n.begin());
            if (n[i] != 0 && first == i)
            {
                fetched[i] = args.frame_at(n[i]);
            }
            frames[i] = n[i] == 0 ? args.src : fetched[first].view();
        }
        const auto clamp_chroma = v.scalar_f(p::clamp_chroma);

        const core::image& dst = args.dst;
        core::parallel::for_pixels(dst.bounds, [&](int x, int y) {
            float* d = dst.at(x, y);
            if (!args.src.bounds.contains(x, y))
            {
                std::fill_n(d, 4, 0.0f);
                return;
            }
            core::color::rgb c{straight(frames[0], x, y, 0), straight(frames[1], x, y, 1),
                               straight(frames[2], x, y, 2)};
            const float chroma = std::ranges::max(c) - std::ranges::min(c);
            if (clamp_chroma < 1.0f && chroma > 0.0f)
            {
                c = core::color::sat_scale(c, std::min(1.0f, clamp_chroma / chroma));
            }
            // the current frame's alpha
            const float a = args.src.at(x, y)[3];
            d[0] = c[0] * a;
            d[1] = c[1] * a;
            d[2] = c[2] * a;
            d[3] = a;
        });

        core::mask::apply(args, core::mask::luma_or_alpha(v.choice(p::mask_use)), v.scalar(p::blur_mask),
                          v.toggle(p::invert_mask));
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_timewarprgb",
            .name = "f_timewarprgb",
            .category = "Functional Time",
            .description = "Takes the red, green and blue channels from different frames of the clip, so moving "
                           "content splits into color fringes.",
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
} // namespace functional_fx_suite::effects::timewarprgb
