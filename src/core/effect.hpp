#pragma once

#include "core/param.hpp"
#include "core/render_args.hpp"
#include <span>
#include <string_view>
#include <vector>

namespace functional_fx_suite::core
{
    // pixels per full-resolution pixel along each axis, below 1 for downsampled renders
    struct pixel_scale
    {
        double x = 1;
        double y = 1;
    };

    // static description of one effect plus its render entry point
    struct effect
    {
        std::string_view match_name; // host-visible id, e.g. "f_shake"
        std::string_view name;
        std::string_view category;
        std::string_view description;
        std::span<const param> params;
        std::string_view mask_input;       // label of the optional mask layer, empty if none
        std::string_view background_input; // label of the optional background layer, empty if none
        bool time_varying = false;         // output changes over time with constant params
        bool reads_outside = false;        // samples source pixels outside the output window
        void (*render)(const render_args&) = nullptr;
        // distinct non-zero frame offsets render reads through render_args::frame_at, given the current frame number
        // (render_args::frame_number); null when it reads none
        std::vector<int> (*frame_offsets)(const param_values&, int frame) = nullptr;
        // the output's bounds for a source spanning `src`, in the same pixel space; null when the effect doesn't grow
        // the layer, and set only with reads_outside, which the hosts rely on to fetch the whole source
        rect (*output_bounds)(const param_values&, const rect& src, pixel_scale scale) = nullptr;

        // frame_offsets, or none when the effect reads no other frames
        [[nodiscard]] std::vector<int> offsets(const param_values& v, int frame) const
        {
            return frame_offsets != nullptr ? frame_offsets(v, frame) : std::vector<int>{};
        }
    };
} // namespace functional_fx_suite::core
