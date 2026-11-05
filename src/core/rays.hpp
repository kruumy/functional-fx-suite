#pragma once

#include "core/buffer.hpp"
#include "core/noise.hpp"
#include "core/render_args.hpp"
#include <array>
#include <cstdint>

namespace functional_fx_suite::core::rays
{
    using rgb = std::array<double, 3>;

    // a ray path from each pixel toward (or past) the centre, with a piecewise-linear brightness profile
    struct ray_shape
    {
        double reverse = 0; // relative length extending away from the centre
        double bias = 0;    // bias_outer_bright
        double brightness = 3;
    };

    struct path
    {
        double cx = 0;
        double cy = 0;
        rgb lengths{};
        ray_shape shape;
        int res_level = 0; // Rays Res: full, half, quarter
    };

    // per-channel lengths; dark rays invert the relative channel lengths at the same
    // overall magnitude
    [[nodiscard]] rgb channel_lengths(double base, const rgb& rel, bool dark);

    // shimmer noise multiplying the ray generator: 1 + amp/2 - amp * noise((xy + shift) * freq)
    struct shimmer
    {
        double amp = 0;
        double freq = 1;
        std::uint32_t seed = 0;
        double x = 0; // shift + time * speed
        double y = 0;
        [[nodiscard]] float at(double nx, double ny) const;
    };

    // the path from the params f_rays and f_edgerays both declare, `P` being the effect's param enum;
    // `brightness` is the value of their brightness (or darkness) param
    template <class P> [[nodiscard]] path read_path(const param_values& v, bool dark, double brightness)
    {
        path s;
        s.cx = v.vec(P::center)[0];
        s.cy = v.vec(P::center)[1];
        s.lengths =
            channel_lengths(v.scalar(P::rays_length),
                            {v.scalar(P::length_red), v.scalar(P::length_green), v.scalar(P::length_blue)}, dark);
        s.shape = ray_shape{v.scalar(P::reverse_rays), v.scalar(P::bias_outer_bright), brightness};
        s.res_level = v.choice(P::rays_res);
        return s;
    }

    // the Shimmer group at `time` seconds, `P` being the effect's param enum
    template <class P> [[nodiscard]] shimmer read_shimmer(const param_values& v, double time)
    {
        return shimmer{v.scalar(P::shimmer_amp), v.scalar(P::shimmer_freq), noise::seed_bits(v.scalar(P::shimmer_seed)),
                       v.scalar(P::shimmer_shift_x) + time * v.scalar(P::shimmer_speed_x),
                       v.scalar(P::shimmer_shift_y) + time * v.scalar(P::shimmer_speed_y)};
    }

    // streaks the generator's rgb toward the centre on a grid reduced by the Rays Res level
    [[nodiscard]] buffer streak(const render_args& args, const path& s, const image& gen);

    // composite into args.dst: `rays` (rgb + alpha) screened over source-over-background,
    // or under the source by `under`; with `dark`, its rgb darkens by an inverted screen and is mixed in by
    // `dark_amount` (alpha stays the light result's)
    void composite(const render_args& args, const image& rays, const image* dark, double dark_amount, double under,
                   double source_opacity, double bg_brightness);
} // namespace functional_fx_suite::core::rays
