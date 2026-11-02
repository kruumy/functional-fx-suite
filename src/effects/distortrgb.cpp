#include "effects/distortrgb.hpp"
#include "core/mask.hpp"
#include "core/warp.hpp"
#include <algorithm>
#include <array>

namespace functional_fx_suite::effects::distortrgb
{
    enum class p : std::uint8_t
    {
        amount,
        blur_lens,
        rotate_warp_dir,
        warp_red,
        warp_green,
        warp_blue,
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

    // Warp Alpha is fixed at 0, so alpha stays put
    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "amount", "Amount").value(1).range(-UNBOUNDED, UNBOUNDED).slider(-2, 2),
        param::scalar(2, "blur_lens", "Blur Lens").value(0.4).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(3, "rotate_warp_dir", "Rotate Warp Dir").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-180, 180),
        param::scalar(4, "warp_red", "Warp Red").value(0.5).range(-UNBOUNDED, UNBOUNDED).slider(-2, 2),
        param::scalar(5, "warp_green", "Warp Green").value(0.75).range(-UNBOUNDED, UNBOUNDED).slider(-2, 2),
        param::scalar(6, "warp_blue", "Warp Blue").value(1).range(-UNBOUNDED, UNBOUNDED).slider(-2, 2),
        param::scalar(7, "amount_rel_x", "Amount Rel X").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(8, "amount_rel_y", "Amount Rel Y").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::choice(9, "wrap", "Wrap", "No|Tile|Reflect").value(2),
        param::toggle(10, "filter", "Filter").value(1),
        param::scalar(11, "blur_matte", "Blur Matte").value(0).range(0, 1000).slider(0, 2),
        param::toggle(12, "invert_matte", "Invert Matte"),
        param::choice(13, "matte_use", "Matte Use", "Luma|Alpha"),
    };

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const double blur_lens = v.scalar(p::blur_lens);
        const core::buffer lens = core::warp::lens(args, blur_lens, v.scalar(p::amount) * (blur_lens + 0.1));
        const core::buffer matte =
            core::mask::build(args, v.choice(p::matte_use) == 1 ? core::mask::source::alpha : core::mask::source::luma,
                              v.scalar(p::blur_matte), v.toggle(p::invert_matte));
        const core::warp::vec2 rel{v.scalar(p::amount_rel_x), v.scalar(p::amount_rel_y)};
        const double turns = v.scalar(p::rotate_warp_dir) / 360.0;
        const core::warp::source src(args.src, static_cast<core::sample::wrap>(std::clamp(v.choice(p::wrap), 0, 2)),
                                     v.toggle(p::filter));

        // each color channel looks up the source at its own multiple of the offset
        const std::array amounts{v.scalar(p::warp_red), v.scalar(p::warp_green), v.scalar(p::warp_blue)};
        for (int c = 0; c < 3; ++c)
        {
            const core::buffer field = core::warp::field(args, [&](int x, int y, const core::warp::vec2& xy) {
                const auto g = core::warp::lens_gradient(lens.view(), x, y);
                const auto d = core::warp::rotate({rel[0] * g[0], rel[1] * g[1]}, turns);
                const double k = amounts[c] * core::warp::matte(matte.view(), x, y);
                return core::warp::vec2{xy[0] + k * d[0], xy[1] + k * d[1]};
            });
            const core::image& dst = args.dst;
            core::parallel::for_range(dst.bounds.y0, dst.bounds.y1, [&](int y_begin, int y_end) {
                for (int y = y_begin; y < y_end; ++y)
                {
                    for (int x = dst.bounds.x0; x < dst.bounds.x1; ++x)
                    {
                        dst.at(x, y)[c] =
                            src.at(core::warp::at(field.view(), x, y), core::warp::footprint(field.view(), x, y))[c];
                    }
                }
            });
        }
        const core::image& dst = args.dst;
        core::parallel::for_range(dst.bounds.y0, dst.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                for (int x = dst.bounds.x0; x < dst.bounds.x1; ++x)
                {
                    dst.at(x, y)[3] = args.src.bounds.contains(x, y) ? args.src.at(x, y)[3] : 0.0f;
                }
            }
        });
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_distortrgb",
            .name = "f_distortrgb",
            .category = "Functional Distort",
            .description = "Warps the red, green and blue channels by different amounts along a lens layer's gradient.",
            .params = PARAMS,
            .mask_input = "Matte",
            .background_input = "Lens",
            .time_varying = false,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::distortrgb
