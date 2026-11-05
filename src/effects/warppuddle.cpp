#include "effects/warppuddle.hpp"
#include "core/mask.hpp"
#include "core/warp.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace functional_fx_suite::effects::warppuddle
{
    enum class p : std::uint8_t
    {
        amplitude,
        frequency,
        rel_height,
        rotate_puddle,
        center,
        phase_start,
        phase_speed,
        inner_radius,
        inner_softness,
        outer_radius,
        outer_softness,
        z_dist,
        wrap,
        filter,
        blur_matte,
        invert_matte,
        matte_use,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    // radii and softness are in xy units
    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "amplitude", "Amplitude").value(0.1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(2, "frequency", "Frequency").value(8).range(0.01, UNBOUNDED).slider(1, 50),
        param::scalar(3, "rel_height", "Rel Height").value(0.75).range(0.01, UNBOUNDED).slider(0.1, 5),
        param::scalar(4, "rotate_puddle", "Rotate Puddle").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-180, 180),
        param::point(5, "center", "Center").value({0, 0, 0}),
        param::scalar(6, "phase_start", "Phase Start").value(0).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(7, "phase_speed", "Phase Speed").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(8, "inner_radius", "Inner Radius").value(0).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(9, "inner_softness", "Inner Softness").value(0.1).range(0.0056, UNBOUNDED).slider(0.0056, 1),
        param::scalar(10, "outer_radius", "Outer Radius").value(1.4).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(11, "outer_softness", "Outer Softness").value(0.42).range(0.0056, UNBOUNDED).slider(0.0056, 1),
        param::scalar(12, "z_dist", "Z Dist").value(1).range(0.001, UNBOUNDED).slider(0.1, 2),
        param::choice(13, "wrap", "Wrap", "No|Tile|Reflect").value(2),
        param::toggle(14, "filter", "Filter").value(1),
        param::scalar(15, "blur_matte", "Blur Matte").value(0).range(0, 1000).slider(0, 2),
        param::toggle(16, "invert_matte", "Invert Matte"),
        param::choice(17, "matte_use", "Matte Use", "Luma|Alpha"),
    };

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const core::warp::vec2 center{v.vec(p::center)[0] * core::XY_PER_FRAME_UNIT,
                                      v.vec(p::center)[1] * core::XY_PER_FRAME_UNIT};
        const double phase = v.scalar(p::phase_start) + v.scalar(p::phase_speed) * args.time;
        const double turns = v.scalar(p::rotate_puddle) / -360.0;
        const double z = v.scalar(p::z_dist);
        const double inv_height = 1.0 / v.scalar(p::rel_height);
        const double freq = v.scalar(p::frequency);
        const double amplitude = v.scalar(p::amplitude);
        const double inner_soft = v.scalar(p::inner_softness);
        const double outer_soft = v.scalar(p::outer_softness);
        const double inner_start = v.scalar(p::inner_radius) - 0.5 * inner_soft;
        const double outer_end = v.scalar(p::outer_radius) + 0.5 * outer_soft;
        const core::buffer matte = core::mask::build(args, core::mask::luma_or_alpha(v.choice(p::matte_use)),
                                                     v.scalar(p::blur_matte), v.toggle(p::invert_matte));

        const core::buffer field = core::warp::field(args, [&](int x, int y, const core::warp::vec2& xy) {
            const core::warp::vec2 d{xy[0] - center[0], xy[1] - center[1]};
            const auto r = core::warp::rotate(d, turns);
            const double dist = std::hypot(r[0] * z, r[1] * z * inv_height);
            // waves fade in across the inner softness band and out across the outer one (ncos ramp)
            const double ramp = std::clamp(
                std::min((dist - inner_start) * (0.5 / inner_soft), (outer_end - dist) * (0.5 / outer_soft)), 0.0, 0.5);
            const double fade = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * ramp);
            const double len = std::hypot(d[0], d[1]);
            const double wave = std::sin(2.0 * std::numbers::pi * (freq * dist - phase)) * amplitude * fade *
                                core::warp::matte(matte.view(), x, y);
            const double k = len > 0.0 ? wave / len : 0.0;
            return core::warp::vec2{d[0] * k + xy[0] * z, d[1] * k + xy[1] * z};
        });
        const core::warp::source src(args.src, static_cast<core::sample::wrap>(v.choice(p::wrap)), v.toggle(p::filter));
        core::warp::apply(args, field.view(), src);
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_warppuddle",
            .name = "f_warppuddle",
            .category = "Functional Distort",
            .description = "Warps the clip by concentric waves that move outwards from a centre over time.",
            .params = PARAMS,
            .mask_input = "Matte",
            .background_input = "",
            .time_varying = true,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::warppuddle
