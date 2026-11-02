#include "core/fft.hpp"
#include "core/parallel.hpp"
#include "core/sample.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace functional_fx_suite::core::fft
{
    static constexpr std::array<int, 3> RADICES{5, 3, 2};

    int good_size(int n)
    {
        for (int m = std::max(n, 1);; ++m)
        {
            int r = m;
            for (const int p : RADICES)
            {
                while (r % p == 0)
                {
                    r /= p;
                }
            }
            if (r == 1)
            {
                return m;
            }
        }
    }

    plan::plan(int n) : n_(n)
    {
        int r = n;
        for (const int p : RADICES)
        {
            while (r % p == 0)
            {
                factors_.push_back(p);
                r /= p;
            }
        }
        roots_.resize(static_cast<std::size_t>(n));
        for (int k = 0; k < n; ++k)
        {
            const double a = -2.0 * std::numbers::pi * k / n;
            roots_[static_cast<std::size_t>(k)] = {static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a))};
        }
    }

    void plan::run(std::span<const complex> in, std::span<complex> out, bool inverse) const
    {
        step(in.data(), 1, out.data(), n_, 0, inverse);
    }

    // decimation in time: transform the p interleaved sub-sequences, then combine them with radix-p butterflies
    void plan::step(const complex* in, std::ptrdiff_t stride, complex* out, int n, std::size_t f, bool inverse) const
    {
        if (n == 1)
        {
            out[0] = in[0];
            return;
        }
        const int p = factors_[f];
        const int m = n / p;
        for (int q = 0; q < p; ++q)
        {
            step(in + (q * stride), stride * p, out + (static_cast<std::ptrdiff_t>(q) * m), m, f + 1, inverse);
        }
        const int scale = n_ / n;
        const auto root = [&](int k) {
            const complex w = roots_[static_cast<std::size_t>(k)];
            return inverse ? std::conj(w) : w;
        };
        if (p == 2)
        {
            for (int k = 0; k < m; ++k)
            {
                const complex a = out[k];
                const complex b = out[k + m] * root(k * scale);
                out[k] = a + b;
                out[k + m] = a - b;
            }
            return;
        }
        std::array<complex, 5> t{};
        for (int k = 0; k < m; ++k)
        {
            for (int q = 0; q < p; ++q)
            {
                t[static_cast<std::size_t>(q)] = out[(q * m) + k] * root(q * k * scale);
            }
            for (int r = 0; r < p; ++r)
            {
                complex sum = t[0];
                for (int q = 1; q < p; ++q)
                {
                    sum += t[static_cast<std::size_t>(q)] * root(((q * r) % p) * m * scale);
                }
                out[k + (r * m)] = sum;
            }
        }
    }

    // 2D transform of a w x h row-major grid: every row, then every column
    static void transform_2d(std::vector<complex>& grid, const plan& rows, const plan& cols, bool inverse)
    {
        const int w = rows.size();
        const int h = cols.size();
        parallel::for_range(0, h, [&](int begin, int end) {
            std::vector<complex> line(static_cast<std::size_t>(w));
            for (int y = begin; y < end; ++y)
            {
                const std::span<complex> row(grid.data() + (static_cast<std::ptrdiff_t>(y) * w), line.size());
                std::ranges::copy(row, line.begin());
                rows.run(line, row, inverse);
            }
        });
        parallel::for_range(0, w, [&](int begin, int end) {
            std::vector<complex> line(static_cast<std::size_t>(h));
            std::vector<complex> out(line.size());
            for (int x = begin; x < end; ++x)
            {
                for (int y = 0; y < h; ++y)
                {
                    line[static_cast<std::size_t>(y)] = grid[(static_cast<std::size_t>(y) * w) + x];
                }
                cols.run(line, out, inverse);
                for (int y = 0; y < h; ++y)
                {
                    grid[(static_cast<std::size_t>(y) * w) + x] = out[static_cast<std::size_t>(y)];
                }
            }
        });
    }

    void convolve(const image& img, const image& kernel, edge mode)
    {
        const int w = img.bounds.width();
        const int h = img.bounds.height();
        const int rx = kernel.bounds.width() / 2;
        const int ry = kernel.bounds.height() / 2;
        if (w <= 0 || h <= 0)
        {
            return;
        }
        // padding by the kernel reach keeps the circular wrap out of the pixels we keep
        const plan rows(good_size(w + (2 * rx)));
        const plan cols(good_size(h + (2 * ry)));
        const int gw = rows.size();
        const int gh = cols.size();
        const auto cell = [gw](int x, int y) { return (static_cast<std::size_t>(y) * gw) + x; };

        std::vector<complex> spectrum(static_cast<std::size_t>(gw) * gh);
        for (int y = 0; y < kernel.bounds.height(); ++y)
        {
            for (int x = 0; x < kernel.bounds.width(); ++x)
            {
                const int gx = (x - rx + gw) % gw;
                const int gy = (y - ry + gh) % gh;
                spectrum[cell(gx, gy)] = *kernel.at(kernel.bounds.x0 + x, kernel.bounds.y0 + y);
            }
        }
        transform_2d(spectrum, rows, cols, false);

        // what the convolution reads at padded position i of a line of n pixels; false is transparent
        const auto source = [mode](int& i, int n) {
            switch (mode)
            {
            case edge::transparent:
                return i >= 0 && i < n;
            case edge::repeat:
                i = std::clamp(i, 0, n - 1);
                return true;
            case edge::reflect:
                return sample::resolve(i, 0, n, sample::wrap::reflect);
            }
            return false;
        };
        const float norm = 1.0f / static_cast<float>(gw * gh);
        std::vector<complex> grid(spectrum.size());
        // two channels ride in one complex grid: the kernel is real, so they stay apart
        for (int c = 0; c < img.channels; c += 2)
        {
            const bool pair = c + 1 < img.channels;
            parallel::for_range(0, gh, [&](int begin, int end) {
                for (int y = begin; y < end; ++y)
                {
                    for (int x = 0; x < gw; ++x)
                    {
                        int sx = x - rx;
                        int sy = y - ry;
                        complex v{};
                        if (x < w + (2 * rx) && y < h + (2 * ry) && source(sx, w) && source(sy, h))
                        {
                            const float* s = img.at(img.bounds.x0 + sx, img.bounds.y0 + sy);
                            v = {s[c], pair ? s[c + 1] : 0.0f};
                        }
                        grid[cell(x, y)] = v;
                    }
                }
            });
            transform_2d(grid, rows, cols, false);
            std::ranges::transform(grid, spectrum, grid.begin(), std::multiplies{});
            transform_2d(grid, rows, cols, true);
            parallel::for_range(0, h, [&](int begin, int end) {
                for (int y = begin; y < end; ++y)
                {
                    for (int x = 0; x < w; ++x)
                    {
                        const complex v = grid[cell(x + rx, y + ry)] * norm;
                        float* d = img.at(img.bounds.x0 + x, img.bounds.y0 + y);
                        d[c] = v.real();
                        if (pair)
                        {
                            d[c + 1] = v.imag();
                        }
                    }
                }
            });
        }
    }
} // namespace functional_fx_suite::core::fft
