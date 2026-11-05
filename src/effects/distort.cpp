#include "effects/distort.hpp"
#include "core/mask.hpp"
#include "core/warp.hpp"
#include <array>

namespace functional_fx_suite::effects::distort
{
    enum class p : std::uint8_t
    {
        amount,
        fine,
        blur_lens,
        rotate_warp_dir,
        amount_rel_x,
        amount_rel_y,
        wrap,
        filter,
        blur_matte,
        invert_matte,
        matte_use,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "amount", "Amount").value(1).range(-UNBOUNDED, UNBOUNDED).slider(-2, 2),
        param::toggle(2, "fine", "Fine"),
        param::scalar(3, "blur_lens", "Blur Lens").value(0.4).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(4, "rotate_warp_dir", "Rotate Warp Dir").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-180, 180),
        param::scalar(5, "amount_rel_x", "Amount Rel X").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(6, "amount_rel_y", "Amount Rel Y").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::choice(7, "wrap", "Wrap", "No|Tile|Reflect").value(2),
        param::toggle(8, "filter", "Filter").value(1),
        param::scalar(9, "blur_matte", "Blur Matte").value(0).range(0, 1000).slider(0, 2),
        param::toggle(10, "invert_matte", "Invert Matte"),
        param::choice(11, "matte_use", "Matte Use", "Luma|Alpha"),
    };

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const double blur_lens = v.scalar(p::blur_lens);
        const core::buffer lens = core::warp::lens(args, blur_lens, v.scalar(p::amount) * (blur_lens + 0.1));
        const core::buffer matte = core::mask::build(args, core::mask::luma_or_alpha(v.choice(p::matte_use)),
                                                     v.scalar(p::blur_matte), v.toggle(p::invert_matte));
        const double fine = v.toggle(p::fine) ? 0.01 : 1.0;
        const core::warp::vec2 rel{v.scalar(p::amount_rel_x), v.scalar(p::amount_rel_y)};
        const double turns = v.scalar(p::rotate_warp_dir) / 360.0;

        const core::buffer field = core::warp::field(args, [&](int x, int y, const core::warp::vec2& xy) {
            const auto g = core::warp::lens_gradient(lens.view(), x, y);
            const auto d = core::warp::rotate({rel[0] * g[0], rel[1] * g[1]}, turns);
            const double k = fine * core::warp::matte(matte.view(), x, y);
            return core::warp::vec2{xy[0] + k * d[0], xy[1] + k * d[1]};
        });
        const core::warp::source src(args.src, static_cast<core::sample::wrap>(v.choice(p::wrap)), v.toggle(p::filter));
        core::warp::apply(args, field.view(), src);
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_distort",
            .name = "f_distort",
            .category = "Functional Distort",
            .description = "Warps the clip along the brightness gradient of a lens layer, like looking through glass.",
            .params = PARAMS,
            .mask_input = "Matte",
            .background_input = "Lens",
            .time_varying = false,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::distort
