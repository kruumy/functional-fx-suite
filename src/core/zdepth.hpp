#pragma once

#include "core/buffer.hpp"
#include "core/render_args.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

namespace functional_fx_suite::core::zdepth
{
    // 0 is near, 1 is far; a missing Z buffer (the background input) reads as black
    [[nodiscard]] buffer depth(const render_args& args, bool white_near, bool use_alpha);

    // the focus zone screened with 0.5, everything else darkened
    void show_zone(const render_args& args, const image& z, double focal_depth, double depth_of_field);

    // the source dissolved toward the fog color (times alpha) by dissolve(near, far, z)
    [[nodiscard]] buffer fog(const image& src, const image& z, float near, float far,
                             const std::array<double, 3>& color);

    struct layers
    {
        int count = 5;
        double focal_depth = 0;
        double depth_of_field = 0.1;
        double rel_near = 1;
        double rel_far = 1;
        bool comp = false; // Comp: each depth slice on its own, over the farther ones; Interp: mix the layers per pixel
    };

    // the layer settings from an effect's Layers, Layer Mode, focus and Width Rel params
    template <param_index E> [[nodiscard]] layers read_layers(const param_values& v)
    {
        return {
            .count = v.whole(E::layers),
            .focal_depth = v.scalar(E::focal_depth),
            .depth_of_field = v.scalar(E::depth_of_field),
            .rel_near = v.scalar(E::width_rel_near),
            .rel_far = v.scalar(E::width_rel_far),
            .comp = v.choice(E::layer_mode) == 0,
        };
    }

    // how far depth d lies outside the focus zone, scaled by rel_near or rel_far
    [[nodiscard]] inline double focus_distance(const layers& s, double d)
    {
        return std::max(0.0, std::abs(d - s.focal_depth) - (s.depth_of_field / 2.0)) *
               (d < s.focal_depth ? s.rel_near : s.rel_far);
    }

    // splits src into depth layers evenly spaced over 0..1, defocuses each in place by its distance outside the focus
    // zone and composites them far to near into args.dst
    void composite(const render_args& args, const image& src, const image& z, const layers& settings,
                   const std::function<void(const image& layer, double out_of_focus)>& defocus);
} // namespace functional_fx_suite::core::zdepth
