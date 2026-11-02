#pragma once

#include "core/image.hpp"
#include <complex>
#include <cstdint>
#include <span>
#include <vector>

namespace functional_fx_suite::core::fft
{
    using complex = std::complex<float>;

    // smallest length >= n with no prime factor above 5
    [[nodiscard]] int good_size(int n);

    // mixed-radix (2, 3, 5) transform of one length; forward is exp(-i...), inverse is unscaled
    class plan
    {
      public:
        explicit plan(int n);

        [[nodiscard]] int size() const
        {
            return n_;
        }
        // in / out are size() long and must not overlap
        void run(std::span<const complex> in, std::span<complex> out, bool inverse) const;

      private:
        void step(const complex* in, std::ptrdiff_t stride, complex* out, int n, std::size_t f, bool inverse) const;

        int n_;
        std::vector<int> factors_;
        std::vector<complex> roots_; // exp(-2 pi i k / n)
    };

    // what the convolution sees past the image edge; order matches the "Edge Mode" popup
    enum class edge : std::uint8_t
    {
        transparent,
        repeat,
        reflect
    };

    // convolves every channel of img in place with a 1-channel kernel of odd size, centred on its middle pixel
    void convolve(const image& img, const image& kernel, edge mode);
} // namespace functional_fx_suite::core::fft
