#include "effects/blurmocurves.hpp"
#include "core/mask.hpp"
#include "core/warp.hpp"
#include <algorithm>
#include <array>

namespace functional_fx_suite::effects::blurmocurves
{
    enum class p : std::uint8_t
    {
        center,
        z_dist,
        rotate,
        shift_x,
        shift_y,
        shutter_duration,
        shutter_shift,
        exposure_bias,
        brightness,
        wrap,
        blur_res,
        subpixel,
        blur_only,
        mask_use,
        blur_mask,
        invert_mask,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    // Shift is in xy units. Blur Only off moves the image by the pose too; on, it only blurs along the motion
    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::point(1, "center", "Center").value({0, 0, 0}),
        param::scalar(2, "z_dist", "Z Dist").value(1).range(0.001, UNBOUNDED).slider(0.1, 5),
        param::scalar(3, "rotate", "Rotate").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-180, 180),
        param::scalar(4, "shift_x", "Shift X").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(5, "shift_y", "Shift Y").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(6, "shutter_duration", "Shutter Duration").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(7, "shutter_shift", "Shutter Shift").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(8, "exposure_bias", "Exposure Bias").value(0.5).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(9, "brightness", "Brightness").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::choice(10, "wrap", "Wrap", "No|Tile|Reflect"),
        param::choice(11, "blur_res", "Blur Res", "Full|Half|Quarter"),
        param::toggle(12, "subpixel", "Subpixel").value(1),
        param::toggle(16, "blur_only", "Blur Only"),
        param::choice(13, "mask_use", "Mask Use", "Luma|Alpha"),
        param::scalar(14, "blur_mask", "Blur Mask").value(0.05).range(0, 1000).slider(0, 2),
        param::toggle(15, "invert_mask", "Invert Mask"),
    };

    // the animated transform params at one moment
    struct pose
    {
        double turns;
        double z_dist;
        core::warp::vec2 origin; // center - shift
    };

    // the params' own curves `rel_frames` from now
    static pose pose_at(const core::render_args& args, const core::warp::vec2& center, double rel_frames)
    {
        const core::param_values& v =
            args.params_at ? args.params_at(args.time + rel_frames * args.frame_duration) : *args.params;
        return {v.scalar(p::rotate) / -360.0,
                v.scalar(p::z_dist),
                {center[0] - v.scalar(p::shift_x), center[1] - v.scalar(p::shift_y)}};
    }

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const core::warp::vec2 center{v.vec(p::center)[0] * core::XY_PER_FRAME_UNIT,
                                      v.vec(p::center)[1] * core::XY_PER_FRAME_UNIT};
        const double duration = v.scalar(p::shutter_duration);
        const double shift = v.scalar(p::shutter_shift);
        const pose from = pose_at(args, center, shift + 0.5 * duration);
        const pose to = pose_at(args, center, shift - 0.5 * duration);
        const double half_inv_z = 0.5 / v.scalar(p::z_dist);
        const double turns = v.scalar(p::rotate) / 360.0;
        const double bias = v.scalar(p::exposure_bias);
        const bool blur_only = v.toggle(p::blur_only);
        const auto brightness = v.scalar_f(p::brightness);
        const int res = v.choice(p::blur_res);
        const core::warp::source src(args.src, static_cast<core::sample::wrap>(v.choice(p::wrap)), false);
        const core::frame& f = args.geometry;

        const core::image& dst = args.dst;
        core::parallel::for_pixels(dst.bounds, [&](int x, int y) {
            const auto xy = core::warp::to_xy(f, x, y);
            const core::warp::vec2 c{xy[0] - center[0], xy[1] - center[1]};
            const auto a = core::warp::rotate(c, from.turns);
            const auto b = core::warp::rotate(c, to.turns);
            core::warp::vec2 xy0{};
            core::warp::vec2 xy1{};
            if (blur_only)
            {
                // half the path this pixel's content travels between the two poses, without applying either
                const double kf = from.z_dist * half_inv_z;
                const double kt = to.z_dist * half_inv_z;
                const auto dv =
                    core::warp::rotate({b[0] * kt - a[0] * kf + (to.origin[0] - from.origin[0]) * half_inv_z,
                                        b[1] * kt - a[1] * kf + (to.origin[1] - from.origin[1]) * half_inv_z},
                                       turns);
                xy0 = {xy[0] - dv[0], xy[1] - dv[1]};
                xy1 = {xy[0] + dv[0], xy[1] + dv[1]};
            }
            else
            {
                // where the pose at each end of the shutter samples the source
                xy0 = {a[0] * from.z_dist + from.origin[0], a[1] * from.z_dist + from.origin[1]};
                xy1 = {b[0] * to.z_dist + to.origin[0], b[1] * to.z_dist + to.origin[1]};
            }
            auto px =
                core::warp::line(src, core::warp::to_px(f, xy0), core::warp::to_px(f, xy1), 1.0 - bias, bias, res);
            float* d = dst.at(x, y);
            std::transform(px.begin(), px.end(), d, [brightness](float c) { return c * brightness; });
        });

        core::mask::apply(args, core::mask::luma_or_alpha(v.choice(p::mask_use)), v.scalar(p::blur_mask),
                          v.toggle(p::invert_mask));
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_blurmocurves",
            .name = "f_blurmocurves",
            .category = "Functional Blur",
            .description =
                "Moves, rotates and scales the clip by its Shift, Rotate and Z Dist, motion-blurred along the "
                "path their keyframes trace over the shutter; Blur Only keeps the blur without the move.",
            .params = PARAMS,
            .mask_input = "Mask",
            .background_input = "",
            .time_varying = true,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::blurmocurves
