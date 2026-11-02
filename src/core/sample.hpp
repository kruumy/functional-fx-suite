#pragma once

#include "core/image.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace functional_fx_suite::core::sample
{
    // order matches the "Wrap" popup: No, Tile, Reflect
    enum class wrap : std::uint8_t
    {
        none,
        tile,
        reflect
    };

    using pixel = std::array<float, 4>;

    // maps index i into [lo, lo + n); false when it falls outside with wrap::none
    [[nodiscard]] inline bool resolve(int& i, int lo, int n, wrap mode)
    {
        int r = i - lo;
        if (r >= 0 && r < n)
        {
            return true;
        }
        switch (mode)
        {
        case wrap::none:
            return false;
        case wrap::tile:
            r %= n;
            r += r < 0 ? n : 0;
            break;
        case wrap::reflect: {
            const int period = 2 * n;
            r %= period;
            r += r < 0 ? period : 0;
            r = r >= n ? period - 1 - r : r;
            break;
        }
        }
        i = lo + r;
        return true;
    }

    // bilinear sample at continuous pixel position (pixel i spans [i, i + 1))
    [[nodiscard]] inline pixel bilinear(const image& img, double x, double y, wrap mode)
    {
        pixel out{};
        const int w = img.bounds.width();
        const int h = img.bounds.height();
        if (w <= 0 || h <= 0)
        {
            return out;
        }
        // keeps the float -> int conversion defined for wild coordinates
        constexpr double LIMIT = 1e8;
        const double fx = std::clamp(x - 0.5, -LIMIT, LIMIT);
        const double fy = std::clamp(y - 0.5, -LIMIT, LIMIT);
        const double flx = std::floor(fx);
        const double fly = std::floor(fy);
        const auto tx = static_cast<float>(fx - flx);
        const auto ty = static_cast<float>(fy - fly);

        std::array<int, 2> xs{static_cast<int>(flx), static_cast<int>(flx) + 1};
        std::array<int, 2> ys{static_cast<int>(fly), static_cast<int>(fly) + 1};
        const std::array<bool, 2> x_ok{resolve(xs[0], img.bounds.x0, w, mode), resolve(xs[1], img.bounds.x0, w, mode)};
        const std::array<bool, 2> y_ok{resolve(ys[0], img.bounds.y0, h, mode), resolve(ys[1], img.bounds.y0, h, mode)};
        const std::array<float, 2> wx{1.0f - tx, tx};
        const std::array<float, 2> wy{1.0f - ty, ty};

        const int channels = std::min(img.channels, 4);
        for (int j = 0; j < 2; ++j)
        {
            if (!y_ok[j])
            {
                continue;
            }
            const float* row = img.row(ys[j]);
            for (int i = 0; i < 2; ++i)
            {
                if (!x_ok[i])
                {
                    continue;
                }
                const float weight = wx[i] * wy[j];
                const float* p = row + static_cast<std::ptrdiff_t>(xs[i]) * img.channels;
                for (int c = 0; c < channels; ++c)
                {
                    out[c] += weight * p[c];
                }
            }
        }
        return out;
    }
} // namespace functional_fx_suite::core::sample
