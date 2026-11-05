#include "core/glow.hpp"
#include "core/color.hpp"
#include "core/noise.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <cmath>

namespace functional_fx_suite::core::glow
{
    buffer source(const render_args& args, const image* matte, double from_alpha)
    {
        buffer out(args.src.bounds, 4);
        const image& o = out.view();
        const auto t = static_cast<float>(from_alpha);
        parallel::for_pixels(o.bounds, [&](int x, int y) {
            const float* s = args.src.at(x, y);
            float* d = o.at(x, y);
            for (int c = 0; c < 4; ++c)
            {
                d[c] = color::lerp(s[c], s[3], t);
            }
            if (matte != nullptr && matte->bounds.contains(x, y))
            {
                const float* m = matte->at(x, y);
                for (int c = 0; c < 3; ++c)
                {
                    d[c] *= m[matte->channels == 3 ? c : 0];
                }
            }
        });
        return out;
    }

    void hotspots(const image& img, const rgb& th)
    {
        parallel::for_pixels(img.bounds, [&](int x, int y) {
            float* v = img.at(x, y);
            float excess = 0.0f;
            float total = 0.0f;
            for (int c = 0; c < 3; ++c)
            {
                excess += std::max(v[c] - static_cast<float>(th[c]), 0.0f);
                total += std::max(v[c], 0.0f);
            }
            const float k = excess > 0.0f && total > 0.0f ? excess / total : 0.0f;
            std::for_each(v, v + 3, [k](float& c) { c *= k; });
        });
    }

    void threshold(const image& img, const rgb& th)
    {
        if (th[0] == 0.0 && th[1] == 0.0 && th[2] == 0.0)
        {
            return;
        }
        constexpr double MIN_HEADROOM = 1.0 / 128.0;
        const auto k = static_cast<float>(1.0 / std::max(MIN_HEADROOM, 1.0 - std::ranges::min(th)));
        parallel::for_pixels(img.bounds, [&](int x, int y) {
            float* v = img.at(x, y);
            float excess = 0.0f;
            float total = 0.0f;
            for (int c = 0; c < 3; ++c)
            {
                excess += std::max(v[c] - static_cast<float>(th[c]), 0.0f);
                total += std::max(v[c], 0.0f);
            }
            if (excess <= 0.0f || total <= 0.0f)
            {
                std::fill_n(v, 3, 0.0f);
                return;
            }
            // only ever darkens: bright cores keep their own value
            const float scaled = k * excess;
            if (total > scaled)
            {
                std::for_each(v, v + 3, [&](float& c) { c *= scaled / total; });
            }
        });
    }

    void blur_channels(const image& img, const rgb& amount_x, const rgb& amount_y, const rgb& gain, blur::edge mode)
    {
        buffer plane(img.bounds, 1);
        const image& p = plane.view();
        for (int c = 0; c < 3; ++c)
        {
            parallel::for_pixels(img.bounds, [&](int x, int y) { *p.at(x, y) = img.at(x, y)[c]; });
            blur::gaussian(p, amount_x[c], amount_y[c], mode);
            const auto g = static_cast<float>(gain[c]);
            parallel::for_pixels(img.bounds, [&](int x, int y) { img.at(x, y)[c] = *p.at(x, y) * g; });
        }
    }

    float atmosphere::at(double nx, double ny) const
    {
        constexpr int OCTAVES = 8;
        return static_cast<float>(1.0 - amp * noise::fbm_3d(seed, nx * freq, ny * freq, z, OCTAVES, detail));
    }

    atmosphere make_atmosphere(double amp, double freq, double detail, double seed, double speed, double time)
    {
        return {amp, freq, detail, noise::seed_bits(seed), speed * (time + 1.0) * 0.1};
    }

    void apply(const atmosphere& a, const image& img, const frame& f)
    {
        if (a.amp == 0.0)
        {
            return;
        }
        parallel::for_pixels(img.bounds, [&](int x, int y) {
            const float k = a.at(f.to_nx(x + 0.5), f.to_ny(y + 0.5));
            float* v = img.at(x, y);
            std::for_each(v, v + 3, [k](float& c) { c *= k; });
        });
    }

