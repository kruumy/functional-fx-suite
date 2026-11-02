#include "core/blur.hpp"
#include "core/edges.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <cmath>

namespace functional_fx_suite::core::edges
{
    buffer straight_rgb(const image& src)
    {
        buffer out(src.bounds, 3);
        const image& o = out.view();
        parallel::for_range(o.bounds.y0, o.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                for (int x = o.bounds.x0; x < o.bounds.x1; ++x)
                {
                    const float* s = src.at(x, y);
                    const float inv = s[3] > 0.0f ? 1.0f / s[3] : 0.0f;
                    std::transform(s, s + 3, o.at(x, y), [inv](float c) { return c * inv; });
                }
            }
        });
        return out;
    }

    void prepare(const image& img, double smooth, double scale, const frame& f)
    {
        const double amount = blur::amount_px(smooth, f);
        blur::gaussian(img, amount, amount, blur::edge::reflect);
        const auto k = static_cast<float>(f.to_px(scale) * SCALE_PER_FRAME_UNIT);
        parallel::for_range(img.bounds.y0, img.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                float* r = img.at(img.bounds.x0, y);
                std::for_each(r, r + static_cast<std::ptrdiff_t>(img.bounds.width()) * img.channels,
                              [k](float& v) { v *= k; });
            }
        });
    }

    std::array<float, 2> gradient(const image& img, int x, int y, int c)
    {
        const rect& b = img.bounds;
        const int left = std::max(x - 1, b.x0);
        const int right = std::min(x + 1, b.x1 - 1);
        const int above = std::max(y - 1, b.y0);
        const int below = std::min(y + 1, b.y1 - 1);
        const float gx = img.at(right, y)[c] - img.at(left, y)[c];
        const float gy = img.at(x, above)[c] - img.at(x, below)[c];
        return {right - left == 1 ? 2.0f * gx : gx, below - above == 1 ? 2.0f * gy : gy};
    }

    buffer magnitude(const image& img)
    {
        buffer out(img.bounds, img.channels);
        const image& o = out.view();
        parallel::for_range(o.bounds.y0, o.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                for (int x = o.bounds.x0; x < o.bounds.x1; ++x)
                {
                    float* d = o.at(x, y);
                    for (int c = 0; c < img.channels; ++c)
                    {
                        const auto [gx, gy] = gradient(img, x, y, c);
                        d[c] = std::sqrt(gx * gx + gy * gy);
                    }
                }
            }
        });
        return out;
    }
} // namespace functional_fx_suite::core::edges
