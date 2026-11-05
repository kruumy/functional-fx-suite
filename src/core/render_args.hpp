#pragma once

#include "core/buffer.hpp"
#include "core/frame.hpp"
#include "core/image.hpp"
#include "core/param_values.hpp"
#include <cmath>
#include <functional>

namespace functional_fx_suite::core
{
    // everything an effect needs for one render; all images are float RGBA premultiplied
    struct render_args
    {
        image src;                         // full source frame
        image dst;                         // output window, same pixel space as src
        const image* mask = nullptr;       // same pixel space as src; null when not connected
        const image* background = nullptr; // same pixel space as src; null when not connected
        const param_values* params = nullptr;
        frame geometry;
        double time = 0.0;                  // seconds
        double frame_duration = 1.0 / 24.0; // seconds
        // param values at another time in seconds; empty when the host only has the current ones
        std::function<param_values(double)> params_at;
        // the source `offset` frames away, empty if unlisted or missing; fetch each once, as AE frees it after reading
        std::function<buffer(int)> frame_at;

        // the current frame number, as passed to the effect's frame_offsets
        [[nodiscard]] int frame_number() const
        {
            return static_cast<int>(std::lround(time / frame_duration));
        }
    };
} // namespace functional_fx_suite::core
