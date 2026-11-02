#pragma once

#include "core/buffer.hpp"
#include "core/sample.hpp"
#include <vector>

namespace functional_fx_suite::core
{
    // box-filtered pyramid of a 4-channel image for blurred lookups along long paths
    class mip
    {
      public:
        mip(const image& src, int max_levels);

        [[nodiscard]] int levels() const
        {
            return static_cast<int>(levels_.size());
        }
        [[nodiscard]] const image& level(int i) const
        {
            return levels_[i].view();
        }
        // bilinear lookup on one level at a continuous position in level-0 pixel coordinates
        [[nodiscard]] sample::pixel lookup_level(double x, double y, int level,
                                                 sample::wrap mode = sample::wrap::none) const;

      private:
        static buffer downsample(const image& src);

        std::vector<buffer> levels_;
        int origin_x_ = 0;
        int origin_y_ = 0;
    };
} // namespace functional_fx_suite::core
