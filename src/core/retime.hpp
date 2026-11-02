#pragma once

#include "core/mask.hpp"
#include "core/render_args.hpp"

namespace functional_fx_suite::core::retime
{
    // first frame of the `length`-frame block holding `frame`, blocks counted from `start`
    [[nodiscard]] int block_start(int frame, int length, int start);

    // dst = the source `offset` frames from now, transparent where the host has none; then the auto mask dissolves it
    // back to the source
    void render(const render_args& args, int offset, mask::source mask_use, double blur_mask, bool invert_mask);
} // namespace functional_fx_suite::core::retime
