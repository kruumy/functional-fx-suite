#include "core/warp.hpp"
#include "core/color.hpp"
#include "core/edges.hpp"
#include <algorithm>

namespace functional_fx_suite::core::warp
{
    static constexpr int MAX_LEVELS = 16;

    source::source(const image& src, sample::wrap mode, bool filter)
        : src_(&src), mode_(mode), pyramid_(src, MAX_LEVELS), filter_(filter)
    {
    }

    sample::pixel source::at(const vec2& px, double footprint) const
    {
        if (!filter_ || footprint <= 1.0)
        {
            return sample::bilinear(*src_, px[0], px[1], mode_);
        }
        return at_level(px, std::log2(footprint));
    }

    sample::pixel source::at_level(const vec2& px, double level) const
    {
        level = std::clamp(level, 0.0, static_cast<double>(pyramid_.levels() - 1));
        const int l0 = static_cast<int>(level);
        const auto t = static_cast<float>(level - l0);
        sample::pixel a = pyramid_.lookup_level(px[0], px[1], l0, mode_);
        if (t > 0.0f)
        {
            const sample::pixel b = pyramid_.lookup_level(px[0], px[1], l0 + 1, mode_);
            for (int c = 0; c < 4; ++c)
            {
                a[c] += (b[c] - a[c]) * t;
            }
        }
        return a;
    }

    double footprint(const image& field, int x, int y)
    {
        const vec2 p = at(field, x, y);
        const rect& b = field.bounds;
        const vec2 px = at(field, std::min(x + 1, b.x1 - 1), y);
        const vec2 py = at(field, x, std::min(y + 1, b.y1 - 1));
        return std::max(std::hypot(px[0] - p[0], px[1] - p[1]), std::hypot(py[0] - p[0], py[1] - p[1]));
    }

    void apply(const render_args& args, const image& field, const source& s)
    {
        const image& dst = args.dst;
        parallel::for_range(dst.bounds.y0, dst.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                for (int x = dst.bounds.x0; x < dst.bounds.x1; ++x)
                {
                    const sample::pixel px = s.at(at(field, x, y), footprint(field, x, y));
                    std::copy(px.begin(), px.end(), dst.at(x, y));
                }
            }
        });
    }

    sample::pixel line(const source& s, const vec2& p0, const vec2& p1, double e0, double e1, int res_level)
    {
        // one texel per tap; past MAX_TAPS a coarser level keeps the taps a texel apart
        constexpr int MAX_TAPS = 64;
        const double length = std::hypot(p1[0] - p0[0], p1[1] - p0[1]);
        double level = res_level;
        double texels = length / std::exp2(level);
        if (texels > MAX_TAPS - 1)
        {
            level += std::log2(texels / (MAX_TAPS - 1));
            texels = MAX_TAPS - 1;
        }
        const int taps = static_cast<int>(std::ceil(texels)) + 1;
        sample::pixel sum{};
        double weights = 0.0;
        for (int i = 0; i < taps; ++i)
        {
            const double f = taps > 1 ? static_cast<double>(i) / (taps - 1) : 0.5;
            const double w = e0 + (e1 - e0) * f;
            const vec2 p{p0[0] + (p1[0] - p0[0]) * f, p0[1] + (p1[1] - p0[1]) * f};
            const sample::pixel px = level > 0.0 ? s.at_level(p, level) : s.at(p, 1.0);
            for (int c = 0; c < 4; ++c)
            {
                sum[c] += static_cast<float>(w) * px[c];
            }
            weights += w;
        }
        const auto inv = static_cast<float>(weights != 0.0 ? 1.0 / weights : 0.0);
        for (float& c : sum)
        {
            c *= inv;
        }
        return sum;
    }

    buffer lens(const render_args& args, double blur, double scale)
    {
        const image& in = args.background != nullptr ? *args.background : args.src;
        buffer out(in.bounds, 1);
        const image& o = out.view();
        parallel::for_range(o.bounds.y0, o.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                for (int x = o.bounds.x0; x < o.bounds.x1; ++x)
                {
                    const float* p = in.at(x, y);
                    *o.at(x, y) = color::luma(p[0], p[1], p[2]);
                }
            }
        });
        edges::prepare(o, blur, scale, args.geometry);
        return out;
    }

    vec2 lens_gradient(const image& lens, int x, int y)
    {
        const rect& b = lens.bounds;
        if (b.width() <= 0 || b.height() <= 0)
        {
            return {};
        }
        const auto [gx, gy] = edges::gradient(lens, std::clamp(x, b.x0, b.x1 - 1), std::clamp(y, b.y0, b.y1 - 1), 0);
        return {gx, gy};
    }

    rgb_weights spectrum(int steps, std::span<const std::array<double, 3>, 3> colors, bool white_balance,
                         double brightness)
    {
        steps = std::max(steps, 2);
        // color1 at the first step, color2 halfway, color3 at the last
        const auto ramp = [steps](std::span<const std::array<double, 3>, 3> cs, int i, int c) {
            const double u = 2.0 * i / (steps - 1);
            const int k = u < 1.0 ? 0 : 1;
            return cs[k][c] + (cs[k + 1][c] - cs[k][c]) * (u - k);
        };
        // white balance divides by the colors' own totals; otherwise by those of red, green and blue, so the
        // default colors add up to white and custom ones keep their tint
        static constexpr std::array<std::array<double, 3>, 3> PRIMARIES{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
        const std::span<const std::array<double, 3>, 3> norm = white_balance ? colors : std::span{PRIMARIES};
        rgb_weights w(static_cast<std::size_t>(steps));
        for (int c = 0; c < 3; ++c)
        {
            double total = 0.0;
            for (int i = 0; i < steps; ++i)
            {
                total += ramp(norm, i, c);
            }
            const double k = total != 0.0 ? brightness / total : 0.0;
            for (int i = 0; i < steps; ++i)
            {
                w[i][c] = static_cast<float>(ramp(colors, i, c) * k);
            }
        }
        return w;
    }

    sample::pixel chroma(const source& s, const vec2& p0, const vec2& p1, double fp0, double fp1,
                         const rgb_weights& weights)
    {
        sample::pixel out{};
        const int n = static_cast<int>(weights.size());
        for (int i = 0; i < n; ++i)
        {
            const double f = static_cast<double>(i) / (n - 1);
            const vec2 p{p0[0] + (p1[0] - p0[0]) * f, p0[1] + (p1[1] - p0[1]) * f};
            const sample::pixel px = s.at(p, fp0 + (fp1 - fp0) * f);
            for (int c = 0; c < 3; ++c)
            {
                out[c] += px[c] * weights[i][c];
            }
            out[3] += px[3];
        }
        out[3] /= static_cast<float>(n);
        return out;
    }
} // namespace functional_fx_suite::core::warp
