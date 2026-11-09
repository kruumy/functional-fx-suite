#include "effects/motionwarp.hpp"
#include "core/blur.hpp"
#include "core/color.hpp"
#include "core/edges.hpp"
#include "core/mask.hpp"
#include "core/retime.hpp"
#include "core/warp.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace functional_fx_suite::effects::motionwarp
{
    enum class p : std::uint8_t
    {
        delay_frames,
        amount,
        direction,
        smoothness,
        threshold,
        mode,
        wrap,
        filter,
        blur_matte,
        invert_matte,
        matte_use,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    // Amount 1 repeats the motion since Delay Frames ago; Smoothness is a blur width, Threshold in frame widths
    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "delay_frames", "Delay Frames").value(1).range(-UNBOUNDED, UNBOUNDED).slider(1, 10).digits(0),
        param::scalar(2, "amount", "Amount").value(2).range(-UNBOUNDED, UNBOUNDED).slider(-10, 10),
        param::scalar(3, "direction", "Direction").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-180, 180),
        param::scalar(4, "smoothness", "Smoothness").value(0.1).range(0.001, 2).slider(0.01, 0.5),
        param::scalar(5, "threshold", "Threshold").value(0).range(0, UNBOUNDED).slider(0, 0.01).digits(4),
        param::choice(6, "mode", "Mode", "Push|Smear"),
        param::choice(7, "wrap", "Wrap", "No|Tile|Reflect").value(2),
        param::toggle(8, "filter", "Filter").value(1),
        param::scalar(9, "blur_matte", "Blur Matte").value(0).range(0, 1000).slider(0, 2),
        param::toggle(10, "invert_matte", "Invert Matte"),
        param::choice(11, "matte_use", "Matte Use", "Luma|Alpha"),
    };

    enum class mode : std::uint8_t
    {
        push,
        smear
    };

    using core::warp::vec2;

    static std::vector<int> frame_offsets(const core::param_values& v, int /*frame*/)
    {
        return core::retime::offsets(-v.whole(p::delay_frames));
    }

    // optical flow from `before` to the source in pixels (y down), 2 channels over the source bounds, or empty
    static core::buffer motion(const core::render_args& args, const core::image& before, double blur)
    {
        const core::rect& b = args.src.bounds;
        if (before.data == nullptr || b.empty())
        {
            return {};
        }
        // the source's luma, then the earlier frame's, which is transparent past its bounds
        const core::buffer lumas(b, 2);
        core::parallel::for_pixels(b, [&](int x, int y) {
            const float* s = args.src.at(x, y);
            const float* e = before.bounds.contains(x, y) ? before.at(x, y) : nullptr;
            float* l = lumas.view().at(x, y);
            l[0] = core::color::luma(s[0], s[1], s[2]);
            l[1] = e != nullptr ? core::color::luma(e[0], e[1], e[2]) : 0.0f;
        });
        core::buffer flow(b, 2);
        const core::buffer warped(b, 1);
        const core::buffer tensor(b, 3);
        const core::buffer mismatch(b, 2);
        // coarse to fine Lucas-Kanade: each level blurs twice as much as the next and measures what the coarser left
        constexpr int LEVELS = 4;
        // a cut or flash reads as motion past any level's reach; the coarsest reach bounds it
        const double reach = blur * std::exp2(LEVELS - 1);
        for (int l = LEVELS - 1; l >= 0; --l)
        {
            const double r = blur * std::exp2(l);
            const core::buffer level = core::buffer::copy_of(lumas.view());
            const core::image& lv = level.view();
            core::blur::gaussian(lv, r, r, core::blur::edge::reflect);
            core::parallel::for_pixels(b, [&](int x, int y) {
                const float* f = flow.view().at(x, y);
                *warped.view().at(x, y) =
                    core::sample::bilinear(lv, x + 0.5 - f[0], y + 0.5 - f[1], core::sample::wrap::reflect)[1];
            });
            core::parallel::for_pixels(b, [&](int x, int y) {
                const auto [nx, ny] = core::edges::gradient(lv, x, y, 0);
                const auto [wx, wy] = core::edges::gradient(warped.view(), x, y, 0);
                // both gradients span two pixels, y up: their mean per pixel, y down
                const float ix = (nx + wx) / 4.0f;
                const float iy = -(ny + wy) / 4.0f;
                const float it = lv.at(x, y)[0] - *warped.view().at(x, y);
                float* t = tensor.view().at(x, y);
                t[0] = ix * ix;
                t[1] = ix * iy;
                t[2] = iy * iy;
                float* m = mismatch.view().at(x, y);
                m[0] = ix * it;
                m[1] = iy * it;
            });
            core::blur::gaussian(tensor.view(), r, r, core::blur::edge::reflect);
            core::blur::gaussian(mismatch.view(), r, r, core::blur::edge::reflect);

            // the regularizer is the squared gradient of a faint edge after the blur, so flat areas read as still
            constexpr double FAINT_EDGE = 0.02;
            const double lambda = std::pow(FAINT_EDGE / std::max(r, 1.0), 2.0);
            core::parallel::for_pixels(b, [&](int x, int y) {
                const float* t = tensor.view().at(x, y);
                const float* m = mismatch.view().at(x, y);
                float* f = flow.view().at(x, y);
                const double a = t[0] + lambda;
                const double c = t[2] + lambda;
                const double det = (a * c) - (static_cast<double>(t[1]) * t[1]);
                const double u = f[0] - (((c * m[0]) - (t[1] * m[1])) / det);
                const double w = f[1] - (((a * m[1]) - (t[1] * m[0])) / det);
                const double k = std::min(1.0, reach / std::max(std::hypot(u, w), 1e-9));
                f[0] = static_cast<float>(u * k);
                f[1] = static_cast<float>(w * k);
            });
        }
        return flow;
    }

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const int n = v.whole(p::delay_frames);
        const core::buffer before = n != 0 ? args.frame_at(-n) : core::buffer{};
        const core::buffer flow =
            motion(args, before.view(), core::blur::amount_px(v.scalar(p::smoothness), args.geometry));
        const core::image& fl = flow.view();
        const core::image& dst = args.dst;
        if (fl.data == nullptr)
        {
            // nothing to measure motion against: the source as is
            core::parallel::for_pixels(dst.bounds, [&](int x, int y) {
                float* d = dst.at(x, y);
                if (args.src.bounds.contains(x, y))
                {
                    std::copy_n(args.src.at(x, y), 4, d);
                }
                else
                {
                    std::fill_n(d, 4, 0.0f);
                }
            });
            return;
        }
        const core::buffer matte = core::mask::build(args, core::mask::luma_or_alpha(v.choice(p::matte_use)),
                                                     v.scalar(p::blur_matte), v.toggle(p::invert_matte));
        const double amount = v.scalar(p::amount);
        const double turns = v.scalar(p::direction) / 360.0;
        const double threshold = v.scalar(p::threshold) * core::XY_PER_FRAME_UNIT;
        const double xy_per_px = core::XY_PER_FRAME_UNIT / args.geometry.width;

        const core::buffer warp = core::warp::field(args, [&](int x, int y, const vec2& xy) {
            const float* f =
                fl.at(std::clamp(x, fl.bounds.x0, fl.bounds.x1 - 1), std::clamp(y, fl.bounds.y0, fl.bounds.y1 - 1));
            const vec2 m{f[0] * xy_per_px, -f[1] * xy_per_px};
            const double len = std::hypot(m[0], m[1]);
            // Threshold shrinks every motion by itself, so the cut-off has no hard edge
            const double keep = len > threshold ? (len - threshold) / len : 0.0;
            const vec2 d = core::warp::rotate(m, turns);
            const double k = amount * keep * core::warp::matte(matte.view(), x, y);
            return vec2{xy[0] - (k * d[0]), xy[1] - (k * d[1])};
        });
        const core::warp::source src(args.src, static_cast<core::sample::wrap>(v.choice(p::wrap)), v.toggle(p::filter));
        if (static_cast<mode>(v.choice(p::mode)) == mode::push)
        {
            core::warp::apply(args, warp.view(), src);
            return;
        }
        // Smear averages ahead along the motion, so moving things leave fading trails behind them
        core::parallel::for_pixels(dst.bounds, [&](int x, int y) {
            const vec2 c{x + 0.5, y + 0.5};
            const vec2 back = core::warp::at(warp.view(), x, y);
            const core::sample::pixel px =
                core::warp::line(src, c, {(2.0 * c[0]) - back[0], (2.0 * c[1]) - back[1]}, 1.0, 0.0, 0);
            std::copy(px.begin(), px.end(), dst.at(x, y));
        });
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_motionwarp",
            .name = "f_motionwarp",
            .category = "Functional Distort",
            .description = "Pushes or smears the clip along its own motion since an earlier frame.",
            .params = PARAMS,
            .mask_input = "Matte",
            .background_input = "",
            .time_varying = false,
            .reads_outside = true,
            .render = render,
            .frame_offsets = frame_offsets,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::motionwarp
