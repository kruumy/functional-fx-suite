#include "effects/zdefocus.hpp"
#include "core/blur.hpp"
#include "core/bokeh.hpp"
#include "core/color.hpp"
#include "core/fft.hpp"
#include "core/parallel.hpp"
#include "core/zdepth.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace functional_fx_suite::effects::zdefocus
{
    enum class p : std::uint8_t
    {
        focal_depth,
        depth_of_field,
        defocus_width,
        rel_height,
        z_buffer_type,
        show,
        layers,
        layer_mode,
        lens,
        shape,
        roundness,
        rotate,
        bokeh,
        lens_noise,
        noise_freq,
        noise_freq_rel_x,
        noise_seed,
        lens_end,
        use_gamma,
        boost_highlights,
        highlight_threshold,
        brightness,
        offset_darks,
        mix_with_source,
        width_rel_near,
        width_rel_far,
        fog_near,
        fog_far,
        fog_color,
        edge_mode,
        zbuffer_use,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "focal_depth", "Focal Depth").value(0).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(2, "depth_of_field", "Depth Of Field").value(0.1).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(3, "defocus_width", "Defocus Width").value(0.35).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(4, "rel_height", "Rel Height").value(1).range(0.01, UNBOUNDED).slider(0.1, 5),
        param::choice(5, "z_buffer_type", "Z Buffer Type", "Black is Near|White is Near").value(1),
        param::choice(6, "show", "Show", "Result|In Focus Zone|Shape"),
        param::scalar(7, "layers", "Layers").value(5).range(2, 50).slider(2, 50).digits(0),
        param::choice(8, "layer_mode", "Layer Mode", "Comp|Interp").value(1),
        param::group(9, "lens", "Lens"),
        param::choice(10, "shape", "Shape",
                      "Circle|3 sides|4 sides|5 sides|6 sides|7 sides|8 sides|9 sides|10 sides|11 sides|12 sides"),
        param::scalar(11, "roundness", "Roundness").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(12, "rotate", "Rotate").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-180, 180),
        param::scalar(13, "bokeh", "Bokeh").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(14, "lens_noise", "Lens Noise").value(0).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(15, "noise_freq", "Noise Freq").value(40).range(0.01, UNBOUNDED).slider(0.1, 50),
        param::scalar(16, "noise_freq_rel_x", "Noise Freq Rel X").value(1).range(0.01, UNBOUNDED).slider(0.1, 10),
        param::scalar(17, "noise_seed", "Noise Seed").value(0.123).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::group_end(18, "lens_end"),
        param::scalar(19, "use_gamma", "Use Gamma").value(1).range(0.1, UNBOUNDED).slider(0.1, 2),
        param::scalar(20, "boost_highlights", "Boost Highlights").value(0).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(21, "highlight_threshold", "Highlight Threshold")
            .value(0.9)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 1),
        param::scalar(22, "brightness", "Brightness").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(23, "offset_darks", "Offset Darks").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-5, 1),
        param::scalar(24, "mix_with_source", "Mix With Source").value(0).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(25, "width_rel_near", "Width Rel Near").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(26, "width_rel_far", "Width Rel Far").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(27, "fog_near", "Fog Near").value(0).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(28, "fog_far", "Fog Far").value(0).range(-UNBOUNDED, 1).slider(0, 1),
        param::color(29, "fog_color", "Fog Color").value({0.5, 0.5, 0.5}),
        param::choice(30, "edge_mode", "Edge Mode", "Transparent|Repeat|Reflect").value(2),
        param::choice(31, "zbuffer_use", "ZBuffer Use", "Luma|Alpha"),
    };

    // the lens shape at a defocus width (frame units, before Rel Height)
    static core::bokeh::lens lens(const core::param_values& v, const core::frame& f, double width)
    {
        const int shape = v.choice(p::shape);
        const double freq = v.scalar(p::noise_freq);
        return {
            .radius_x = core::blur::amount_px(width, f),
            .radius_y = core::blur::amount_px(width * v.scalar(p::rel_height), f),
            .sides = shape == 0 ? 0 : shape + 2,
            .pointiness = shape == 0 ? 0.0 : 1.0 - v.scalar(p::roundness),
            .rotate = v.scalar(p::rotate),
            .bokeh = v.scalar(p::bokeh),
            .noise_amp = v.scalar(p::lens_noise),
            .noise_freq_x = freq * v.scalar(p::noise_freq_rel_x),
            .noise_freq_y = freq,
            .noise_seed = v.scalar(p::noise_seed),
        };
    }

    // Show Shape: the kernel at Defocus Width, scaled to a peak of 1 and centred on the frame
    static void show_shape(const core::render_args& args, const core::param_values& v)
    {
        const core::buffer k = core::bokeh::kernel(lens(v, args.geometry, v.scalar(p::defocus_width)));
        const core::image& kv = k.view();
        const float peak =
            *std::max_element(kv.data, kv.data + (static_cast<std::ptrdiff_t>(kv.bounds.width()) * kv.bounds.height()));
        const int cx = static_cast<int>(std::floor(args.geometry.cx));
        const int cy = static_cast<int>(std::floor(args.geometry.cy));
        const core::image& dst = args.dst;
        core::parallel::for_range(dst.bounds.y0, dst.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                for (int x = dst.bounds.x0; x < dst.bounds.x1; ++x)
                {
                    const float value = kv.bounds.contains(x - cx, y - cy) ? *kv.at(x - cx, y - cy) / peak : 0.0f;
                    float* o = dst.at(x, y);
                    std::fill_n(o, 3, value);
                    o[3] = 1.0f;
                }
            }
        });
    }

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const core::buffer z =
            core::zdepth::depth(args, v.choice(p::z_buffer_type) == 1, v.choice(p::zbuffer_use) == 1);
        const double focal = v.scalar(p::focal_depth);
        const double dof = v.scalar(p::depth_of_field);
        if (v.choice(p::show) == 1)
        {
            core::zdepth::show_zone(args, z.view(), focal, dof);
            return;
        }
        if (v.choice(p::show) == 2)
        {
            show_shape(args, v);
            return;
        }

        // fog, then linearize with Use Gamma and boost highlights by how defocused each pixel will be
        const core::buffer fogged =
            core::zdepth::fog(args.src, z.view(), v.scalar_f(p::fog_near), v.scalar_f(p::fog_far), v.vec(p::fog_color));
        const core::image& s = fogged.view();
        const float use_gamma = v.scalar_f(p::use_gamma);
        const double width = v.scalar(p::defocus_width);
        const double boost = v.scalar(p::boost_highlights);
        const float threshold = v.scalar_f(p::highlight_threshold);
        const core::zdepth::layers layers = core::zdepth::read_layers<p>(v);
        core::parallel::for_range(s.bounds.y0, s.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                for (int x = s.bounds.x0; x < s.bounds.x1; ++x)
                {
                    float* o = s.at(x, y);
                    std::transform(o, o + 3, o, [use_gamma](float c) { return core::color::gamma(c, use_gamma); });
                    // distance outside the focus zone, scaled near or far
                    const double focus = core::zdepth::focus_distance(layers, *z.view().at(x, y));
                    // boost highlights: rgb x (1 + amount x max(0, max(rgb) - threshold))
                    const double size = width * focus;
                    const auto amount = static_cast<float>(1000.0 * boost * boost * size * size);
                    const float gain = 1.0f + (amount * std::max(0.0f, std::max({o[0], o[1], o[2]}) - threshold));
                    std::transform(o, o + 3, o, [gain](float c) { return c * gain; });
                }
            }
        });

        const auto edge = static_cast<core::fft::edge>(v.choice(p::edge_mode));
        core::zdepth::composite(args, s, z.view(), layers, [&](const core::image& layer, double out_of_focus) {
            const core::buffer k = core::bokeh::kernel(lens(v, args.geometry, 2.0 * width * out_of_focus));
            if (k.view().bounds.width() > 1)
            {
                core::fft::convolve(layer, k.view(), edge);
            }
        });

        // undo the gamma, scale lights and offset darks, then mix the source back in
        const float inv_gamma = 1.0f / use_gamma;
        const float lights = v.scalar_f(p::brightness);
        const float darks = v.scalar_f(p::offset_darks);
        const float mix = v.scalar_f(p::mix_with_source);
        const core::image& dst = args.dst;
        core::parallel::for_range(dst.bounds.y0, dst.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                for (int x = dst.bounds.x0; x < dst.bounds.x1; ++x)
                {
                    if (!args.src.bounds.contains(x, y))
                    {
                        continue;
                    }
                    float* o = dst.at(x, y);
                    const float* src = args.src.at(x, y);
                    for (int c = 0; c < 4; ++c)
                    {
                        const float d = c < 3 ? core::color::scale_lights_offset_darks(
                                                    core::color::gamma(o[c], inv_gamma), lights, darks)
                                              : o[c];
                        o[c] = core::color::lerp(d, src[c], mix);
                    }
                }
            }
        });
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_zdefocus",
            .name = "f_zdefocus",
            .category = "Functional Blur",
            .description = "Defocuses the clip by depth from a Z buffer layer with a lens-shaped blur, sharp at the "
                           "focal depth, with bokeh highlights and optional fog.",
            .params = PARAMS,
            .mask_input = "",
            .background_input = "ZBuffer",
            .time_varying = false,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::zdefocus
