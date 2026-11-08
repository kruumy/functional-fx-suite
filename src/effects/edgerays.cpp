#include "effects/edgerays.hpp"
#include "core/blur.hpp"
#include "core/buffer.hpp"
#include "core/color.hpp"
#include "core/edges.hpp"
#include "core/mask.hpp"
#include "core/noise.hpp"
#include "core/parallel.hpp"
#include "core/rays.hpp"
#include "core/sample.hpp"
#include <algorithm>
#include <array>

namespace functional_fx_suite::effects::edgerays
{
    enum class p : std::uint8_t
    {
        center,
        rays_length,
        length_red,
        length_green,
        length_blue,
        reverse_rays,
        rays_brightness,
        blur_rays,
        blur_rays_rel_x,
        blur_rays_rel_y,
        rays_color,
        enable_dark_rays,
        bias_outer_bright,
        rays_res,
        show,
        edge_thickness,
        edge_brightness,
        edge_subpixel,
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
        matte_type,
        blur_matte,
        invert_matte,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::point(1, "center", "Center").value({0, 0, 0}),
        param::scalar(2, "rays_length", "Rays Length").value(0.25).range(-5, 1).slider(-1, 1),
        param::scalar(3, "length_red", "Length Red").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(4, "length_green", "Length Green").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(5, "length_blue", "Length Blue").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(6, "reverse_rays", "Reverse Rays").value(0).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(7, "rays_brightness", "Rays Brightness").value(2).range(0, UNBOUNDED).slider(0, 20),
        param::scalar(8, "blur_rays", "Blur Rays").value(0).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(9, "blur_rays_rel_x", "Blur Rays Rel X").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(10, "blur_rays_rel_y", "Blur Rays Rel Y").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::color(11, "rays_color", "Rays Color").value({1, 1, 1}),
        param::toggle(12, "enable_dark_rays", "Enable Dark Rays"),
        param::scalar(13, "bias_outer_bright", "Bias Outer Bright").value(0).range(0, 1),
        param::choice(14, "rays_res", "Rays Res", "Full|Half|Quarter"),
        param::choice(15, "show", "Show", "Result|Edges|Rays"),
        param::scalar(16, "edge_thickness", "Edge Thickness").value(0.022).range(0, UNBOUNDED).slider(0, 1),
        param::scalar(17, "edge_brightness", "Edge Brightness").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::toggle(18, "edge_subpixel", "Edge Subpixel").value(1),
        param::group(19, "shimmer", "Shimmer"),
        param::scalar(20, "shimmer_amp", "Shimmer Amp").value(0.5).range(0, UNBOUNDED).slider(0, 5),
        param::scalar(21, "shimmer_freq", "Shimmer Freq").value(40).range(0.1, UNBOUNDED).slider(0.1, 50),
        param::scalar(22, "shimmer_seed", "Shimmer Seed").value(0.123).range(0, UNBOUNDED).slider(0, 1),
        param::scalar(23, "shimmer_shift_x", "Shimmer Shift X").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(24, "shimmer_shift_y", "Shimmer Shift Y").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(25, "shimmer_speed_x", "Shimmer Speed X").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(26, "shimmer_speed_y", "Shimmer Speed Y").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::group_end(27, "shimmer_end"),
        param::group(28, "atmosphere", "Atmosphere"),
        param::scalar(29, "atmosphere_amp", "Atmosphere Amp").value(0).range(0, UNBOUNDED).slider(0, 5),
        param::scalar(30, "atmosphere_freq", "Atmosphere Freq").value(1).range(0.1, 20),
        param::scalar(31, "atmosphere_detail", "Atmosphere Detail").value(0.6).range(0, 1),
        param::scalar(32, "atmosphere_speed", "Atmosphere Speed").value(1).range(-UNBOUNDED, UNBOUNDED).slider(-5, 5),
        param::group_end(33, "atmosphere_end"),
        param::scalar(34, "affect_alpha", "Affect Alpha").value(0).range(0, UNBOUNDED).slider(0, 1),
        param::scalar(35, "rays_from_alpha", "Rays From Alpha").value(0).range(0, 1),
        param::scalar(36, "rays_under_source", "Rays Under Source").value(0).range(0, 1),
        param::scalar(37, "source_opacity", "Source Opacity").value(1).range(0, 1),
        param::choice(38, "matte_type", "Matte Type", "Luma|Color|Alpha"),
        param::scalar(39, "blur_matte", "Blur Matte").value(0).range(0, UNBOUNDED).slider(0, 2),
        param::toggle(40, "invert_matte", "Invert Matte"),
    };

