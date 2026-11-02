#pragma once

#include "core/image.hpp"
#include <vector>

namespace functional_fx_suite::core
{
    // zero-initialized storage backing an image view
    class buffer
    {
      public:
        buffer() = default;
        buffer(rect r, int channels)
        {
            reset(r, channels);
        }
        buffer(const buffer&) = delete;
        buffer& operator=(const buffer&) = delete;
        buffer(buffer&&) = default;
        buffer& operator=(buffer&&) = default;

        void reset(rect r, int channels)
        {
            const auto row_floats = static_cast<std::ptrdiff_t>(r.width()) * channels;
            store_.assign(static_cast<std::size_t>(row_floats) * r.height(), 0.0f);
            view_ = image{store_.data(), r, row_floats, channels};
        }
        [[nodiscard]] const image& view() const
        {
            return view_;
        }

      private:
        std::vector<float> store_;
        image view_;
    };
} // namespace functional_fx_suite::core
