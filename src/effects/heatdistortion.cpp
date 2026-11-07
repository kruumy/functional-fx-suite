#include "effects/heatdistortion.hpp"
#include "core/blur.hpp"
#include "core/mask.hpp"
#include "core/noise.hpp"
#include "core/warp.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace functional_fx_suite::effects::heatdistortion
{
    enum class p : std::uint8_t
    {
        distortion,
        diffusion,
        diffusion_bias,
        scale,
        rel_height,
        detail,
        rotation,
        speed,
        wind_direction,
        wind_speed,
        seed,
        wrap,
        filter,
        blur_matte,
        invert_matte,
        matte_use,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    // Diffusion blurs where the distortion is strong, as a blur width; Diffusion Bias adds to how much of the frame
    // it reaches. Distortion, Scale and Wind Speed are in frame widths (per second for the wind); angles in degrees,
    // counter-clockwise; Speed is noise changes per second
    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(2, "distortion", "Distortion").value(0.006).range(-UNBOUNDED, UNBOUNDED).slider(0, 0.05),
        param::scalar(16, "diffusion", "Diffusion").value(0.05).range(0, 1000).slider(0, 0.3),
        param::scalar(17, "diffusion_bias", "Diffusion Bias").value(0).range(-1, 1),
        param::scalar(3, "scale", "Scale").value(0.05).range(0.001, UNBOUNDED).slider(0.005, 0.5),
        param::scalar(4, "rel_height", "Rel Height").value(2).range(0.01, UNBOUNDED).slider(0.1, 10),
        param::scalar(5, "detail", "Detail").value(3).range(1, 8).digits(0),
        param::scalar(6, "rotation", "Rotation").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-180, 180),
        param::scalar(7, "speed", "Speed").value(2).range(-UNBOUNDED, UNBOUNDED).slider(0, 10),
        param::scalar(8, "wind_direction", "Wind Direction").value(90).range(-UNBOUNDED, UNBOUNDED).slider(-180, 180),
        param::scalar(9, "wind_speed", "Wind Speed").value(0.05).range(-UNBOUNDED, UNBOUNDED).slider(0, 0.5),
        param::scalar(10, "seed", "Seed").value(0.31).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::choice(11, "wrap", "Wrap", "No|Tile|Reflect").value(2),
        param::toggle(12, "filter", "Filter").value(1),
        param::scalar(13, "blur_matte", "Blur Matte").value(0).range(0, 1000).slider(0, 2),
        param::toggle(14, "invert_matte", "Invert Matte"),
        param::choice(15, "matte_use", "Matte Use", "Luma|Alpha"),
    };

    using core::warp::vec2;

    // the animated haze, in noise cells: two fbm values, one per axis, each with its own cache so it keeps its own
    // lattice cell
    struct field
    {
        std::uint32_t seed = 0;
        int octaves = 1;
        double z = 0;

        // displacement at q, [-1, 1] on each axis
        [[nodiscard]] vec2 at(std::array<core::noise::cached_fbm_3d, 2>& cache, vec2 q) const
        {
            constexpr double GAIN = 0.5;
            constexpr std::uint32_t OTHER_AXIS = 101;
            return {2.0 * cache[0].at(seed, q[0], q[1], z, octaves, GAIN) - 1.0,
                    2.0 * cache[1].at(seed + OTHER_AXIS, q[0], q[1], z, octaves, GAIN) - 1.0};
        }
    };

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const double scale = v.scalar(p::scale);
        const double rel_height = v.scalar(p::rel_height);
        // Rotation's cos and sin, once rather than per pixel
        const double angle = v.scalar(p::rotation) * std::numbers::pi / 180.0;
        const double cos_a = std::cos(angle);
        const double sin_a = std::sin(angle);
        const double wind_angle = v.scalar(p::wind_direction) * std::numbers::pi / 180.0;
        const double wind = v.scalar(p::wind_speed) * args.time;
        const vec2 drift{wind * std::cos(wind_angle), wind * std::sin(wind_angle)};
        const field noise{
            .seed = core::noise::seed_bits(v.scalar(p::seed)),
            .octaves = v.whole(p::detail),
            .z = v.scalar(p::speed) * args.time,
        };
        const double amount = v.scalar(p::distortion) * core::XY_PER_FRAME_UNIT;
        const core::buffer matte = core::mask::build(args, core::mask::luma_or_alpha(v.choice(p::matte_use)),
                                                     v.scalar(p::blur_matte), v.toggle(p::invert_matte));

        const double diffusion = core::blur::amount_px(v.scalar(p::diffusion), args.geometry);
        const double bias = v.scalar(p::diffusion_bias);
        // how much of the blurred frame each pixel reads, filled alongside the field
        const core::buffer blend =
            diffusion > 0.0 ? core::buffer(core::rect{args.dst.bounds.x0, args.dst.bounds.y0, args.dst.bounds.x1 + 1,
                                                      args.dst.bounds.y1 + 1},
                                           1)
                            : core::buffer{};
        const core::image& bl = blend.view();

        const core::buffer warp = core::warp::field(args, [&](int x, int y, const vec2& xy) {
            // the pattern drifts with the wind, turned by Rotation and stretched by Rel Height
            const double ux = xy[0] / core::XY_PER_FRAME_UNIT - drift[0];
            const double uy = xy[1] / core::XY_PER_FRAME_UNIT - drift[1];
            const vec2 n{cos_a * ux + sin_a * uy, cos_a * uy - sin_a * ux};
            // each worker thread keeps its own caches; they only hold hashes, so a stale one just misses
            thread_local std::array<core::noise::cached_fbm_3d, 2> cache;
            const vec2 r = noise.at(cache, {n[0] / scale, n[1] / (scale * rel_height)});
            const vec2 d{cos_a * r[0] - sin_a * r[1], sin_a * r[0] + cos_a * r[1]};
            const double m = core::warp::matte(matte.view(), x, y);
            if (bl.data != nullptr)
            {
                *bl.at(x, y) = static_cast<float>(std::clamp((2.0 * std::hypot(r[0], r[1])) + bias, 0.0, 1.0) * m);
            }
            const double k = amount * m;
            return vec2{xy[0] + k * d[0], xy[1] + k * d[1]};
        });
        const auto wrap = static_cast<core::sample::wrap>(v.choice(p::wrap));
        const core::warp::source src(args.src, wrap, v.toggle(p::filter));
        if (diffusion <= 0.0)
        {
            core::warp::apply(args, warp.view(), src);
            return;
        }
        // a variable blur as a mix of the sharp lookup and the same lookup into one blurred copy
        const core::buffer blurred = core::buffer::copy_of(args.src);
        core::blur::gaussian(blurred.view(), diffusion, diffusion, core::blur::edge::reflect);
        const core::image& dst = args.dst;
        core::parallel::for_pixels(dst.bounds, [&](int x, int y) {
            const vec2 at = core::warp::at(warp.view(), x, y);
            core::sample::pixel px = src.at(at, core::warp::footprint(warp.view(), x, y));
            const float w = *bl.at(x, y);
            if (w > 0.0f)
            {
                const core::sample::pixel soft = core::sample::bilinear(blurred.view(), at[0], at[1], wrap);
                for (std::size_t c = 0; c < 4; ++c)
                {
                    px[c] += (soft[c] - px[c]) * w;
                }
            }
            std::copy(px.begin(), px.end(), dst.at(x, y));
        });
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_heatdistortion",
            .name = "f_heatdistortion",
            .category = "Functional Distort",
            .description = "Ripples and softens the clip with drifting noise, like rising heat haze.",
            .params = PARAMS,
            .mask_input = "Matte",
            .background_input = "",
            .time_varying = true,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::heatdistortion
