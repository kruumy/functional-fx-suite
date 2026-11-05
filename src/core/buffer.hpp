#pragma once

#include "core/image.hpp"
#include "core/parallel.hpp"
#include <algorithm>
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
        // a copy of `src`'s pixels with its bounds and channels
        [[nodiscard]] static buffer copy_of(const image& src)
        {
            buffer out(src.bounds, src.channels);
            const auto row = static_cast<std::size_t>(src.bounds.width()) * src.channels;
            parallel::for_range(src.bounds.y0, src.bounds.y1, [&](int y_begin, int y_end) {
                for (int y = y_begin; y < y_end; ++y)
                {
                    std::copy_n(src.at(src.bounds.x0, y), row, out.view_.at(src.bounds.x0, y));
                }
            });
            return out;
        }

        [[nodiscard]] const image& view() const
        {
            return view_;
        }
        // the view, or null when nothing is stored (an unconnected optional input)
        [[nodiscard]] const image* view_if_set() const
        {
            return view_.data != nullptr ? &view_ : nullptr;
        }

      private:
        std::vector<float> store_;
        image view_;
    };
} // namespace functional_fx_suite::core
