#include "effects/warpfisheye.hpp"
#include "core/mask.hpp"
#include "core/warp.hpp"
#include <algorithm>
#include <array>

namespace functional_fx_suite::effects::warpfisheye
{
    enum class p : std::uint8_t
    {
        amount,
        center,
        z_dist,
        rotate,
        shift_orig_x,
        shift_orig_y,
        wrap,
        filter,
        blur_matte,
        invert_matte,
        matte_use,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    // Shift Orig is in xy units
    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "amount", "Amount").value(1).range(-UNBOUNDED, UNBOUNDED).slider(-10, 10),
        param::point(2, "center", "Center").value({0, 0, 0}),
        param::scalar(3, "z_dist", "Z Dist").value(1).range(0.001, UNBOUNDED).slider(0.1, 2),
        param::scalar(4, "rotate", "Rotate").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-180, 180),
        param::scalar(5, "shift_orig_x", "Shift Orig X").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(6, "shift_orig_y", "Shift Orig Y").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::choice(7, "wrap", "Wrap", "No|Tile|Reflect").value(2),
        param::toggle(8, "filter", "Filter").value(1),
        param::scalar(9, "blur_matte", "Blur Matte").value(0).range(0, 1000).slider(0, 2),
        param::toggle(10, "invert_matte", "Invert Matte"),
        param::choice(11, "matte_use", "Matte Use", "Luma|Alpha"),
    };

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const core::warp::vec2 center{v.vec(p::center)[0] * core::XY_PER_FRAME_UNIT,
                                      v.vec(p::center)[1] * core::XY_PER_FRAME_UNIT};
        const double amount = v.scalar(p::amount);
        // positive amounts also zoom out so the frame edges stay near the edges
        const double zoom = v.scalar(p::z_dist) / (1.0 + std::max(0.0, amount));
        const double turns = v.scalar(p::rotate) / -360.0;
        const core::warp::vec2 origin{center[0] - v.scalar(p::shift_orig_x), center[1] - v.scalar(p::shift_orig_y)};
        const core::buffer matte = core::mask::build(args, core::mask::luma_or_alpha(v.choice(p::matte_use)),
                                                     v.scalar(p::blur_matte), v.toggle(p::invert_matte));

        const core::buffer field = core::warp::field(args, [&](int x, int y, const core::warp::vec2& xy) {
            const core::warp::vec2 d{xy[0] - center[0], xy[1] - center[1]};
            const double k = (1.0 + (d[0] * d[0] + d[1] * d[1]) * amount) * zoom;
            const auto r = core::warp::rotate({d[0] * k, d[1] * k}, turns);
            const double m = core::warp::matte(matte.view(), x, y);
            return core::warp::vec2{xy[0] + (r[0] + origin[0] - xy[0]) * m, xy[1] + (r[1] + origin[1] - xy[1]) * m};
        });
        const core::warp::source src(args.src, static_cast<core::sample::wrap>(v.choice(p::wrap)), v.toggle(p::filter));
        core::warp::apply(args, field.view(), src);
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_warpfisheye",
            .name = "f_warpfisheye",
            .category = "Functional Distort",
            .description = "Expands the centre of the clip as if seen through a fish-eye lens.",
            .params = PARAMS,
            .mask_input = "Matte",
            .background_input = "",
            .time_varying = false,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::warpfisheye
