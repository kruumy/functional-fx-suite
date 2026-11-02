#include "core/rays.hpp"
#include "core/blur.hpp"
#include "core/buffer.hpp"
#include "core/color.hpp"
#include "core/mask.hpp"
#include "core/noise.hpp"
#include "core/parallel.hpp"
#include "core/sample.hpp"
#include "effects/rays.hpp"
#include <algorithm>
#include <array>

namespace functional_fx_suite::effects::rays
{
    enum class p : std::uint8_t
    {
        style,
        center,
        rays_length,
        length_red,
        length_green,
        length_blue,
        reverse_rays,
        rays_brightness,
        rays_darkness,
        blur_rays,
        blur_rays_rel_x,
        blur_rays_rel_y,
        rays_color,
        rays_color_dark,
        bias_outer_bright,
        rays_res,
        threshold,
        threshold_add_color,
        shimmer_group,
        shimmer_amp,
        shimmer_freq,
        shimmer_seed,
        shimmer_shift_x,
        shimmer_shift_y,
        shimmer_speed_x,
        shimmer_speed_y,
        shimmer_group_end,
        atmosphere_group,
        atmosphere_amp,
        atmosphere_freq,
        atmosphere_detail,
        atmosphere_speed,
        atmosphere_group_end,
        affect_alpha,
        rays_from_alpha,
        rays_under_source,
        source_opacity,
        bg_brightness,
        use_source_chroma,
        matte_type,
        blur_matte,
        invert_matte,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;
    static constexpr int STYLE = static_cast<int>(p::style);
    static constexpr std::uint32_t LIGHT_ONLY = 1U << 0;
    static constexpr std::uint32_t DARK_ONLY = 1U << 1;

    // Dark Rays uses its own brightness and color params (black by default), so both exist here and the style
    // enables one
    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::choice(1, "style", "Style", "Light Rays|Dark Rays"),
        param::point(2, "center", "Center").value({0, 0, 0}),
        param::scalar(3, "rays_length", "Rays Length").value(0.25).range(-5, 1).slider(-1, 1),
        param::scalar(4, "length_red", "Length Red").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(5, "length_green", "Length Green").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(6, "length_blue", "Length Blue").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(7, "reverse_rays", "Reverse Rays").value(0).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(8, "rays_brightness", "Rays Brightness")
            .value(3)
            .range(0, UNBOUNDED)
            .slider(0, 20)
            .enabled_when(STYLE, LIGHT_ONLY),
        param::scalar(41, "rays_darkness", "Rays Darkness")
            .value(3)
            .range(0, UNBOUNDED)
            .slider(0, 20)
            .enabled_when(STYLE, DARK_ONLY),
        param::scalar(9, "blur_rays", "Blur Rays").value(0).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(10, "blur_rays_rel_x", "Blur Rays Rel X").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(11, "blur_rays_rel_y", "Blur Rays Rel Y").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::color(12, "rays_color", "Rays Color").value({1, 1, 1}).enabled_when(STYLE, LIGHT_ONLY),
        param::color(42, "rays_color_dark", "Rays Color (Dark)").value({0, 0, 0}).enabled_when(STYLE, DARK_ONLY),
        param::scalar(13, "bias_outer_bright", "Bias Outer Bright").value(0).range(0, 1),
        param::choice(14, "rays_res", "Rays Res", "Full|Half|Quarter"),
        param::scalar(15, "threshold", "Threshold").value(0.5).range(0, UNBOUNDED).slider(0, 1),
        param::color(16, "threshold_add_color", "Threshold Add Color").value({0, 0, 0}),
        param::group(17, "shimmer", "Shimmer"),
        param::scalar(18, "shimmer_amp", "Shimmer Amp").value(0.5).range(0, UNBOUNDED).slider(0, 5),
        param::scalar(19, "shimmer_freq", "Shimmer Freq").value(40).range(0.1, UNBOUNDED).slider(0.1, 50),
        param::scalar(20, "shimmer_seed", "Shimmer Seed").value(0.123).range(0, UNBOUNDED).slider(0, 1),
        param::scalar(21, "shimmer_shift_x", "Shimmer Shift X").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(22, "shimmer_shift_y", "Shimmer Shift Y").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(23, "shimmer_speed_x", "Shimmer Speed X").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(24, "shimmer_speed_y", "Shimmer Speed Y").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::group_end(25, "shimmer_end"),
        param::group(26, "atmosphere", "Atmosphere"),
        param::scalar(27, "atmosphere_amp", "Atmosphere Amp").value(0).range(0, UNBOUNDED).slider(0, 5),
        param::scalar(28, "atmosphere_freq", "Atmosphere Freq").value(1).range(0.1, 20),
        param::scalar(29, "atmosphere_detail", "Atmosphere Detail").value(0.6).range(0, 1),
        param::scalar(30, "atmosphere_speed", "Atmosphere Speed").value(1).range(-UNBOUNDED, UNBOUNDED).slider(-5, 5),
        param::group_end(31, "atmosphere_end"),
        param::scalar(32, "affect_alpha", "Affect Alpha").value(0).range(0, 1),
        param::scalar(33, "rays_from_alpha", "Rays From Alpha").value(0).range(0, 1),
        param::scalar(34, "rays_under_source", "Rays Under Source").value(0).range(0, 1),
        param::scalar(35, "source_opacity", "Source Opacity").value(1).range(0, 1),
        param::scalar(36, "bg_brightness", "Bg Brightness").value(1).range(0, UNBOUNDED).slider(0, 1),
        param::scalar(37, "use_source_chroma", "Use Source Chroma").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::choice(38, "matte_type", "Matte Type", "Luma|Color|Alpha"),
        param::scalar(39, "blur_matte", "Blur Matte").value(0).range(0, UNBOUNDED).slider(0, 2),
        param::toggle(40, "invert_matte", "Invert Matte"),
    };

