#include "effects/edgeextend.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace functional_fx_suite::effects::edgeextend
{
    enum class p : std::uint8_t
    {
        expand_left,
        expand_right,
        expand_up,
        expand_down,
        count
    };

    using core::param;

    // full-resolution pixels; the cap keeps a maxed HD layer's float buffer near 0.5 GB
    static constexpr double MAX_EXPAND = 2000;

    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "expand_left", "Expand Left").value(0).range(0, MAX_EXPAND).slider(0, 1000).digits(0),
        param::scalar(2, "expand_right", "Expand Right").value(0).range(0, MAX_EXPAND).slider(0, 1000).digits(0),
        param::scalar(3, "expand_up", "Expand Up").value(0).range(0, MAX_EXPAND).slider(0, 1000).digits(0),
        param::scalar(4, "expand_down", "Expand Down").value(0).range(0, MAX_EXPAND).slider(0, 1000).digits(0),
    };

    static core::rect output_bounds(const core::param_values& v, const core::rect& src, core::pixel_scale scale)
    {
        const auto px = [&](p side, double s) { return static_cast<int>(std::ceil(v.scalar(side) * s)); };
        return {src.x0 - px(p::expand_left, scale.x), src.y0 - px(p::expand_up, scale.y),
                src.x1 + px(p::expand_right, scale.x), src.y1 + px(p::expand_down, scale.y)};
    }

    static void render(const core::render_args& args)
    {
        const core::image& src = args.src;
        const core::image& dst = args.dst;
        const core::rect& s = src.bounds;
        // the columns of dst the source covers; the rest repeat the source's first or last column
        const int inner0 = std::clamp(s.x0, dst.bounds.x0, dst.bounds.x1);
        const int inner1 = std::clamp(s.x1, dst.bounds.x0, dst.bounds.x1);
        // the host sizes dst to output_bounds, so every dst pixel stretches from the nearest edge
        core::parallel::for_range(dst.bounds.y0, dst.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                // an empty source has no edge to stretch
                if (s.empty())
                {
                    std::fill_n(dst.at(dst.bounds.x0, y), static_cast<std::size_t>(dst.bounds.width()) * 4, 0.0f);
                    continue;
                }
                const int sy = std::clamp(y, s.y0, s.y1 - 1);
                for (int x = dst.bounds.x0; x < inner0; ++x)
                {
                    std::copy_n(src.at(s.x0, sy), 4, dst.at(x, y));
                }
                if (inner1 > inner0)
                {
                    std::copy_n(src.at(inner0, sy), static_cast<std::size_t>(inner1 - inner0) * 4, dst.at(inner0, y));
                }
                for (int x = inner1; x < dst.bounds.x1; ++x)
                {
                    std::copy_n(src.at(s.x1 - 1, sy), 4, dst.at(x, y));
                }
            }
        });
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_edgeextend",
            .name = "f_edgeextend",
            .category = "Functional Stylize",
            .description = "Grows the layer by stretching its edge pixels outward.",
            .params = PARAMS,
            .mask_input = "",
            .background_input = "",
            .time_varying = false,
            .reads_outside = true,
            .render = render,
            .output_bounds = output_bounds,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::edgeextend
