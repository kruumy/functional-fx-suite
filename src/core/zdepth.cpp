#include "core/zdepth.hpp"
#include "core/color.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <cmath>

namespace functional_fx_suite::core::zdepth
{
    buffer depth(const render_args& args, bool white_near, bool use_alpha)
    {
        buffer out(args.src.bounds, 1);
        const image& o = out.view();
        const image* zb = args.background;
        parallel::for_range(o.bounds.y0, o.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                for (int x = o.bounds.x0; x < o.bounds.x1; ++x)
                {
                    float z = 0.0f;
                    if (zb != nullptr && zb->bounds.contains(x, y))
                    {
                        const float* q = zb->at(x, y);
                        z = use_alpha ? q[3] : color::luma(q[0], q[1], q[2]);
                    }
                    *o.at(x, y) = white_near ? 1.0f - z : z;
                }
            }
        });
        return out;
    }

    void show_zone(const render_args& args, const image& z, double focal_depth, double depth_of_field)
    {
        const double d = std::max(0.001, depth_of_field / 2.0);
        const double lo = focal_depth - d;
        const double hi = focal_depth + d;
        const image& dst = args.dst;
        parallel::for_range(dst.bounds.y0, dst.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                for (int x = dst.bounds.x0; x < dst.bounds.x1; ++x)
                {
                    float* o = dst.at(x, y);
                    if (!args.src.bounds.contains(x, y))
                    {
                        std::fill_n(o, 4, 0.0f);
                        continue;
                    }
                    const float* s = args.src.at(x, y);
                    const float zv = *z.at(x, y);
                    const float sharp = std::clamp(zv, 0.0f, 1.0f) > lo && zv <= hi ? 0.5f : 0.0f;
                    for (int c = 0; c < 3; ++c)
                    {
                        o[c] = color::screen_super(s[c] * 0.6f * (1.0f - sharp), sharp);
                    }
                    o[3] = s[3];
                }
            }
        });
    }

    buffer fog(const image& src, const image& z, float near, float far, const std::array<double, 3>& color)
    {
        const rect& b = src.bounds;
        buffer fogged(b, 4);
        parallel::for_range(b.y0, b.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                for (int x = b.x0; x < b.x1; ++x)
                {
                    const float* s = src.at(x, y);
                    float* o = fogged.view().at(x, y);
                    const float amount = color::dissolve(near, far, *z.at(x, y));
                    for (int c = 0; c < 3; ++c)
                    {
                        o[c] = color::dissolve(s[c], static_cast<float>(color[c]) * s[3], amount);
                    }
                    o[3] = s[3];
                }
            }
        });
        return fogged;
    }

    void composite(const render_args& args, const image& src, const image& z, const layers& settings,
                   const std::function<void(const image& layer, double out_of_focus)>& defocus)
    {
        const int n = settings.count;
        const double half_dof = settings.depth_of_field / 2.0;
        const bool comp = settings.comp;
        const rect& b = src.bounds;
        const image& dst = args.dst;
        buffer acc(dst.bounds, 4);
        buffer layer(b, 4);
        const image& l = layer.view();

        // far layers first so nearer ones composite over them
        for (int k = n - 1; k >= 0; --k)
        {
            const double d = static_cast<double>(k) / (n - 1);
            // blur grows with the distance outside the in-focus zone
            const double out_of_focus = std::max(0.0, std::abs(d - settings.focal_depth) - half_dof) *
                                        (d < settings.focal_depth ? settings.rel_near : settings.rel_far);
            // each depth belongs to its two nearest layers by a tent weight
            const auto weight = [&](int x, int y) {
                const double u = std::clamp(static_cast<double>(*z.at(x, y)), 0.0, 1.0) * (n - 1);
                return static_cast<float>(std::max(0.0, 1.0 - std::abs(u - k)));
            };
            parallel::for_range(b.y0, b.y1, [&](int y_begin, int y_end) {
                for (int y = y_begin; y < y_end; ++y)
                {
                    for (int x = b.x0; x < b.x1; ++x)
                    {
                        const float* s = src.at(x, y);
                        const float m = comp ? weight(x, y) : 1.0f;
                        std::transform(s, s + 4, l.at(x, y), [m](float c) { return c * m; });
                    }
                }
            });
            defocus(l, out_of_focus);
            parallel::for_range(dst.bounds.y0, dst.bounds.y1, [&](int y_begin, int y_end) {
                for (int y = y_begin; y < y_end; ++y)
                {
                    for (int x = dst.bounds.x0; x < dst.bounds.x1; ++x)
                    {
                        if (!b.contains(x, y))
                        {
                            continue;
                        }
                        const float* s = l.at(x, y);
                        float* a = acc.view().at(x, y);
                        // comp: the layer over what is behind it; interp: each pixel mixes the layers around its depth
                        const float keep = comp ? 1.0f - s[3] : 1.0f;
                        const float m = comp ? 1.0f : weight(x, y);
                        for (int c = 0; c < 4; ++c)
                        {
                            a[c] = (s[c] * m) + (a[c] * keep);
                        }
                    }
                }
            });
        }
        parallel::for_range(dst.bounds.y0, dst.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                std::copy_n(acc.view().at(dst.bounds.x0, y), static_cast<std::size_t>(dst.bounds.width()) * 4,
                            dst.at(dst.bounds.x0, y));
            }
        });
    }
} // namespace functional_fx_suite::core::zdepth