    using rgb = core::rays::rgb;

    struct settings
    {
        bool dark = false;
        core::rays::path path;
        rgb threshold{};
        core::rays::shimmer shimmer;
        double from_alpha = 0;
        double source_chroma = 1;
    };

    static settings read_settings(const core::param_values& v, double time)
    {
        settings s;
        s.dark = v.choice(p::style) == 1;
        s.path.cx = v.vec(p::center)[0];
        s.path.cy = v.vec(p::center)[1];
        s.path.lengths = core::rays::channel_lengths(
            v.scalar(p::rays_length), {v.scalar(p::length_red), v.scalar(p::length_green), v.scalar(p::length_blue)},
            s.dark);
        s.path.shape = core::rays::ray_shape{v.scalar(p::reverse_rays), v.scalar(p::bias_outer_bright),
                                             v.scalar(s.dark ? p::rays_darkness : p::rays_brightness)};
        s.path.res_level = std::clamp(v.choice(p::rays_res), 0, 2);
        for (int c = 0; c < 3; ++c)
        {
            const double t = v.scalar(p::threshold) + v.vec(p::threshold_add_color)[c];
            s.threshold[c] = s.dark ? 1.0 - t : t;
        }
        s.shimmer = core::rays::shimmer{v.scalar(p::shimmer_amp), v.scalar(p::shimmer_freq),
                                        core::noise::seed_bits(v.scalar(p::shimmer_seed)),
                                        v.scalar(p::shimmer_shift_x) + time * v.scalar(p::shimmer_speed_x),
                                        v.scalar(p::shimmer_shift_y) + time * v.scalar(p::shimmer_speed_y)};
        s.from_alpha = v.scalar(p::rays_from_alpha);
        s.source_chroma = v.scalar(p::use_source_chroma);
        return s;
    }

