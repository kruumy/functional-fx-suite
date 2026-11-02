#pragma once

#include "core/blur.hpp"
#include "core/buffer.hpp"
#include "core/color.hpp"
#include "core/parallel.hpp"
#include "core/render_args.hpp"
#include "core/warp.hpp"
#include <algorithm>
#include <array>

namespace functional_fx_suite::core::psyko
{
    // the color, phase and mixing params shared by f_psykoblobs and f_psykostripes
    struct settings
    {
        std::array<double, 3> freq;  // freq_colors x Freq Red / Green / Blue
        std::array<double, 3> phase; // phase_start + Phase Red / Green / Blue + phase_speed x seconds
        std::array<float, 3> tint;   // brightness x color
        float saturation;
        float scale_by_source;
        float scale_by_src_amp;
        float offset;
        float mix;
        double source_blur;
        double source_scale;
    };

    // `pattern(xy)` is the noise (Blobs) or the stripe ramp (Stripes):
    // pic = sat_scale(nsin(freq x (pattern + bw - phase)) x tint), times interp(1, min(1, bw x amp), scale_by_source),
    // plus offset, mixed with the source; bw is the source luma blurred by Source Blur, times Source Scale
    template <class Pattern> void render(const render_args& args, const settings& s, Pattern&& pattern)
    {
        const rect& b = args.src.bounds;
        buffer bw(b, 1);
        const image& l = bw.view();
        parallel::for_range(b.y0, b.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                for (int x = b.x0; x < b.x1; ++x)
                {
                    const float* p = args.src.at(x, y);
                    *l.at(x, y) = color::luma(p[0], p[1], p[2]) * static_cast<float>(s.source_scale);
                }
            }
        });
        const double amount = blur::amount_px(s.source_blur, args.geometry);
        // reflected so the frame border does not darken bw into a rim of extra bands
        blur::gaussian(l, amount, amount, blur::edge::reflect);

        const image& dst = args.dst;
        parallel::for_range(dst.bounds.y0, dst.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                for (int x = dst.bounds.x0; x < dst.bounds.x1; ++x)
                {
                    float* d = dst.at(x, y);
                    if (!b.contains(x, y))
                    {
                        std::fill_n(d, 4, 0.0f);
                        continue;
                    }
                    const float* p = args.src.at(x, y);
                    const float a = p[3];
                    const float inv = a > 0.0f ? 1.0f / a : 0.0f;
                    const float v = *l.at(x, y);
                    const double u = pattern(warp::to_xy(args.geometry, x, y)) + v;
                    color::rgb pic{};
                    for (int c = 0; c < 3; ++c)
                    {
                        pic[c] = static_cast<float>(color::nsin(s.freq[c] * (u - s.phase[c]))) * s.tint[c];
                    }
                    pic = color::sat_scale(pic, s.saturation);
                    const float k = color::lerp(1.0f, std::min(1.0f, v * s.scale_by_src_amp), s.scale_by_source);
                    for (int c = 0; c < 3; ++c)
                    {
                        d[c] = color::lerp((pic[c] * k) + s.offset, p[c] * inv, s.mix) * a;
                    }
                    d[3] = a;
                }
            }
        });
    }
} // namespace functional_fx_suite::core::psyko
