#include "effects/reverseedits.hpp"
#include "core/retime.hpp"
#include <array>
#include <cmath>

namespace functional_fx_suite::effects::reverseedits
{
    enum class p : std::uint8_t
    {
        edit_frame_length,
        edits_start,
        mask_use,
        blur_mask,
        invert_mask,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "edit_frame_length", "Edit Frame Length").value(5).range(1, UNBOUNDED).slider(1, 30).digits(0),
        param::scalar(2, "edits_start", "Edits Start").value(0).range(0, UNBOUNDED).slider(0, 30).digits(0),
        param::choice(3, "mask_use", "Mask Use", "Luma|Alpha"),
        param::scalar(4, "blur_mask", "Blur Mask").value(0.05).range(0, 1000).slider(0, 2),
        param::toggle(5, "invert_mask", "Invert Mask"),
    };

    // each block of Edit Frame Length frames from Edits Start plays backwards
    static int offset(const core::param_values& v, int frame)
    {
        const int length = v.whole(p::edit_frame_length);
        const int cut = core::retime::block_start(frame, length, v.whole(p::edits_start));
        return (2 * (cut - frame)) + length - 1;
    }

    static std::vector<int> frame_offsets(const core::param_values& v, int frame)
    {
        return core::retime::offsets(offset(v, frame));
    }

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        core::retime::render(args, offset(v, args.frame_number()), core::mask::luma_or_alpha(v.choice(p::mask_use)),
                             v.scalar(p::blur_mask), v.toggle(p::invert_mask));
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_reverseedits",
            .name = "f_reverseedits",
            .category = "Functional Time",
            .description = "Plays the clip backwards in short pieces, each piece reversed in place.",
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
} // namespace functional_fx_suite::effects::reverseedits