    // thresholded, shimmer-modulated, matted source that the rays are streaked from
    static core::buffer generator(const core::render_args& args, const settings& s, const core::image* matte)
    {
        core::buffer gen(args.src.bounds, 4);
        const core::image& g = gen.view();
        const bool matte_color = matte != nullptr && matte->channels == 3;
        const bool use_color = matte_color || s.source_chroma * (1.0 - s.from_alpha) != 0.0;
        const auto chroma = static_cast<float>(s.source_chroma);
        const auto from_alpha = static_cast<float>(s.from_alpha);
        core::parallel::for_range(g.bounds.y0, g.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                const double ny = args.geometry.to_ny(y + 0.5);
                for (int x = g.bounds.x0; x < g.bounds.x1; ++x)
                {
                    const float* src = args.src.at(x, y);
                    // hotspots: the source color scaled by how far it rises above the threshold,
                    // summed over channels, relative to its own brightness (keeps the source hue)
                    std::array<float, 3> v{};
                    float excess = 0.0f;
                    float total = 0.0f;
                    for (int c = 0; c < 3; ++c)
                    {
                        const float value = s.dark ? 1.0f - src[c] : src[c];
                        v[c] = core::color::lerp(value, src[3], from_alpha);
                        excess += std::max(v[c] - static_cast<float>(s.threshold[c]), 0.0f);
                        total += std::max(v[c], 0.0f);
                    }
                    const float k = excess > 0.0f && total > 0.0f ? excess / total : 0.0f;
                    const std::array<float, 3> hot{v[0] * k, v[1] * k, v[2] * k};
                    const float mono = core::color::luma(hot[0], hot[1], hot[2]);
                    const float shimmer = s.shimmer.at(args.geometry.to_nx(x + 0.5), ny);
                    const float* m = matte != nullptr ? matte->at(x, y) : nullptr;
                    float* d = g.at(x, y);
                    for (int c = 0; c < 3; ++c)
                    {
                        const float base = use_color ? core::color::lerp(mono, hot[c], chroma) : mono;
                        const float matte_value = m == nullptr ? 1.0f : m[matte_color ? c : 0];
                        d[c] = shimmer * base * matte_value;
                    }
                }
            }
        });
        return gen;
    }

    // final rays layer (rgb: light or dark rays, alpha: light rays coverage) over the output window
    static core::buffer finish(const core::render_args& args, const settings& s, const core::image& grid)
    {
        const core::param_values& v = *args.params;
        const double blur_width = v.scalar(p::blur_rays);
        // blurring needs the surroundings, so cover the whole source in that case
        const core::rect area = blur_width > 0.0 ? args.src.bounds : args.dst.bounds;
        core::buffer out(area, 4);
        const core::image& o = out.view();
        const auto& color = v.vec(s.dark ? p::rays_color_dark : p::rays_color);
        const auto affect_alpha = v.scalar_f(p::affect_alpha);
        const double atmos_amp = v.scalar(p::atmosphere_amp);
        const double atmos_freq = v.scalar(p::atmosphere_freq);
        const double atmos_detail = v.scalar(p::atmosphere_detail);
        const double atmos_z = v.scalar(p::atmosphere_speed) * (args.time + 1.0) * 0.1;
        const std::uint32_t atmos_seed = core::noise::combine(s.shimmer.seed, 0xa7a7U);
        constexpr int ATMOS_OCTAVES = 8;
        const double step = static_cast<double>(1 << s.path.res_level);
        const core::rect& b = args.src.bounds;

        core::parallel::for_range(area.y0, area.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                const double ny = args.geometry.to_ny(y + 0.5);
                for (int x = area.x0; x < area.x1; ++x)
                {
                    const auto r = core::sample::bilinear(grid, (x + 0.5 - b.x0) / step, (y + 0.5 - b.y0) / step,
                                                          core::sample::wrap::none);
                    float atmos = 1.0f;
                    if (atmos_amp != 0.0)
                    {
                        const double nx = args.geometry.to_nx(x + 0.5);
                        atmos = static_cast<float>(1.0 - atmos_amp * core::noise::fbm_3d(atmos_seed, nx * atmos_freq,
                                                                                         ny * atmos_freq, atmos_z,
                                                                                         ATMOS_OCTAVES, atmos_detail));
                    }
                    float* d = o.at(x, y);
                    float brightest = 0.0f;
                    for (int c = 0; c < 3; ++c)
                    {
                        const auto light = static_cast<float>(color[c]) * r[c];
                        const auto dark = static_cast<float>(1.0 - color[c]) * r[c];
                        brightest = std::max(brightest, light);
                        d[c] = (s.dark ? dark : light) * atmos;
                    }
                    d[3] = std::clamp(brightest, 0.0f, 1.0f) * affect_alpha * atmos;
                }
            }
        });
        if (blur_width > 0.0)
        {
            const double amount = core::blur::amount_px(blur_width, args.geometry);
            core::blur::gaussian(o, amount * v.scalar(p::blur_rays_rel_x), amount * v.scalar(p::blur_rays_rel_y));
        }
        return out;
    }

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const settings s = read_settings(v, args.time);

        static constexpr std::array MATTE_SOURCES{core::mask::source::luma, core::mask::source::color,
                                                  core::mask::source::alpha};
        const core::buffer matte = core::mask::build(args, MATTE_SOURCES[std::clamp(v.choice(p::matte_type), 0, 2)],
                                                     v.scalar(p::blur_matte), v.toggle(p::invert_matte));
        const core::image* matte_view = matte.view().data != nullptr ? &matte.view() : nullptr;

        const core::buffer gen = generator(args, s, matte_view);
        const core::buffer grid = core::rays::streak(args, s.path, gen.view());
        const core::buffer rays = finish(args, s, grid.view());
        core::rays::composite(args, rays.view(), s.dark ? &rays.view() : nullptr, 1.0, v.scalar(p::rays_under_source),
                              v.scalar(p::source_opacity), v.scalar(p::bg_brightness));
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_rays",
            .name = "f_rays",
            .category = "Functional Lighting",
            .description = "Streaks beams of light from the bright areas of the clip, radiating from a center point.",
            .params = PARAMS,
            .mask_input = "Matte",
            .background_input = "Background",
            .time_varying = true,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::rays
