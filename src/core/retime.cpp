#include "core/retime.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <cmath>

namespace functional_fx_suite::core::retime
{
    int block_start(int frame, int length, int start)
    {
        // a plain floor, also at exact negative multiples
        return start + (static_cast<int>(std::floor(static_cast<double>(frame - start) / length)) * length);
    }

    void render(const render_args& args, int offset, mask::source mask_use, double blur_mask, bool invert_mask)
    {
        const buffer fetched = offset != 0 ? args.frame_at(offset) : buffer{};
        const image& from = offset != 0 ? fetched.view() : args.src;
        const image& dst = args.dst;
        parallel::for_pixels(dst.bounds, [&](int x, int y) {
            float* d = dst.at(x, y);
            if (from.data != nullptr && from.bounds.contains(x, y))
            {
                std::copy_n(from.at(x, y), 4, d);
            }
            else
            {
                std::fill_n(d, 4, 0.0f);
            }
        });

        mask::apply(args, mask_use, blur_mask, invert_mask);
    }
} // namespace functional_fx_suite::core::retime
