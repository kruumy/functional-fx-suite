#include "effects/distortblur.hpp"
#include "core/mask.hpp"
#include "core/warp.hpp"
#include <algorithm>
#include <array>

namespace functional_fx_suite::effects::distortblur
{
    enum class p : std::uint8_t
    {
        blur_amount,
        warp_amount,
        blur_lens,
        rotate_blur_dir,
        amount_rel_x,
        amount_rel_y,
        wrap,
        subpixel,
        blur_matte,
        invert_matte,
        matte_use,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "blur_amount", "Blur Amount").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(2, "warp_amount", "Warp Amount").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-2, 2),
        param::scalar(3, "blur_lens", "Blur Lens").value(0.4).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(4, "rotate_blur_dir", "Rotate Blur Dir").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-180, 180),
        param::scalar(5, "amount_rel_x", "Amount Rel X").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(6, "amount_rel_y", "Amount Rel Y").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::choice(7, "wrap", "Wrap", "No|Tile|Reflect").value(2),
        param::toggle(8, "subpixel", "Subpixel").value(1),
        param::scalar(9, "blur_matte", "Blur Matte").value(0).range(0, 1000).slider(0, 2),
        param::toggle(10, "invert_matte", "Invert Matte"),
        param::choice(11, "matte_use", "Matte Use", "Luma|Alpha"),
    };

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const double blur_lens = v.scalar(p::blur_lens);
        const core::buffer lens = core::warp::lens(args, blur_lens, blur_lens + 0.1);
        const core::buffer matte = core::mask::build(args, core::mask::luma_or_alpha(v.choice(p::matte_use)),
                                                     v.scalar(p::blur_matte), v.toggle(p::invert_matte));
        const core::warp::vec2 rel{v.scalar(p::amount_rel_x), v.scalar(p::amount_rel_y)};
        const double turns = v.scalar(p::rotate_blur_dir) / 360.0;
        // the blur runs along the gradient from warp - blur / 2 to warp + blur / 2
        const double warp = v.scalar(p::warp_amount);
        const double half_blur = 0.5 * v.scalar(p::blur_amount);
        const core::warp::source src(args.src, static_cast<core::sample::wrap>(v.choice(p::wrap)), false);
        const core::frame& f = args.geometry;

        const core::image& dst = args.dst;
        core::parallel::for_pixels(dst.bounds, [&](int x, int y) {
            const auto xy = core::warp::to_xy(f, x, y);
            const auto g0 = core::warp::lens_gradient(lens.view(), x, y);
            auto g = core::warp::rotate({rel[0] * g0[0], rel[1] * g0[1]}, turns);
            const double m = core::warp::matte(matte.view(), x, y);
            g = {g[0] * m, g[1] * m};
            const auto p0 =
                core::warp::to_px(f, {xy[0] + g[0] * (warp - half_blur), xy[1] + g[1] * (warp - half_blur)});
            const auto p1 =
                core::warp::to_px(f, {xy[0] + g[0] * (warp + half_blur), xy[1] + g[1] * (warp + half_blur)});
            const auto px = core::warp::line(src, p0, p1, 1.0, 1.0, 0);
            std::copy(px.begin(), px.end(), dst.at(x, y));
        });
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_distortblur",
            .name = "f_distortblur",
            .category = "Functional Distort",
            .description = "Blurs the clip along the brightness gradient of a lens layer, with an optional warp.",
            .params = PARAMS,
            .mask_input = "Matte",
            .background_input = "Lens",
            .time_varying = false,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::distortblur
