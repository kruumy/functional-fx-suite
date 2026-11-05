#include "effects/mosaic.hpp"
#include "core/blur.hpp"
#include "core/color.hpp"
#include "core/mask.hpp"
#include "core/parallel.hpp"
#include "core/sample.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace functional_fx_suite::effects::mosaic
{
    enum class p : std::uint8_t
    {
        pixel_frequency,
        pixel_rel_height,
        pixel_shift,
        smooth_colors,
        mix_with_source,
        blur_matte,
        invert_matte,
        matte_use,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    // Wrap is fixed to Reflect
    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "pixel_frequency", "Pixel Frequency").value(40).range(1, UNBOUNDED).slider(4, 200),
        param::scalar(2, "pixel_rel_height", "Pixel Rel Height").value(1).range(0.01, UNBOUNDED).slider(0.1, 10),
        param::point(3, "pixel_shift", "Pixel Shift").value({0, 0, 0}),
        param::scalar(4, "smooth_colors", "Smooth Colors").value(0).range(0, UNBOUNDED).slider(0, 0.2),
        param::scalar(5, "mix_with_source", "Mix With Source").value(0).range(0, 1),
        param::scalar(6, "blur_matte", "Blur Matte").value(0).range(0, 1000).slider(0, 2),
        param::toggle(7, "invert_matte", "Invert Matte"),
        param::choice(8, "matte_use", "Matte Use", "Luma|Alpha"),
    };

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const core::frame& f = args.geometry;
        const double freq = v.scalar(p::pixel_frequency);
        // a cell is 2 / frequency xy units, which is 1 / frequency frame widths
        const double cell_x = 1.0 / freq;
        const double cell_y = cell_x * v.scalar(p::pixel_rel_height);
        const double shift_x = v.vec(p::pixel_shift)[0];
        const double shift_y = v.vec(p::pixel_shift)[1];

        // works on premultiplied rgba
        const core::buffer smooth = core::buffer::copy_of(args.src);
        const core::image& s = smooth.view();
        const double amount = core::blur::amount_px(v.scalar(p::smooth_colors), f);
        core::blur::gaussian(s, amount, amount);

        const auto mode = core::mask::luma_or_alpha(v.choice(p::matte_use));
        const core::buffer m = core::mask::build(args, mode, v.scalar(p::blur_matte), v.toggle(p::invert_matte));
        const core::image& mask = m.view();
        const float mix = v.scalar_f(p::mix_with_source);

        const core::image& dst = args.dst;
        core::parallel::for_range(dst.bounds.y0, dst.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                const double ny = f.to_ny(y + 0.5) - shift_y;
                const double cy = (std::floor(ny / cell_y) + 0.5) * cell_y + shift_y;
                const double py = f.to_px_y(cy);
                for (int x = dst.bounds.x0; x < dst.bounds.x1; ++x)
                {
                    float* d = dst.at(x, y);
                    if (!args.src.bounds.contains(x, y))
                    {
                        std::fill_n(d, 4, 0.0f);
                        continue;
                    }
                    const double nx = f.to_nx(x + 0.5) - shift_x;
                    const double px = f.to_px_x((std::floor(nx / cell_x) + 0.5) * cell_x + shift_x);
                    const core::sample::pixel cell = core::sample::bilinear(s, px, py, core::sample::wrap::reflect);
                    // the matte is sampled at the cell centre too, so cells switch on or off whole
                    float k = 1.0f;
                    if (mask.data != nullptr)
                    {
                        const float* matte_px = core::sample::nearest(mask, px, py);
                        k = matte_px != nullptr ? *matte_px : 0.0f;
                    }
                    const float* src = args.src.at(x, y);
                    for (int c = 0; c < 4; ++c)
                    {
                        const float result = core::color::dissolve(src[c], cell[c], k);
                        d[c] = core::color::lerp(result, src[c], mix);
                    }
                }
            }
        });
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_mosaic",
            .name = "f_mosaic",
            .category = "Functional Stylize",
            .description = "Breaks the clip into a grid of solid-colored rectangles.",
            .params = PARAMS,
            .mask_input = "Matte",
            .background_input = "",
            .time_varying = false,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::mosaic
