#include "core/color.hpp"
#include "core/mip.hpp"
#include "core/noise.hpp"
#include "core/parallel.hpp"
#include "core/rays.hpp"
#include "core/sample.hpp"
#include <algorithm>
#include <cmath>

namespace functional_fx_suite::core::rays
{
    rgb channel_lengths(double base, const rgb& rel, bool dark)
    {
        const rgb l{base * rel[0], base * rel[1], base * rel[2]};
        if (!dark || std::ranges::any_of(l, [](double c) { return c == 0.0; }))
        {
            return l;
        }
        const rgb inv{1.0 / l[0], 1.0 / l[1], 1.0 / l[2]};
        const double scale = std::hypot(l[0], l[1], l[2]) / std::hypot(inv[0], inv[1], inv[2]);
        return {inv[0] * scale, inv[1] * scale, inv[2] * scale};
    }

    float shimmer::at(double nx, double ny) const
    {
        const double n = noise::value_2d(seed, (nx + x) * freq, (ny + y) * freq);
        return static_cast<float>(1.0 + amp * 0.5 - amp * n);
    }

    // ray integration weight at t in [0, 1] along the path
    static double weight(const ray_shape& s, double t)
    {
        if (s.reverse > 0.0)
        {
            // from the outer end (t = 0) through the pixel to the inner end (t = 1)
            const double mid = s.reverse / (s.reverse + 1.0);
            return t < mid ? s.bias + (1.0 - 2.0 * s.bias) * (t / mid)
                           : (1.0 - s.bias) + (2.0 * s.bias - 1.0) * ((t - mid) / (1.0 - mid));
        }
        return (1.0 - s.bias) + (2.0 * s.bias - 1.0) * t;
    }

    // integrates the generator along one pixel's ray path; `length` is the fraction of the way to the centre
    static std::array<float, 3> trace(const mip& gen, const frame& f, const path& s, double length, double nx,
                                      double ny)
    {
        const double reach = 1.0 - length;
        const double rev = std::min(1.0, -length * s.shape.reverse);
        const double from_x = s.cx + (nx - s.cx) * (1.0 - rev);
        const double from_y = s.cy + (ny - s.cy) * (1.0 - rev);
        const double to_x = s.cx + (nx - s.cx) * reach;
        const double to_y = s.cy + (ny - s.cy) * reach;
        const double path_px = std::hypot(to_x - from_x, to_y - from_y) * f.width;

        // taps about four pixels apart, read from a pyramid level blurred to match
        constexpr int MAX_TAPS = 64;
        constexpr double TAP_SPACING = 4.0;
        const int taps = std::clamp(static_cast<int>(std::ceil(path_px / TAP_SPACING)), 1, MAX_TAPS);
        const auto level = static_cast<int>(std::lround(std::log2(std::max(path_px / taps, 1.0))));
        std::array<float, 3> sum{};
        float weights = 0.0f;
        for (int i = 0; i < taps; ++i)
        {
            const double t = (i + 0.5) / taps;
            const auto px = gen.lookup_level(f.to_px_x(from_x + (to_x - from_x) * t),
                                             f.to_px_y(from_y + (to_y - from_y) * t), level);
            const auto w = static_cast<float>(weight(s.shape, t));
            weights += w;
            for (int c = 0; c < 3; ++c)
            {
                sum[c] += w * px[c];
            }
        }
        // weighted mean along the path: the bias shapes the falloff without changing overall brightness
        const float scale = weights > 0.0f ? static_cast<float>(s.shape.brightness) / weights : 0.0f;
        for (float& c : sum)
        {
            c *= scale;
        }
        return sum;
    }