    // rgb: shimmering edges times the matte, the rays' source; alpha: the shimmering edges alone (Show Edges)
    static core::buffer generator(const core::render_args& args, const core::image* matte)
    {
        const core::param_values& v = *args.params;
        const core::rect& b = args.src.bounds;
        core::buffer level(b, 1);
        const core::image& l = level.view();
        const float from_alpha = v.scalar_f(p::rays_from_alpha);
        core::parallel::for_pixels(b, [&](int x, int y) {
            const float* s = args.src.at(x, y);
            *l.at(x, y) = core::color::lerp(core::color::luma(s[0], s[1], s[2]), s[3], from_alpha);
        });
        const double thickness = v.scalar(p::edge_thickness);
        core::edges::prepare(l, thickness, v.scalar(p::edge_brightness) * (thickness + 0.1), args.geometry);
        const core::buffer edges = core::edges::magnitude(l);

        const core::rays::shimmer shimmer = core::rays::read_shimmer<p>(v, args.time);
        core::buffer gen(b, 4);
        const core::image& g = gen.view();
        core::parallel::for_range(b.y0, b.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                const double ny = args.geometry.to_ny(y + 0.5);
                for (int x = b.x0; x < b.x1; ++x)
                {
                    const float e =
                        std::min(1.0f, *edges.view().at(x, y)) * shimmer.at(args.geometry.to_nx(x + 0.5), ny);
                    const float* m = matte != nullptr && matte->bounds.contains(x, y) ? matte->at(x, y) : nullptr;
                    float* d = g.at(x, y);
                    for (int c = 0; c < 3; ++c)
                    {
                        d[c] = m == nullptr ? e : e * m[matte->channels == 3 ? c : 0];
                    }
                    d[3] = e;
                }
            }
        });
        return gen;
    }

    // the streaked rays upsampled to `area`, times the atmosphere, blurred by Blur Rays
    static core::buffer upsample(const core::render_args& args, const core::image& grid, int res_level)
    {
        const core::param_values& v = *args.params;
        const double blur_width = v.scalar(p::blur_rays);
        // blurring needs the surroundings, so cover the whole source too; the output can reach past the source
        const core::rect area = blur_width > 0.0 ? args.src.bounds.united(args.dst.bounds) : args.dst.bounds;
        core::buffer out(area, 4);
        const core::image& o = out.view();
        const double atmos_amp = v.scalar(p::atmosphere_amp);
        const double atmos_freq = v.scalar(p::atmosphere_freq);
        const double atmos_detail = v.scalar(p::atmosphere_detail);
        const double atmos_z = v.scalar(p::atmosphere_speed) * (args.time + 1.0) * 0.1;
        const std::uint32_t atmos_seed =
            core::noise::combine(core::noise::seed_bits(v.scalar(p::shimmer_seed)), 0xa7a7U);
        constexpr int ATMOS_OCTAVES = 8;
        const double step = static_cast<double>(1 << res_level);
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
                    for (int c = 0; c < 3; ++c)
                    {
                        d[c] = r[c] * atmos;
                    }
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

    // Show Edges / Show Rays: the image as is, opaque
    static void show_opaque(const core::render_args& args, const core::image& img, bool mono)
    {
        const core::image& dst = args.dst;
        core::parallel::for_pixels(dst.bounds, [&](int x, int y) {
            float* d = dst.at(x, y);
            const float* s = img.bounds.contains(x, y) ? img.at(x, y) : nullptr;
            for (int c = 0; c < 3; ++c)
            {
                d[c] = s == nullptr ? 0.0f : s[mono ? 3 : c];
            }
            d[3] = 1.0f;
        });
    }

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const core::buffer matte = core::mask::build(args, core::mask::luma_color_alpha(v.choice(p::matte_type)),
                                                     v.scalar(p::blur_matte), v.toggle(p::invert_matte));
        const core::buffer gen = generator(args, matte.view_if_set());
        const int show = v.choice(p::show);
        if (show == 1)
        {
            show_opaque(args, gen.view(), true);
            return;
        }

        const bool dark = v.toggle(p::enable_dark_rays);
        const core::rays::path path = core::rays::read_path<p>(v, dark, v.scalar(p::rays_brightness));
        const core::buffer grid = core::rays::streak(args, path, gen.view());
        const core::buffer rays = upsample(args, grid.view(), path.res_level);

        // light rays take Rays Color and alpha from their brightest channel; dark rays take its inverse
        const core::image& light = rays.view();
        core::buffer dark_rays(light.bounds, 4);
        const auto& color = v.vec(p::rays_color);
        const auto affect_alpha = v.scalar_f(p::affect_alpha);
        core::parallel::for_pixels(light.bounds, [&](int x, int y) {
            float* l = light.at(x, y);
            float* d = dark_rays.view().at(x, y);
            float brightest = 0.0f;
            for (int c = 0; c < 3; ++c)
            {
                d[c] = static_cast<float>(1.0 - color[c]) * l[c];
                l[c] *= static_cast<float>(color[c]);
                brightest = std::max(brightest, l[c]);
            }
            l[3] = std::clamp(brightest, 0.0f, 1.0f) * affect_alpha;
        });
        if (show == 2)
        {
            show_opaque(args, light, false);
            return;
        }
        const double light_amount = std::clamp(std::ranges::max(color), 0.0, 1.0);
        core::rays::composite(args, light, dark ? &dark_rays.view() : nullptr, 1.0 - light_amount,
                              v.scalar(p::rays_under_source), v.scalar(p::source_opacity), 1.0);
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_edgerays",
            .name = "f_edgerays",
            .category = "Functional Lighting",
            .description = "Streaks beams of light from the edges in the clip, radiating from a center point.",
            .params = PARAMS,
            .mask_input = "Matte",
            .background_input = "Background",
            .time_varying = true,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::edgerays
