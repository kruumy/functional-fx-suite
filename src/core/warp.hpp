#pragma once

#include "core/buffer.hpp"
#include "core/frame.hpp"
#include "core/mip.hpp"
#include "core/parallel.hpp"
#include "core/render_args.hpp"
#include "core/sample.hpp"
#include <array>
#include <cmath>
#include <numbers>
#include <span>
#include <vector>

namespace functional_fx_suite::core::warp
{
    using vec2 = std::array<double, 2>;

    // rotate(v, turns), counter-clockwise in y-up coordinates
    [[nodiscard]] inline vec2 rotate(const vec2& v, double turns)
    {
        const double a = 2.0 * std::numbers::pi * turns;
        const double c = std::cos(a);
        const double s = std::sin(a);
        return {c * v[0] - s * v[1], s * v[0] + c * v[1]};
    }

    // centre of pixel (x, y) in xy coordinates
    [[nodiscard]] inline vec2 to_xy(const frame& f, int x, int y)
    {
        return {f.to_nx(x + 0.5) * XY_PER_FRAME_UNIT, f.to_ny(y + 0.5) * XY_PER_FRAME_UNIT};
    }

    // continuous pixel position of a point in xy coordinates
    [[nodiscard]] inline vec2 to_px(const frame& f, const vec2& p)
    {
        return {f.to_px_x(p[0] / XY_PER_FRAME_UNIT), f.to_px_y(p[1] / XY_PER_FRAME_UNIT)};
    }

    // warp lookup: bilinear, or with filter on, trilinear on a box pyramid at the level a
    // footprint (source pixels per output pixel) asks for
    class source
    {
      public:
        source(const image& src, sample::wrap mode, bool filter);

        [[nodiscard]] sample::pixel at(const vec2& px, double footprint) const;
        // lookup on a fractional pyramid level, always filtered
        [[nodiscard]] sample::pixel at_level(const vec2& px, double level) const;

      private:
        const image* src_;
        sample::wrap mode_;
        mip pyramid_;
        bool filter_;
    };

    // source pixel positions for every output pixel plus one more column and row (for footprints), 2 channels;
    // fn(x, y, xy) returns where output pixel (x, y), centred at xy, reads from, in xy coordinates
    template <class F> [[nodiscard]] buffer field(const render_args& args, const F& fn)
    {
        const rect& d = args.dst.bounds;
        buffer out(rect{d.x0, d.y0, d.x1 + 1, d.y1 + 1}, 2);
        const image& o = out.view();
        parallel::for_range(o.bounds.y0, o.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                for (int x = o.bounds.x0; x < o.bounds.x1; ++x)
                {
                    const vec2 p = to_px(args.geometry, fn(x, y, to_xy(args.geometry, x, y)));
                    float* q = o.at(x, y);
                    q[0] = static_cast<float>(p[0]);
                    q[1] = static_cast<float>(p[1]);
                }
            }
        });
        return out;
    }

    [[nodiscard]] inline vec2 at(const image& field, int x, int y)
    {
        const float* q = field.at(x, y);
        return {q[0], q[1]};
    }

    // longest step between neighbouring output pixels at (x, y), in source pixels
    [[nodiscard]] double footprint(const image& field, int x, int y);

    // dst = the source looked up through a field from field()
    void apply(const render_args& args, const image& field, const source& s);

    // blur warp at one pixel: weighted mean of lookups from p0 to p1 (pixels), weight going linearly
    // from e0 to e1, on pyramid level `res_level` or coarser when the path is long
    [[nodiscard]] sample::pixel line(const source& s, const vec2& p0, const vec2& p1, double e0, double e1,
                                     int res_level);

    using rgb_weights = std::vector<std::array<float, 3>>;

    // per-step rgb weights of the chroma warp: a spectrum through three colors, scaled so each channel of
    // white sums to `brightness` (for the given colors with white balance, else for red, green and blue)
    [[nodiscard]] rgb_weights spectrum(int steps, std::span<const std::array<double, 3>, 3> colors, bool white_balance,
                                       double brightness);

    // chroma warp at one pixel: lookups from p0 to p1 (pixels), rgb weighted per step, alpha averaged;
    // footprints are those of the two end fields
    [[nodiscard]] sample::pixel chroma(const source& s, const vec2& p0, const vec2& p1, double fp0, double fp1,
                                       const rgb_weights& weights);

    // the lens gradient's input: luma of the lens (the Background input, else
    // the source) after edges::prepare
    [[nodiscard]] buffer lens(const render_args& args, double blur, double scale);

    // gradient of lens() at (x, y) clamped into its bounds, y up
    [[nodiscard]] vec2 lens_gradient(const image& lens, int x, int y);

    // 1-channel matte value at (x, y) of a buffer from mask::build, 1 when there is none
    [[nodiscard]] inline float matte(const image& m, int x, int y)
    {
        if (m.data == nullptr)
        {
            return 1.0f;
        }
        return m.bounds.contains(x, y) ? *m.at(x, y) : 0.0f;
    }
} // namespace functional_fx_suite::core::warp
