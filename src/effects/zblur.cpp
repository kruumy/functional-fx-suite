#include "effects/zblur.hpp"
#include "core/blur.hpp"
#include "core/zdepth.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace functional_fx_suite::effects::zblur
{
    enum class p : std::uint8_t
    {
        focal_depth,
        depth_of_field,
        blur_width,
        blur_rel_x,
        blur_rel_y,
        z_buffer_type,
        show,
        layers,
        layer_mode,
        width_rel_near,
        width_rel_far,
        fog_near,
        fog_far,
        fog_color,
        zbuffer_use,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "focal_depth", "Focal Depth").value(0).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(2, "depth_of_field", "Depth Of Field").value(0.1).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(3, "blur_width", "Blur Width").value(0.168).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(4, "blur_rel_x", "Blur Rel X").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(5, "blur_rel_y", "Blur Rel Y").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::choice(6, "z_buffer_type", "Z Buffer Type", "Black is Near|White is Near").value(1),
        param::choice(7, "show", "Show", "Result|In Focus Zone"),
        param::scalar(8, "layers", "Layers").value(5).range(2, 50).slider(2, 50).digits(0),
        param::choice(9, "layer_mode", "Layer Mode", "Comp|Interp").value(1),
        param::scalar(10, "width_rel_near", "Width Rel Near").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(11, "width_rel_far", "Width Rel Far").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(12, "fog_near", "Fog Near").value(0).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(13, "fog_far", "Fog Far").value(0).range(-UNBOUNDED, 1).slider(0, 1),
        param::color(14, "fog_color", "Fog Color").value({0.5, 0.5, 0.5}),
        param::choice(15, "zbuffer_use", "ZBuffer Use", "Luma|Alpha"),
    };

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const core::buffer z =
            core::zdepth::depth(args, v.choice(p::z_buffer_type) == 1, v.choice(p::zbuffer_use) == 1);
        if (v.choice(p::show) == 1)
        {
            core::zdepth::show_zone(args, z.view(), v.scalar(p::focal_depth), v.scalar(p::depth_of_field));
            return;
        }

        // linear fog by depth, mixed into the source before the blur
        const core::buffer fogged =
            core::zdepth::fog(args.src, z.view(), v.scalar_f(p::fog_near), v.scalar_f(p::fog_far), v.vec(p::fog_color));
        const double width = 2.0 * v.scalar(p::blur_width);
        const double width_x = core::blur::amount_px(width * v.scalar(p::blur_rel_x), args.geometry);
        const double width_y = core::blur::amount_px(width * v.scalar(p::blur_rel_y), args.geometry);
        const core::zdepth::layers layers{
            .count = std::clamp(static_cast<int>(std::lround(v.scalar(p::layers))), 2, 50),
            .focal_depth = v.scalar(p::focal_depth),
            .depth_of_field = v.scalar(p::depth_of_field),
            .rel_near = v.scalar(p::width_rel_near),
            .rel_far = v.scalar(p::width_rel_far),
            .comp = v.choice(p::layer_mode) == 0,
        };
        core::zdepth::composite(
            args, fogged.view(), z.view(), layers, [&](const core::image& layer, double out_of_focus) {
                core::blur::gaussian(layer, width_x * out_of_focus, width_y * out_of_focus, core::blur::edge::reflect);
            });
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_zblur",
            .name = "f_zblur",
            .category = "Functional Blur",
            .description =
                "Blurs the clip by depth from a Z buffer layer, sharp at the focal depth, with optional fog.",
            .params = PARAMS,
            .mask_input = "",
            .background_input = "ZBuffer",
            .time_varying = false,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::zblur
