#include "effects/warpchroma.hpp"
#include "core/mask.hpp"
#include "core/warp.hpp"
#include <algorithm>
#include <array>

namespace functional_fx_suite::effects::warpchroma
{
    enum class p : std::uint8_t
    {
        steps,
        center,
        from_z_dist,
        from_rotate,
        from_shift_x,
        from_shift_y,
        to_z_dist,
        to_rotate,
        to_shift_x,
        to_shift_y,
        warp_amount,
        brightness,
        color1,
        color2,
        color3,
        white_balance,
        wrap,
        filter,
        blur_matte,
        invert_matte,
        matte_use,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    // shifts are in xy units
    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "steps", "Steps").value(8).range(3, 100).slider(3, 20).digits(0),
        param::point(2, "center", "Center").value({0, 0, 0}),
        param::scalar(3, "from_z_dist", "From Z Dist").value(1.5).range(0.001, UNBOUNDED).slider(0.1, 10),
        param::scalar(4, "from_rotate", "From Rotate").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-90, 90),
        param::scalar(5, "from_shift_x", "From Shift X").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(6, "from_shift_y", "From Shift Y").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(7, "to_z_dist", "To Z Dist").value(1).range(0.001, UNBOUNDED).slider(0.1, 10),
        param::scalar(8, "to_rotate", "To Rotate").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-90, 90),
        param::scalar(9, "to_shift_x", "To Shift X").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(10, "to_shift_y", "To Shift Y").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(11, "warp_amount", "Warp Amount").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(12, "brightness", "Brightness").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::color(13, "color1", "Color1").value({1, 0, 0}),
        param::color(14, "color2", "Color2").value({0, 1, 0}),
        param::color(15, "color3", "Color3").value({0, 0, 1}),
        param::toggle(16, "white_balance", "White Balance"),
        param::choice(17, "wrap", "Wrap", "No|Tile|Reflect").value(0),
        param::toggle(18, "filter", "Filter"),
        param::scalar(19, "blur_matte", "Blur Matte").value(0).range(0, 1000).slider(0, 2),
        param::toggle(20, "invert_matte", "Invert Matte"),
        param::choice(21, "matte_use", "Matte Use", "Luma|Alpha"),
    };

    struct transform
    {
        core::warp::vec2 center;
        double turns;
        double z_dist;
        core::warp::vec2 shift;

        // rotate(xy - center) * z_dist + (center - shift), the xy this transform reads from
        [[nodiscard]] core::warp::vec2 apply(const core::warp::vec2& xy) const
        {
            const auto r = core::warp::rotate({xy[0] - center[0], xy[1] - center[1]}, turns);
            return {r[0] * z_dist + center[0] - shift[0], r[1] * z_dist + center[1] - shift[1]};
        }
    };

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const core::warp::vec2 center{v.vec(p::center)[0] * core::XY_PER_FRAME_UNIT,
                                      v.vec(p::center)[1] * core::XY_PER_FRAME_UNIT};
        const transform from{center,
                             v.scalar(p::from_rotate) / -360.0,
                             v.scalar(p::from_z_dist),
                             {v.scalar(p::from_shift_x), v.scalar(p::from_shift_y)}};
        const transform to{center,
                           v.scalar(p::to_rotate) / -360.0,
                           v.scalar(p::to_z_dist),
                           {v.scalar(p::to_shift_x), v.scalar(p::to_shift_y)}};
        const core::buffer matte =
            core::mask::build(args, v.choice(p::matte_use) == 1 ? core::mask::source::alpha : core::mask::source::luma,
                              v.scalar(p::blur_matte), v.toggle(p::invert_matte));
        const double amount = v.scalar(p::warp_amount);
        // each end blends from no warp to its transform by Warp Amount times the matte
        const auto end_field = [&](const transform& t) {
            return core::warp::field(args, [&](int x, int y, const core::warp::vec2& xy) {
                const auto q = t.apply(xy);
                const double k = amount * core::warp::matte(matte.view(), x, y);
                return core::warp::vec2{xy[0] + (q[0] - xy[0]) * k, xy[1] + (q[1] - xy[1]) * k};
            });
        };
        const core::buffer from_field = end_field(from);
        const core::buffer to_field = end_field(to);
        const std::array colors{v.vec(p::color1), v.vec(p::color2), v.vec(p::color3)};
        const auto weights = core::warp::spectrum(std::clamp(v.choice(p::steps), 3, 100), colors,
                                                  v.toggle(p::white_balance), v.scalar(p::brightness));
        const core::warp::source src(args.src, static_cast<core::sample::wrap>(std::clamp(v.choice(p::wrap), 0, 2)),
                                     v.toggle(p::filter));

        const core::image& dst = args.dst;
        core::parallel::for_range(dst.bounds.y0, dst.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                for (int x = dst.bounds.x0; x < dst.bounds.x1; ++x)
                {
                    const auto px = core::warp::chroma(src, core::warp::at(from_field.view(), x, y),
                                                       core::warp::at(to_field.view(), x, y),
                                                       core::warp::footprint(from_field.view(), x, y),
                                                       core::warp::footprint(to_field.view(), x, y), weights);
                    std::copy(px.begin(), px.end(), dst.at(x, y));
                }
            }
        });
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_warpchroma",
            .name = "f_warpchroma",
            .category = "Functional Distort",
            .description = "Splits the clip into spectral bands, warped from one transform (red) to another (blue).",
            .params = PARAMS,
            .mask_input = "Matte",
            .background_input = "",
            .time_varying = false,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::warpchroma
