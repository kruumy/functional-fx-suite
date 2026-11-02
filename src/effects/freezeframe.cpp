#include "effects/freezeframe.hpp"
#include "core/retime.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace functional_fx_suite::effects::freezeframe
{
    enum class p : std::uint8_t
    {
        freeze_frames,
        frame_start,
        mask_use,
        blur_mask,
        invert_mask,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "freeze_frames", "Freeze Frames").value(5).range(1, UNBOUNDED).slider(1, 30).digits(0),
        param::scalar(2, "frame_start", "Frame Start").value(0).range(0, UNBOUNDED).slider(0, 30).digits(0),
        param::choice(3, "mask_use", "Mask Use", "Luma|Alpha"),
        param::scalar(4, "blur_mask", "Blur Mask").value(0.05).range(0, 1000).slider(0, 2),
        param::toggle(5, "invert_mask", "Invert Mask"),
    };

    static int whole(const core::param_values& v, p id, double min)
    {
        return static_cast<int>(std::lround(std::clamp(v.scalar(id), min, UNBOUNDED)));
    }

    // each block of Freeze Frames frames from Frame Start holds its first frame
    static int offset(const core::param_values& v, int frame)
    {
        return core::retime::block_start(frame, whole(v, p::freeze_frames, 1), whole(v, p::frame_start, 0)) - frame;
    }

    static std::vector<int> frame_offsets(const core::param_values& v, int frame)
    {
        return offset(v, frame) != 0 ? std::vector<int>{offset(v, frame)} : std::vector<int>{};
    }

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        core::retime::render(args, offset(v, args.frame_number()),
                             v.choice(p::mask_use) == 1 ? core::mask::source::alpha : core::mask::source::luma,
                             v.scalar(p::blur_mask), v.toggle(p::invert_mask));
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_freezeframe",
            .name = "f_freezeframe",
            .category = "Functional Time",
            .description = "Holds every few frames of the clip, for a stepped, low frame rate look.",
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
} // namespace functional_fx_suite::effects::freezeframe
