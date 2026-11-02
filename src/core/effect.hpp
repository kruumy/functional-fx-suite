#pragma once

#include "core/param.hpp"
#include "core/render_args.hpp"
#include <span>
#include <string_view>
#include <vector>

namespace functional_fx_suite::core
{
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
    };
} // namespace functional_fx_suite::core
