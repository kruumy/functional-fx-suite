#include "core/blur.hpp"
#include "core/parallel.hpp"
#include "core/sample.hpp"
#include <algorithm>

namespace functional_fx_suite::core::blur
{
    radii split(int total)
    {
        total = std::max(0, total);
        const int first = total / 3;
        const int second = (total - first) / 2;
        return {first, second, total - first - second};
    }

    // gaussian: total reach amount * 4/3, split as evenly as possible over three boxes
    static radii box_radii(double amount)
    {
        constexpr double GAUSS_SCALE = 4.0 / 3.0;
        return split(static_cast<int>(amount * GAUSS_SCALE + 0.5));
    }

    int reach(double amount)
    {
        const radii r = box_radii(amount);
        return r[0] + r[1] + r[2];
    }

    // one box pass over n pixels of `channels` floats, zero outside [0, n)
    static void box_pass(const float* src, float* dst, int n, int channels, int radius)
    {
        const float inv = 1.0f / static_cast<float>(2 * radius + 1);
        std::array<float, 4> sum{};
        for (int k = 0; k <= radius && k < n; ++k)
        {
            for (int c = 0; c < channels; ++c)
            {
                sum[c] += src[k * channels + c];
            }
        }
        for (int i = 0; i < n; ++i)
        {
            const int enter = i + radius + 1;
            const int leave = i - radius;
            for (int c = 0; c < channels; ++c)
            {
                dst[i * channels + c] = sum[c] * inv;
                sum[c] +=
                    (enter < n ? src[enter * channels + c] : 0.0f) - (leave >= 0 ? src[leave * channels + c] : 0.0f);
            }
        }
    }

    void box_line(std::vector<float>& line, std::vector<float>& scratch, int n, int channels, const radii& r)
    {
        box_pass(line.data(), scratch.data(), n, channels, r[0]);
        box_pass(scratch.data(), line.data(), n, channels, r[1]);
        box_pass(line.data(), scratch.data(), n, channels, r[2]);
        line.swap(scratch);
    }

    // blurs `count` lines of n pixels; `at(line, i)` addresses pixel i of a line
    template <class At> static void blur_lines(int count, int n, int ch, const radii& r, edge mode, At&& at)
    {
        // reflected edges: pad each line with mirrored pixels so the boxes never read past them
        const int pad = mode == edge::reflect ? r[0] + r[1] + r[2] : 0;
        const int padded = n + 2 * pad;
        parallel::for_range(0, count, [&](int begin, int end) {
            std::vector<float> line(static_cast<std::size_t>(padded) * ch);
            std::vector<float> scratch(line.size());
            for (int l = begin; l < end; ++l)
            {
                for (int k = 0; k < padded; ++k)
                {
                    int i = k - pad;
                    static_cast<void>(sample::resolve(i, 0, n, sample::wrap::reflect));
                    std::copy_n(at(l, i), ch, line.begin() + static_cast<std::ptrdiff_t>(k) * ch);
                }
                box_line(line, scratch, padded, ch, r);
                for (int i = 0; i < n; ++i)
                {
                    std::copy_n(line.begin() + static_cast<std::ptrdiff_t>(i + pad) * ch, ch, at(l, i));
                }
            }
        });
    }

    void gaussian(const image& img, double amount_x, double amount_y, edge mode)
    {
        const int w = img.bounds.width();
        const int h = img.bounds.height();
        const int ch = img.channels;
        if (w <= 0 || h <= 0 || ch > 4)
        {
            return;
        }
        if (const radii rx = box_radii(amount_x); rx[2] > 0)
        {
            blur_lines(h, w, ch, rx, mode, [&](int y, int x) { return img.at(img.bounds.x0 + x, img.bounds.y0 + y); });
        }
        if (const radii ry = box_radii(amount_y); ry[2] > 0)
        {
            blur_lines(w, h, ch, ry, mode, [&](int x, int y) { return img.at(img.bounds.x0 + x, img.bounds.y0 + y); });
        }
    }
} // namespace functional_fx_suite::core::blur
