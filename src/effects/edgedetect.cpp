#include "effects/edgedetect.hpp"
#include "core/color.hpp"
#include "core/edges.hpp"
#include "core/mask.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <array>

namespace functional_fx_suite::effects::edgedetect
{
    enum class p : std::uint8_t
    {
        style,
        edge_smooth,
        subpixel_smooth,
        brightness,
        saturation,
        threshold,
        weight_red,
        weight_green,
        weight_blue,
        mask_use,
        blur_mask,
        invert_mask,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;
    static constexpr int STYLE = static_cast<int>(p::style);
    static constexpr std::uint32_t NOT_MONO = 0b011;

    // the three styles share every parameter; Mono ignores Saturation
    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::choice(1, "style", "Style", "RGB Edges|Chroma Edges|Mono Edges"),
        param::scalar(2, "edge_smooth", "Edge Smooth").value(0.0224).range(0, UNBOUNDED).slider(0, 1),
        param::toggle(3, "subpixel_smooth", "Subpixel Smooth").value(1),
        param::scalar(4, "brightness", "Brightness").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 10),
        param::scalar(5, "saturation", "Saturation")
            .value(1)
            .range(-UNBOUNDED, 10)
            .slider(0, 2)
            .enabled_when(STYLE, NOT_MONO),
        param::scalar(6, "threshold", "Threshold").value(0).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(7, "weight_red", "Weight Red").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(8, "weight_green", "Weight Green").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(9, "weight_blue", "Weight Blue").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::choice(10, "mask_use", "Mask Use", "Luma|Alpha"),
        param::scalar(11, "blur_mask", "Blur Mask").value(0.05).range(0, 1000).slider(0, 2),
        param::toggle(12, "invert_mask", "Invert Mask"),
    };

    enum class style : std::uint8_t
    {
        rgb,
        chroma,
        mono
    };

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const auto mode = static_cast<style>(v.choice(p::style));
        const std::array weight{v.scalar_f(p::weight_red), v.scalar_f(p::weight_green), v.scalar_f(p::weight_blue)};
        const core::buffer straight = core::edges::straight_rgb(args.src);
        const core::image& s = straight.view();

        // what the edges are found in: weighted rgb, its difference from gray, or its monochrome
        core::buffer input(s.bounds, mode == style::mono ? 1 : 3);
        const core::image& in = input.view();
        core::parallel::for_range(s.bounds.y0, s.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                for (int x = s.bounds.x0; x < s.bounds.x1; ++x)
                {
                    const float* c = s.at(x, y);
                    float* d = in.at(x, y);
                    const float ave = mode == style::chroma ? (c[0] + c[1] + c[2]) / 3.0f : 0.0f;
                    const std::array w{(c[0] - ave) * weight[0], (c[1] - ave) * weight[1], (c[2] - ave) * weight[2]};
                    if (mode == style::mono)
                    {
                        d[0] = core::color::luma(w[0], w[1], w[2]);
                        continue;
                    }
                    std::ranges::copy(w, d);
                }
            }
        });
        const double smooth = v.scalar(p::edge_smooth);
        core::edges::prepare(in, smooth, v.scalar(p::brightness) * (smooth + 0.1), args.geometry);
        const core::buffer edges = core::edges::magnitude(in);

        const float threshold = v.scalar_f(p::threshold);
        const float saturation = v.scalar_f(p::saturation);
        const core::image& dst = args.dst;
        core::parallel::for_range(dst.bounds.y0, dst.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                for (int x = dst.bounds.x0; x < dst.bounds.x1; ++x)
                {
                    float* d = dst.at(x, y);
                    if (!args.src.bounds.contains(x, y))
                    {
                        std::fill_n(d, 4, 0.0f);
                        continue;
                    }
                    const float* e = edges.view().at(x, y);
                    core::color::rgb out{};
                    switch (mode)
                    {
                    case style::rgb:
                        out = core::color::sat_scale({e[0], e[1], e[2]}, saturation);
                        std::ranges::for_each(out, [threshold](float& c) { c -= threshold; });
                        break;
                    case style::chroma: {
                        const core::color::rgb above{std::max(e[0] - threshold, 0.0f), std::max(e[1] - threshold, 0.0f),
                                                     std::max(e[2] - threshold, 0.0f)};
                        const float ave = (above[0] + above[1] + above[2]) / 3.0f;
                        std::ranges::transform(above, out.begin(),
                                               [&](float c) { return core::color::lerp(ave, c, saturation); });
                        break;
                    }
                    case style::mono:
                        out.fill(e[0] - threshold);
                        break;
                    }
                    const float a = args.src.at(x, y)[3];
                    std::ranges::transform(out, d, [a](float c) { return c * a; });
                    d[3] = a;
                }
            }
        });

        core::mask::apply(args, core::mask::luma_or_alpha(v.choice(p::mask_use)), v.scalar(p::blur_mask),
                          v.toggle(p::invert_mask));
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_edgedetect",
            .name = "f_edgedetect",
            .category = "Functional Stylize",
            .description = "Finds the edges in the clip, in color, in chroma only or in brightness only.",
            .params = PARAMS,
            .mask_input = "Mask",
            .background_input = "",
            .time_varying = false,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::edgedetect
