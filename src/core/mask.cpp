#include "core/mask.hpp"
#include "core/blur.hpp"
#include "core/color.hpp"
#include "core/parallel.hpp"

namespace functional_fx_suite::core::mask
{
    buffer build(const render_args& args, source mode, double blur_width, bool invert)
    {
        buffer out;
        if (args.mask == nullptr)
        {
            return out;
        }
        const int channels = mode == source::color ? 3 : 1;
        const double amount = blur::amount_px(blur_width, args.geometry);
        // margin so pixels near the edge of the area blur against their real neighbours
        const int margin = blur::reach(amount);
        const rect& s = args.src.bounds;
        out.reset(rect{s.x0 - margin, s.y0 - margin, s.x1 + margin, s.y1 + margin}, channels);
        const image& m = out.view();
        const image& in = *args.mask;
        parallel::for_pixels(m.bounds, [&](int x, int y) {
            float* d = m.at(x, y);
            if (in.bounds.contains(x, y))
            {
                const float* p = in.at(x, y);
                switch (mode)
                {
                case source::luma:
                    d[0] = color::luma(p[0], p[1], p[2]);
                    break;
                case source::alpha:
                    d[0] = p[3];
                    break;
                case source::color:
                    d[0] = p[0];
                    d[1] = p[1];
                    d[2] = p[2];
                    break;
                }
            }
            for (int c = 0; c < channels; ++c)
            {
                d[c] = invert ? 1.0f - d[c] : d[c];
            }
        });
        blur::gaussian(m, amount, amount);
        return out;
    }

    void dissolve(const render_args& args, const image& mask)
    {
        const image& dst = args.dst;
        parallel::for_pixels(dst.bounds, [&](int x, int y) {
            const float m = mask.bounds.contains(x, y) ? *mask.at(x, y) : 0.0f;
            const float* s = args.src.bounds.contains(x, y) ? args.src.at(x, y) : nullptr;
            float* d = dst.at(x, y);
            for (int c = 0; c < 4; ++c)
            {
                d[c] = color::lerp(s != nullptr ? s[c] : 0.0f, d[c], m);
            }
        });
    }

    void apply(const render_args& args, source mode, double blur_width, bool invert)
    {
        if (const buffer m = build(args, mode, blur_width, invert); m.view().data != nullptr)
        {
            dissolve(args, m.view());
        }
    }
} // namespace functional_fx_suite::core::mask
