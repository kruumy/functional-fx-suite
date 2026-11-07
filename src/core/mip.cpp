#include "core/mip.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <array>

namespace functional_fx_suite::core
{
    mip::mip(const image& src, int max_levels) : origin_x_(src.bounds.x0), origin_y_(src.bounds.y0)
    {
        const rect local{0, 0, src.bounds.width(), src.bounds.height()};
        buffer& base = levels_.emplace_back(local, 4);
        const int copy_channels = std::min(src.channels, 4);
        parallel::for_pixels(local, [&](int x, int y) {
            std::copy_n(src.at(src.bounds.x0 + x, src.bounds.y0 + y), copy_channels, base.view().at(x, y));
        });
        while (levels() < max_levels &&
               (levels_.back().view().bounds.width() > 1 || levels_.back().view().bounds.height() > 1))
        {
            buffer next = downsample(levels_.back().view());
            levels_.push_back(std::move(next));
        }
    }

    buffer mip::downsample(const image& src)
    {
        const int w = (src.bounds.width() + 1) / 2;
        const int h = (src.bounds.height() + 1) / 2;
        buffer out(rect{0, 0, w, h}, 4);
        const image& dst = out.view();
        parallel::for_pixels(dst.bounds, [&](int x, int y) {
            // missing odd-edge texels count as transparent, like the blur
            float* d = dst.at(x, y);
            for (int j = 0; j < 2; ++j)
            {
                const int sy = 2 * y + j;
                if (sy >= src.bounds.height())
                {
                    continue;
                }
                for (int i = 0; i < 2; ++i)
                {
                    const int sx = 2 * x + i;
                    if (sx >= src.bounds.width())
                    {
                        continue;
                    }
                    const float* s = src.at(sx, sy);
                    for (int c = 0; c < 4; ++c)
                    {
                        d[c] += 0.25f * s[c];
                    }
                }
            }
        });
        return out;
    }

    sample::pixel mip::lookup_level(double x, double y, int l, sample::wrap mode) const
    {
        l = std::clamp(l, 0, levels() - 1);
        const double s = 1.0 / static_cast<double>(1 << l);
        return sample::bilinear(level(l), (x - origin_x_) * s, (y - origin_y_) * s, mode);
    }

    buffer box_mean(const image& src, int k)
    {
        const rect& b = src.bounds;
        const int channels = src.channels;
        buffer out(rect{0, 0, (b.width() + k - 1) / k, (b.height() + k - 1) / k}, channels);
        parallel::for_pixels(out.view().bounds, [&](int i, int j) {
            std::array<double, 4> sum{};
            int n = 0;
            for (int y = b.y0 + j * k; y < std::min(b.y1, b.y0 + (j + 1) * k); ++y)
            {
                for (int x = b.x0 + i * k; x < std::min(b.x1, b.x0 + (i + 1) * k); ++x)
                {
                    const float* s = src.at(x, y);
                    for (int c = 0; c < channels; ++c)
                    {
                        sum[static_cast<std::size_t>(c)] += s[c];
                    }
                    ++n;
                }
            }
            float* d = out.view().at(i, j);
            for (int c = 0; c < channels; ++c)
            {
                d[c] = static_cast<float>(sum[static_cast<std::size_t>(c)] / n);
            }
        });
        return out;
    }
} // namespace functional_fx_suite::core
