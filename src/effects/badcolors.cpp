#include "effects/badcolors.hpp"
#include "core/color.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <array>

namespace functional_fx_suite::effects::badcolors
{
    enum class p : std::uint8_t
    {
        min,
        max,
        min_luma,
        max_luma,
        min_chroma,
        max_chroma,
        min_rgb,
        max_rgb,
        high_color,
        low_color,
        output_matte,
        invert_matte,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "min", "Min").value(0).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(2, "max", "Max").value(1).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(3, "min_luma", "Min Luma").value(0).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(4, "max_luma", "Max Luma").value(1).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(5, "min_chroma", "Min Chroma").value(0).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(6, "max_chroma", "Max Chroma").value(1).range(-UNBOUNDED, 1).slider(0, 1),
        param::color(7, "min_rgb", "Min RGB").value({0, 0, 0}),
        param::color(8, "max_rgb", "Max RGB").value({1, 1, 1}),
        param::color(9, "high_color", "High Color").value({1, 0, 0}),
        param::color(10, "low_color", "Low Color").value({0, 0, 1}),
        param::toggle(11, "output_matte", "Output Matte"),
        param::toggle(12, "invert_matte", "Invert Matte"),
    };

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        std::array<float, 3> lo{};
        std::array<float, 3> hi{};
        for (int c = 0; c < 3; ++c)
        {
            lo[c] = static_cast<float>(std::max(v.scalar(p::min), v.vec(p::min_rgb)[c]));
            hi[c] = static_cast<float>(std::min(v.scalar(p::max), v.vec(p::max_rgb)[c]));
        }
        const float min_luma = v.scalar_f(p::min_luma);
        const float max_luma = v.scalar_f(p::max_luma);
        const float min_chroma = v.scalar_f(p::min_chroma);
        const float max_chroma = v.scalar_f(p::max_chroma);
        const auto& high_color = v.vec(p::high_color);
        const auto& low_color = v.vec(p::low_color);
        const bool output_matte = v.toggle(p::output_matte);
        const bool invert = v.toggle(p::invert_matte);

        const core::image& dst = args.dst;
        core::parallel::for_pixels(dst.bounds, [&](int x, int y) {
            float* d = dst.at(x, y);
            if (!args.src.bounds.contains(x, y))
            {
                std::fill_n(d, 4, 0.0f);
                return;
            }
            const float* s = args.src.at(x, y);
            const float a = s[3];
            const float inv = a > 0.0f ? 1.0f / a : 0.0f;
            const std::array<float, 3> rgb{s[0] * inv, s[1] * inv, s[2] * inv};
            const float luma = core::color::luma(rgb[0], rgb[1], rgb[2]);
            const float chroma = std::ranges::max(rgb) - std::ranges::min(rgb);
            // a pixel above any upper limit is high; otherwise below any lower limit is low
            bool high = luma > max_luma || chroma > max_chroma;
            bool low = min_luma > luma || min_chroma > chroma;
            for (int c = 0; c < 3; ++c)
            {
                high = high || rgb[c] > hi[c];
                low = low || lo[c] > rgb[c];
            }
            low = low && !high;
            const bool flagged = high || low;
            for (int c = 0; c < 3; ++c)
            {
                float out = flagged != invert ? 1.0f : 0.0f;
                if (!output_matte)
                {
                    out = high ? static_cast<float>(high_color[c]) : low ? static_cast<float>(low_color[c]) : rgb[c];
                }
                d[c] = out * a;
            }
            d[3] = a;
        });
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_badcolors",
            .name = "f_badcolors",
            .category = "Functional Adjust",
            .description = "Flags the pixels of the clip that fall outside a color range.",
            .params = PARAMS,
            .mask_input = "",
            .background_input = "",
            .time_varying = false,
            .reads_outside = false,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::badcolors
