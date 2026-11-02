#include "effects/scanlines.hpp"
#include "core/blur.hpp"
#include "core/color.hpp"
#include "core/edges.hpp"
#include "core/mask.hpp"
#include "core/noise.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace functional_fx_suite::effects::scanlines
{
    enum class p : std::uint8_t
    {
        lines_frequency,
        lines_sharpness,
        lines_angle,
        lines_shift,
        shift_red,
        shift_green,
        shift_blue,
        add_noise,
        noise_freq_rel,
        brightness,
        scale_color,
        offset,
        gamma,
        saturation,
        smooth_source,
        mask_use,
        blur_mask,
        invert_mask,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    // Lines Angle is in degrees
    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "lines_frequency", "Lines Frequency").value(50).range(1, UNBOUNDED).slider(1, 500),
        param::scalar(2, "lines_sharpness", "Lines Sharpness").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(3, "lines_angle", "Lines Angle").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-180, 180),
        param::scalar(4, "lines_shift", "Lines Shift").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(5, "shift_red", "Shift Red").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(6, "shift_green", "Shift Green").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(7, "shift_blue", "Shift Blue").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(8, "add_noise", "Add Noise").value(0).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(9, "noise_freq_rel", "Noise Freq Rel").value(1).range(0.01, UNBOUNDED).slider(0.01, 2),
        param::scalar(10, "brightness", "Brightness").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::color(11, "scale_color", "Scale Color").value({1, 1, 1}),
        param::scalar(12, "offset", "Offset").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(13, "gamma", "Gamma").value(1.5).range(0.1, UNBOUNDED).slider(0.1, 3),
        param::scalar(14, "saturation", "Saturation").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(15, "smooth_source", "Smooth Source").value(0).range(0, UNBOUNDED).slider(0, 0.2),
        param::choice(16, "mask_use", "Mask Use", "Luma|Alpha"),
        param::scalar(17, "blur_mask", "Blur Mask").value(0.05).range(0, 1000).slider(0, 2),
        param::toggle(18, "invert_mask", "Invert Mask"),
    };

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const core::frame& f = args.geometry;
        const core::buffer straight = core::edges::straight_rgb(args.src);
        const core::image& s = straight.view();
        const double amount = core::blur::amount_px(v.scalar(p::smooth_source), f);
        core::blur::gaussian(s, amount, amount);

        const float sharpness = v.scalar_f(p::lines_sharpness);
        const float power = core::color::dissolve(1.0f, v.scalar_f(p::gamma), sharpness);
        const float brightness = v.scalar_f(p::brightness);
        const auto& scale = v.vec(p::scale_color);
        const float offset = v.scalar_f(p::offset);
        const float saturation = v.scalar_f(p::saturation);
        const double freq = v.scalar(p::lines_frequency);
        const std::array shift{v.scalar(p::lines_shift) + v.scalar(p::shift_red),
                               v.scalar(p::lines_shift) + v.scalar(p::shift_green),
                               v.scalar(p::lines_shift) + v.scalar(p::shift_blue)};
        const double angle = -v.scalar(p::lines_angle) * std::numbers::pi / 180.0;
        const double sin_t = std::sin(angle);
        const double cos_t = std::cos(angle);

        // the noise is reseeded every frame
        const float add_noise = v.scalar_f(p::add_noise);
        const double noise_freq = freq * v.scalar(p::noise_freq_rel);
        const auto frame_number = static_cast<double>(std::lround(args.time / args.frame_duration));
        std::array<std::uint32_t, 3> seeds{};
        for (int c = 0; c < 3; ++c)
        {
            seeds[c] = core::noise::seed_bits(frame_number + c / 3.0);
        }

        const core::image& dst = args.dst;
        core::parallel::for_range(dst.bounds.y0, dst.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                const double ry = core::XY_PER_FRAME_UNIT * f.to_ny(y + 0.5);
                for (int x = dst.bounds.x0; x < dst.bounds.x1; ++x)
                {
                    float* d = dst.at(x, y);
                    if (!args.src.bounds.contains(x, y))
                    {
                        std::fill_n(d, 4, 0.0f);
                        continue;
                    }
                    const double rx = core::XY_PER_FRAME_UNIT * f.to_nx(x + 0.5);
                    const double across = (rx * sin_t + ry * cos_t) * freq;
                    const float* c_in = s.at(x, y);
                    core::color::rgb out{};
                    for (int c = 0; c < 3; ++c)
                    {
                        float noise = 0.0f;
                        if (add_noise != 0.0f)
                        {
                            const auto n =
                                static_cast<float>(core::noise::value_2d(seeds[c], rx * noise_freq, ry * noise_freq));
                            noise = add_noise * (n - 0.5f);
                        }
                        const float i2 =
                            2.0f * (core::color::gamma(c_in[c] * brightness * static_cast<float>(scale[c]), power) +
                                    noise + offset);
                        const auto wave =
                            static_cast<float>(0.5 + 0.5 * std::sin(2.0 * std::numbers::pi * (across + shift[c])));
                        const float lines = wave * sharpness - (sharpness - 1.0f) * 0.5f;
                        out[c] = core::color::dissolve(std::max(0.0f, i2 - 1.0f), std::min(1.0f, i2), lines);
                    }
                    out = core::color::sat_scale(out, saturation);
                    const float a = args.src.at(x, y)[3];
                    std::ranges::transform(out, d, [a](float c) { return c * a; });
                    d[3] = a;
                }
            }
        });

        const auto mode = v.choice(p::mask_use) == 1 ? core::mask::source::alpha : core::mask::source::luma;
        const core::buffer m = core::mask::build(args, mode, v.scalar(p::blur_mask), v.toggle(p::invert_mask));
        if (m.view().data != nullptr)
        {
            core::mask::dissolve(args, m.view());
        }
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_scanlines",
            .name = "f_scanlines",
            .category = "Functional Stylize",
            .description = "Turns the clip into video scan lines whose thickness follows its brightness.",
            .params = PARAMS,
            .mask_input = "Mask",
            .background_input = "",
            .time_varying = true,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::scanlines
