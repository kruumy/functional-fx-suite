#include "effects/jitterframes.hpp"
#include "core/noise.hpp"
#include "core/retime.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace functional_fx_suite::effects::jitterframes
{
    enum class p : std::uint8_t
    {
        edit_frame_length,
        jitter_frame_dist,
        seed,
        mask_use,
        blur_mask,
        invert_mask,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "edit_frame_length", "Edit Frame Length").value(1).range(1, UNBOUNDED).slider(1, 30).digits(0),
        param::scalar(2, "jitter_frame_dist", "Jitter Frame Dist")
            .value(10)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 100),
        param::scalar(3, "seed", "Seed").value(0.123).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::choice(4, "mask_use", "Mask Use", "Luma|Alpha"),
        param::scalar(5, "blur_mask", "Blur Mask").value(0.05).range(0, 1000).slider(0, 2),
        param::toggle(6, "invert_mask", "Invert Mask"),
    };

    // each block of Edit Frame Length frames jumps forward by its own random 0 .. Jitter Frame Dist frames
    static int offset(const core::param_values& v, int frame)
    {
        const int length = static_cast<int>(std::lround(std::clamp(v.scalar(p::edit_frame_length), 1.0, UNBOUNDED)));
        const int cut = core::retime::block_start(frame, length, 0);
        const double r = core::noise::unit(
            core::noise::combine(core::noise::seed_bits(v.scalar(p::seed)), static_cast<std::uint32_t>(cut)));
        return static_cast<int>(
            std::floor(r * (std::clamp(v.scalar(p::jitter_frame_dist), -UNBOUNDED, UNBOUNDED) + 0.99)));
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
            .match_name = "f_jitterframes",
            .name = "f_jitterframes",
            .category = "Functional Time",
            .description = "Jumps the clip randomly ahead in time, every few frames.",
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
} // namespace functional_fx_suite::effects::jitterframes