    buffer streak(const render_args& args, const path& s, const image& gen)
    {
        const int step = 1 << s.res_level;
        const rect& b = args.src.bounds;
        const rect grid{0, 0, (b.width() + step - 1) / step, (b.height() + step - 1) / step};
        buffer out(grid, 4);
        constexpr int MIP_LEVELS = 12;
        const mip pyramid(gen, MIP_LEVELS);
        const bool same_length = s.lengths[0] == s.lengths[1] && s.lengths[1] == s.lengths[2];
        parallel::for_range(0, grid.y1, [&](int y_begin, int y_end) {
            for (int gy = y_begin; gy < y_end; ++gy)
            {
                const double ny = args.geometry.to_ny(b.y0 + (gy + 0.5) * step);
                for (int gx = 0; gx < grid.x1; ++gx)
                {
                    const double nx = args.geometry.to_nx(b.x0 + (gx + 0.5) * step);
                    float* d = out.view().at(gx, gy);
                    if (same_length)
                    {
                        const auto r = trace(pyramid, args.geometry, s, s.lengths[0], nx, ny);
                        std::copy(r.begin(), r.end(), d);
                        continue;
                    }
                    for (int c = 0; c < 3; ++c)
                    {
                        d[c] = trace(pyramid, args.geometry, s, s.lengths[c], nx, ny)[c];
                    }
                }
            }
        });
        return out;
    }

    static sample::pixel over(const sample::pixel& a, const sample::pixel& b)
    {
        sample::pixel r{};
        for (int c = 0; c < 4; ++c)
        {
            r[c] = a[c] + b[c] * (1.0f - a[3]);
        }
        return r;
    }

    static sample::pixel screen(const sample::pixel& a, const sample::pixel& b)
    {
        sample::pixel r{};
        for (int c = 0; c < 4; ++c)
        {
            r[c] = color::screen(a[c], b[c]);
        }
        return r;
    }

    // 1 - screen(1 - a, b) on rgb, keeping a's alpha
    static sample::pixel darken(const sample::pixel& a, const sample::pixel& b)
    {
        sample::pixel r = a;
        for (int c = 0; c < 3; ++c)
        {
            r[c] = 1.0f - color::screen(1.0f - a[c], b[c]);
        }
        return r;
    }

    void composite(const render_args& args, const image& rays, const image* dark, double dark_amount, double under,
                   double source_opacity, double bg_brightness)
    {
        const auto opacity = static_cast<float>(source_opacity);
        const auto bg_scale = static_cast<float>(bg_brightness);
        const auto under_f = static_cast<float>(under);
        const auto dark_f = static_cast<float>(dark_amount);
        const image* bg = args.background;
        const image& dst = args.dst;

        parallel::for_range(dst.bounds.y0, dst.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                for (int x = dst.bounds.x0; x < dst.bounds.x1; ++x)
                {
                    sample::pixel src{};
                    if (args.src.bounds.contains(x, y))
                    {
                        const float* sp = args.src.at(x, y);
                        std::ranges::transform(sp, sp + 4, src.begin(), [&](float c) { return c * opacity; });
                    }
                    sample::pixel back{};
                    if (bg != nullptr && bg->bounds.contains(x, y))
                    {
                        const float* bp = bg->at(x, y);
                        back = {bp[0] * bg_scale, bp[1] * bg_scale, bp[2] * bg_scale, bp[3]};
                    }
                    const float* rp = rays.at(x, y);
                    const sample::pixel ray{rp[0], rp[1], rp[2], rp[3]};
                    const sample::pixel comp = bg != nullptr ? over(src, back) : src;

                    // light rays: screen over everything, or screen onto the background under the source
                    const sample::pixel light_over = screen(comp, ray);
                    const sample::pixel light_under = bg != nullptr ? over(src, screen(back, ray)) : over(src, ray);
                    sample::pixel result{};
                    for (int c = 0; c < 4; ++c)
                    {
                        result[c] = color::lerp(light_over[c], light_under[c], under_f);
                    }
                    if (dark != nullptr && dark_f > 0.0f)
                    {
                        const float* dp = dark->at(x, y);
                        const sample::pixel dark_ray{dp[0], dp[1], dp[2], 0.0f};
                        const sample::pixel opaque_one{1.0f, 1.0f, 1.0f, 1.0f};
                        const sample::pixel dark_over = darken(comp, dark_ray);
                        const sample::pixel dark_under =
                            bg != nullptr ? over(src, darken(back, dark_ray)) : over(src, darken(opaque_one, dark_ray));
                        for (int c = 0; c < 3; ++c)
                        {
                            const float d = color::lerp(dark_over[c], dark_under[c], under_f);
                            // dissolve: clamped, exact at the ends
                            result[c] = dark_f >= 1.0f ? d : color::lerp(result[c], d, dark_f);
                        }
                    }
                    std::copy(result.begin(), result.end(), dst.at(x, y));
                }
            }
        });
    }
} // namespace functional_fx_suite::core::rays
