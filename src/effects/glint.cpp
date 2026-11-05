#include "effects/glint.hpp"
#include "core/color.hpp"
#include "core/glow.hpp"
#include "core/mask.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace functional_fx_suite::effects::glint
{
    enum class p : std::uint8_t
    {
        brightness,
        hue_shift,
        brightness_group,
        scale_colors,
        brightness_x,
        brightness_y,
        brightness_diag1,
        brightness_diag2,
        brightness_group_end,
        threshold,
        threshold_add_color,
        threshold_blur,
        size,
        size_group,
        size_x,
        size_y,
        size_diag1,
        size_diag2,
        size_red,
        size_green,
        size_blue,
        size_group_end,
        blur_glint,
        affect_alpha,
        glint_from_alpha,
        glint_under_source,
        source_opacity,
        bg_brightness,
        invert_matte,
        matte_type,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "brightness", "Brightness").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(2, "hue_shift", "Hue Shift").value(0).range(-1, 1),
        param::group(3, "brightness_details", "Brightness Details"),
        param::color(4, "scale_colors", "Scale Colors").value({1, 1, 1}),
        param::scalar(5, "brightness_x", "Brightness X").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(6, "brightness_y", "Brightness Y").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(7, "brightness_diag1", "Brightness Diag1").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(8, "brightness_diag2", "Brightness Diag2").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::group_end(9, "brightness_details_end"),
        param::scalar(10, "threshold", "Threshold").value(0.7).range(0, UNBOUNDED).slider(0, 1),
        param::color(11, "threshold_add_color", "Threshold Add Color").value({0, 0, 0}),
        param::scalar(12, "threshold_blur", "Threshold Blur").value(0.0896).range(0, UNBOUNDED).slider(0, 3.2),
        param::scalar(13, "size", "Size").value(2).range(0, UNBOUNDED).slider(0, 2),
        param::group(14, "size_details", "Size Details"),
        param::scalar(15, "size_x", "Size X").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(16, "size_y", "Size Y").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(17, "size_diag1", "Size Diag1").value(0.75).range(0, UNBOUNDED).slider(0, 1.5),
        param::scalar(18, "size_diag2", "Size Diag2").value(0.75).range(0, UNBOUNDED).slider(0, 1.5),
        param::scalar(19, "size_red", "Size Red").value(0.5).range(0, UNBOUNDED).slider(0, 1),
        param::scalar(20, "size_green", "Size Green").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(21, "size_blue", "Size Blue").value(1.5).range(0, UNBOUNDED).slider(0, 3),
        param::group_end(22, "size_details_end"),
        param::scalar(23, "blur_glint", "Blur Glint").value(0).range(0, UNBOUNDED).slider(0, 0.8),
        param::scalar(24, "affect_alpha", "Affect Alpha").value(0).range(0, UNBOUNDED).slider(0, 1),
        param::scalar(25, "glint_from_alpha", "Glint From Alpha").value(0).range(0, 1),
        param::scalar(26, "glint_under_source", "Glint Under Source").value(0).range(0, 1),
        param::scalar(27, "source_opacity", "Source Opacity").value(1).range(0, 1),
        param::scalar(28, "bg_brightness", "Bg Brightness").value(1).range(0, UNBOUNDED).slider(0, 1),
        param::toggle(29, "invert_matte", "Invert Matte"),
        param::choice(30, "matte_type", "Matte Type", "Luma|Color|Alpha"),
    };

    // ray directions in pixel steps: x, y, then the two diagonals
    struct ray
    {
        int dx;
        int dy;
        double weight; // diagonal rays are brightened by this factor
    };
    static constexpr std::array<ray, 4> RAYS{{{1, 0, 1.0}, {0, 1, 1.0}, {1, -1, 1.333}, {1, 1, 1.333}}};

    // adds weight * (channel c blurred along one direction by a three-box cascade reaching `total` pixels) to out's c
    static void streak(const core::image& hot, const core::image& out, int c, const ray& r, int total, float weight)
    {
        const core::rect& b = hot.bounds;
        // a line starts at every pixel whose predecessor along the ray is outside the image
        std::vector<std::array<int, 2>> starts;
        for (int y = b.y0; y < b.y1; ++y)
        {
            for (int x = b.x0; x < b.x1; ++x)
            {
                if (!b.contains(x - r.dx, y - r.dy))
                {
                    starts.push_back({x, y});
                }
            }
        }
        const core::blur::radii radii = core::blur::split(total);
        core::parallel::for_range(0, static_cast<int>(starts.size()), [&](int begin, int end) {
            std::vector<float> line;
            std::vector<float> scratch;
            for (int i = begin; i < end; ++i)
            {
                line.clear();
                for (auto [x, y] = starts[i]; b.contains(x, y); x += r.dx, y += r.dy)
                {
                    line.push_back(hot.at(x, y)[c]);
                }
                const int n = static_cast<int>(line.size());
                scratch.resize(line.size());
                core::blur::box_line(line, scratch, n, 1, radii);
                auto [x, y] = starts[i];
                for (int k = 0; k < n; ++k, x += r.dx, y += r.dy)
                {
                    out.at(x, y)[c] += weight * line[k];
                }
            }
        });
    }

    static void composite(const core::render_args& args, const core::image& glint)
    {
        const core::param_values& v = *args.params;
        const auto affect_alpha = v.scalar_f(p::affect_alpha);
        const auto source_opacity = v.scalar_f(p::source_opacity);
        const auto bg_brightness = v.scalar_f(p::bg_brightness);
        const auto under = v.scalar_f(p::glint_under_source);
        const core::image* bg = args.background;
        const core::image& dst = args.dst;
        core::parallel::for_pixels(dst.bounds, [&](int x, int y) {
            std::array<float, 4> src{};
            if (args.src.bounds.contains(x, y))
            {
                const float* sp = args.src.at(x, y);
                std::ranges::transform(sp, sp + 4, src.begin(), [&](float c) { return c * source_opacity; });
            }
            std::array<float, 4> back{};
            if (bg != nullptr && bg->bounds.contains(x, y))
            {
                const float* bp = bg->at(x, y);
                back = {bp[0] * bg_brightness, bp[1] * bg_brightness, bp[2] * bg_brightness, bp[3]};
            }
            std::array<float, 4> g{};
            if (glint.bounds.contains(x, y))
            {
                std::copy_n(glint.at(x, y), 3, g.begin());
            }
            g[3] = std::max({g[0], g[1], g[2]}) * affect_alpha;
            // screened over source-over-background, or onto the background under the source
            float* d = dst.at(x, y);
            for (int c = 0; c < 4; ++c)
            {
                const float on_top = core::color::screen_super(src[c] + back[c] * (1.0f - src[3]), g[c]);
                const float below = src[c] + core::color::screen_super(back[c], g[c]) * (1.0f - src[3]);
                d[c] = core::color::lerp(on_top, below, under);
            }
        });
    }

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const core::buffer matte = core::mask::build(args, core::mask::luma_color_alpha(v.choice(p::matte_type)), 0.0,
                                                     v.toggle(p::invert_matte));
        const core::image* matte_view = matte.view_if_set();
        core::buffer hot = core::glow::source(args, matte_view, v.scalar(p::glint_from_alpha));
        const core::rect& b = hot.view().bounds;

        const auto& add = v.vec(p::threshold_add_color);
        const auto& scale_colors = v.vec(p::scale_colors);
        const std::array channel_size{v.scalar(p::size_red), v.scalar(p::size_green), v.scalar(p::size_blue)};
        const std::array ray_size{v.scalar(p::size_x), v.scalar(p::size_y), v.scalar(p::size_diag1),
                                  v.scalar(p::size_diag2)};
        const std::array ray_brightness{v.scalar(p::brightness_x), v.scalar(p::brightness_y),
                                        v.scalar(p::brightness_diag1), v.scalar(p::brightness_diag2)};
        // Threshold Blur is on a finer scale than the other widths (a quarter)
        constexpr double FINE_SCALE = 0.25;
        const double pre_blur = core::blur::amount_px(v.scalar(p::threshold_blur), args.geometry) * FINE_SCALE;
        constexpr double RAY_GAIN = 4.0;

        core::blur::gaussian(hot.view(), pre_blur, pre_blur);
        const double t = v.scalar(p::threshold);
        // all three channels at once, like f_glow, so bright pixels keep their hue
        core::glow::threshold(hot.view(), core::glow::thresholds(t, add));

        core::buffer glint(b, 4);
        for (int c = 0; c < 3; ++c)
        {
            // each ray: a streak as long as its size, brighter in proportion to its length
            const double size_px = core::blur::amount_px(v.scalar(p::size) * channel_size[c], args.geometry);
            for (std::size_t d = 0; d < RAYS.size(); ++d)
            {
                const double weight = RAY_GAIN * v.scalar(p::brightness) * scale_colors[c] * ray_brightness[d] *
                                      ray_size[d] * RAYS[d].weight;
                if (weight != 0.0)
                {
                    const int total = static_cast<int>(std::lround(ray_size[d] * size_px));
                    streak(hot.view(), glint.view(), c, RAYS[d], total, static_cast<float>(weight));
                }
            }
        }

        if (const auto hue = v.scalar_f(p::hue_shift); hue != 0.0f)
        {
            core::parallel::for_pixels(b, [&](int x, int y) {
                float* g = glint.view().at(x, y);
                const auto shifted = core::color::hue_shift_preserve_luma({g[0], g[1], g[2]}, hue);
                std::copy(shifted.begin(), shifted.end(), g);
            });
        }
        const double blur = core::blur::amount_px(v.scalar(p::blur_glint), args.geometry);
        core::blur::gaussian(glint.view(), blur, blur);
        composite(args, glint.view());
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_glint",
            .name = "f_glint",
            .category = "Functional Lighting",
            .description = "Generates star-shaped glints where the clip is brighter than a threshold.",
            .params = PARAMS,
            .mask_input = "Matte",
            .background_input = "Background",
            .time_varying = false,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::glint
