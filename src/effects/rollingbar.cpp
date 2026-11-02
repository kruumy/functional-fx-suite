#include "effects/rollingbar.hpp"
#include "core/color.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace functional_fx_suite::effects::rollingbar
{
    enum class p : std::uint8_t
    {
        rolling_flicker_factor,
        rolling_flicker_vsync_time,
        count
    };

    using core::param;

    // order, defaults and ranges follow the shader's UI
    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "rolling_flicker_factor", "Rolling Flicker Factor").value(0.25).range(0, 1),
        param::scalar(2, "rolling_flicker_vsync_time", "Rolling Flicker V Sync Time")
            .value(1)
            .range(-144, 144)
            .slider(-10, 10),
    };

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const core::frame& f = args.geometry;
        const float factor = v.scalar_f(p::rolling_flicker_factor);
        // the ramp scrolls down one frame height per second per unit of V Sync Time
        const double roll = args.time * v.scalar(p::rolling_flicker_vsync_time);
        const double height = args.src.bounds.height();
        const double top = f.cy - (height * 0.5);

        const core::image& dst = args.dst;
        core::parallel::for_range(dst.bounds.y0, dst.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                const double ramp = ((y + 0.5 - top) / height) - roll;
                const float gain = core::color::lerp(1.0f, static_cast<float>(ramp - std::floor(ramp)), factor);
                for (int x = dst.bounds.x0; x < dst.bounds.x1; ++x)
                {
                    float* d = dst.at(x, y);
                    if (!args.src.bounds.contains(x, y))
                    {
                        std::fill_n(d, 4, 0.0f);
                        continue;
                    }
                    const float* s = args.src.at(x, y);
                    for (int c = 0; c < 3; ++c)
                    {
                        d[c] = s[c] * gain;
                    }
                    d[3] = s[3];
                }
            }
        });
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_rollingbar",
            .name = "f_rollingbar",
            .category = "Functional Stylize",
            .description =
                "Darkens the clip with a brightness ramp that rolls down the frame, like a CRT filmed out of sync.",
            .params = PARAMS,
            .mask_input = "",
            .background_input = "",
            .time_varying = true,
            .reads_outside = false,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::rollingbar