    sample::pixel blend(combine mode, const sample::pixel& g, const sample::pixel& base, bool mult_overlay)
    {
        sample::pixel r{};
        for (int c = 0; c < 4; ++c)
        {
            switch (mode)
            {
            case combine::mult:
                r[c] = mult_overlay ? color::overlay_super(g[c] * base[c], base[c]) : g[c] * base[c];
                break;
            case combine::add:
                r[c] = g[c] + base[c];
                break;
            case combine::screen:
                r[c] = color::screen_super(g[c], base[c]);
                break;
            case combine::difference:
                r[c] = c < 3 ? std::abs(g[c] - base[c]) : g[c] + base[c] - g[c] * base[c];
                break;
            case combine::overlay:
                r[c] = color::overlay_super(g[c], base[c]);
                break;
            }
        }
        return r;
    }

    static sample::pixel over(const sample::pixel& front, const sample::pixel& back)
    {
        sample::pixel r{};
        for (int c = 0; c < 4; ++c)
        {
            r[c] = front[c] + back[c] * (1.0f - front[3]);
        }
        return r;
    }

    static sample::pixel lerp(const sample::pixel& a, const sample::pixel& b, float t)
    {
        sample::pixel r{};
        for (int c = 0; c < 4; ++c)
        {
            r[c] = color::lerp(a[c], b[c], t);
        }
        return r;
    }

    static sample::pixel add(const sample::pixel& a, const sample::pixel& b)
    {
        sample::pixel r{};
        for (int c = 0; c < 4; ++c)
        {
            r[c] = a[c] + b[c];
        }
        return r;
    }

    static sample::pixel read(const image& img, int x, int y)
    {
        sample::pixel p{};
        if (img.bounds.contains(x, y))
        {
            std::copy_n(img.at(x, y), std::min(img.channels, 4), p.begin());
        }
        return p;
    }

    void composite(const render_args& args, const image& source, const image& glow, const image& shown,
                   const composite_settings& c)
    {
        const auto affect_alpha = static_cast<float>(c.affect_alpha);
        const auto light = static_cast<float>(c.light_bg);
        const auto scale_source = static_cast<float>(c.scale_source);
        const auto scale_background = static_cast<float>(c.scale_background);
        const float under = color::screen_super(static_cast<float>(c.glow_under_source), light);
        const image* bg = args.background;
        parallel::for_pixels(args.dst.bounds, [&](int x, int y) {
            float* d = args.dst.at(x, y);
            if (c.show)
            {
                const sample::pixel t = read(shown, x, y);
                std::copy_n(t.begin(), 3, d);
                d[3] = 1.0f;
                return;
            }
            const sample::pixel s = read(source, x, y);
            sample::pixel back{};
            if (bg != nullptr)
            {
                back = read(*bg, x, y);
                std::for_each(back.begin(), back.begin() + 3, [&](float& v) { v *= scale_background; });
            }
            // Light Background tints the glow by twice the background (or the source)
            const sample::pixel tint = bg != nullptr ? read(*bg, x, y) : s;
            const sample::pixel g3 = read(glow, x, y);
            sample::pixel g{};
            for (int k = 0; k < 3; ++k)
            {
                g[k] = g3[k] * color::lerp(1.0f, 2.0f * tint[k], light);
            }
            g[3] = std::max({g[0], g[1], g[2]}) * affect_alpha;
            sample::pixel src{};
            std::ranges::transform(s, src.begin(), [&](float v) { return v * scale_source; });
            const sample::pixel src_on_back = over(src, back);
            const sample::pixel on_both =
                lerp(blend(c.mode, g, src_on_back, c.mult_overlay), add(src_on_back, g), light);
            const sample::pixel on_back = over(src, lerp(blend(c.mode, g, back, c.mult_overlay), add(back, g), light));
            const sample::pixel r = lerp(on_both, on_back, under);
            std::copy(r.begin(), r.end(), d);
        });
    }
} // namespace functional_fx_suite::core::glow
