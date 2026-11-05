#pragma once

#include "core/mask.hpp"
#include "core/render_args.hpp"
#include <vector>

namespace functional_fx_suite::core::retime
{
    // first frame of the `length`-frame block holding `frame`, blocks counted from `start`
    [[nodiscard]] int block_start(int frame, int length, int start);

    // frame_offsets for an effect that reads one other frame: `offset`, or none when it is the current one
    [[nodiscard]] inline std::vector<int> offsets(int offset)
    {
        return offset != 0 ? std::vector<int>{offset} : std::vector<int>{};
    }

    // dst = the source `offset` frames from now, transparent where the host has none; then the auto mask dissolves it
    // back to the source
    void render(const render_args& args, int offset, mask::source mask_use, double blur_mask, bool invert_mask);
} // namespace functional_fx_suite::core::retime
