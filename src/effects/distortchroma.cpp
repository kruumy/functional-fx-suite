#include "effects/distortchroma.hpp"
#include "core/mask.hpp"
#include "core/warp.hpp"
#include <algorithm>
#include <array>

namespace functional_fx_suite::effects::distortchroma
{
    enum class p : std::uint8_t
    {
        amount,
        blur_lens,
        rotate_warp_dir,
        warp_red,
        warp_blue,
        steps,
        color1,
        color2,
        color3,
        white_balance,
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
        param::scalar(2, "blur_lens", "Blur Lens").value(0.4).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(3, "rotate_warp_dir", "Rotate Warp Dir").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-180, 180),
        param::scalar(4, "warp_red", "Warp Red").value(0.5).range(-UNBOUNDED, UNBOUNDED).slider(-2, 2),
        param::scalar(5, "warp_blue", "Warp Blue").value(1).range(-UNBOUNDED, UNBOUNDED).slider(-2, 2),
        param::scalar(6, "steps", "Steps").value(8).range(3, 100).slider(3, 20).digits(0),
        param::color(7, "color1", "Color1").value({1, 0, 0}),
        param::color(8, "color2", "Color2").value({0, 1, 0}),
        param::color(9, "color3", "Color3").value({0, 0, 1}),
        param::toggle(10, "white_balance", "White Balance"),
        param::scalar(11, "amount_rel_x", "Amount Rel X").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(12, "amount_rel_y", "Amount Rel Y").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::choice(13, "wrap", "Wrap", "No|Tile|Reflect").value(2),
        param::toggle(14, "filter", "Filter").value(1),
        param::scalar(15, "blur_matte", "Blur Matte").value(0).range(0, 1000).slider(0, 2),
        param::toggle(16, "invert_matte", "Invert Matte"),
        param::choice(17, "matte_use", "Matte Use", "Luma|Alpha"),
    };

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const double blur_lens = v.scalar(p::blur_lens);
        const core::buffer lens = core::warp::lens(args, blur_lens, v.scalar(p::amount) * (blur_lens + 0.1));
        const core::buffer matte = core::mask::build(args, core::mask::luma_or_alpha(v.choice(p::matte_use)),
                                                     v.scalar(p::blur_matte), v.toggle(p::invert_matte));
        const core::warp::vec2 rel{v.scalar(p::amount_rel_x), v.scalar(p::amount_rel_y)};
        const double turns = v.scalar(p::rotate_warp_dir) / 360.0;
        const auto offset = [&](int x, int y) {
            const auto g = core::warp::lens_gradient(lens.view(), x, y);
            const auto d = core::warp::rotate({rel[0] * g[0], rel[1] * g[1]}, turns);
            const double m = core::warp::matte(matte.view(), x, y);
            return core::warp::vec2{d[0] * m, d[1] * m};
        };
        // red end of the spectrum at Warp Red times the offset, blue end at Warp Blue times it
        const auto end_field = [&](double k) {
            return core::warp::field(args, [&](int x, int y, const core::warp::vec2& xy) {
                const auto d = offset(x, y);
                return core::warp::vec2{xy[0] + k * d[0], xy[1] + k * d[1]};
            });
        };
        const core::buffer from = end_field(v.scalar(p::warp_red));
        const core::buffer to = end_field(v.scalar(p::warp_blue));
        const std::array colors{v.vec(p::color1), v.vec(p::color2), v.vec(p::color3)};
        const auto weights = core::warp::spectrum(v.whole(p::steps), colors, v.toggle(p::white_balance), 1.0);
        const core::warp::source src(args.src, static_cast<core::sample::wrap>(v.choice(p::wrap)), v.toggle(p::filter));

        const core::image& dst = args.dst;
        core::parallel::for_pixels(dst.bounds, [&](int x, int y) {
            const auto px = core::warp::chroma(src, core::warp::at(from.view(), x, y), core::warp::at(to.view(), x, y),
                                               core::warp::footprint(from.view(), x, y),
                                               core::warp::footprint(to.view(), x, y), weights);
            std::copy(px.begin(), px.end(), dst.at(x, y));
        });
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_distortchroma",
            .name = "f_distortchroma",
            .category = "Functional Distort",
            .description = "Warps the clip along a lens layer's gradient by different amounts across the spectrum.",
            .params = PARAMS,
            .mask_input = "Matte",
            .background_input = "Lens",
            .time_varying = false,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::distortchroma
