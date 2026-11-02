#include "effects/psykoblobs.hpp"
#include "core/mask.hpp"
#include "core/noise.hpp"
#include "core/psyko.hpp"
#include <algorithm>
#include <array>

namespace functional_fx_suite::effects::psykoblobs
{
    enum class p : std::uint8_t
    {
        noise_freq,
        noise_freq_relx,
        noise_octaves,
        noise_seed,
        noise_shift_x,
        noise_shift_y,
        source_blur,
        source_scale,
        freq_colors,
        freq_red,
        freq_green,
        freq_blue,
        phase_start,
        phase_speed,
        phase_red,
        phase_green,
        phase_blue,
        brightness,
        color,
        offset,
        saturation,
        scale_by_source,
        scale_by_src_amp,
        mix_with_source,
        mask_use,
        blur_mask,
        invert_mask,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    // Noise Shift is in xy units
    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "noise_freq", "Noise Freq").value(4).range(0.01, UNBOUNDED).slider(0.1, 20),
        param::scalar(2, "noise_freq_relx", "Noise Freq Relx").value(1).range(0.01, UNBOUNDED).slider(0.1, 10),
        param::scalar(3, "noise_octaves", "Noise Octaves").value(1).range(1, 10).slider(1, 10).digits(0),
        param::scalar(4, "noise_seed", "Noise Seed").value(0.23).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(5, "noise_shift_x", "Noise Shift X").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(6, "noise_shift_y", "Noise Shift Y").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(7, "source_blur", "Source Blur").value(0.088).range(0, UNBOUNDED).slider(0, 1),
        param::scalar(8, "source_scale", "Source Scale").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(9, "freq_colors", "Freq Colors").value(4).range(-UNBOUNDED, UNBOUNDED).slider(0.1, 20),
        param::scalar(10, "freq_red", "Freq Red").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(11, "freq_green", "Freq Green").value(1.1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2.2),
        param::scalar(12, "freq_blue", "Freq Blue").value(1.2).range(-UNBOUNDED, UNBOUNDED).slider(0, 2.4),
        param::scalar(13, "phase_start", "Phase Start").value(0.5).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(14, "phase_speed", "Phase Speed").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(15, "phase_red", "Phase Red").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(16, "phase_green", "Phase Green").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(17, "phase_blue", "Phase Blue").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(18, "brightness", "Brightness").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::color(19, "color", "Color").value({1, 1, 1}),
        param::scalar(20, "offset", "Offset").value(0).range(-8, 2).slider(-1, 1),
        param::scalar(21, "saturation", "Saturation").value(1).range(-UNBOUNDED, 10).slider(0, 2),
        param::scalar(22, "scale_by_source", "Scale By Source").value(0).range(0, 1),
        param::scalar(23, "scale_by_src_amp", "Scale By Src Amp").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 10),
        param::scalar(24, "mix_with_source", "Mix With Source").value(0).range(0, 1),
        param::choice(25, "mask_use", "Mask Use", "Luma|Alpha"),
        param::scalar(26, "blur_mask", "Blur Mask").value(0.05).range(0, 1000).slider(0, 2),
        param::toggle(27, "invert_mask", "Invert Mask"),
    };

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const double ph = v.scalar(p::phase_start) + (v.scalar(p::phase_speed) * args.time);
        const double fc = v.scalar(p::freq_colors);
        const auto& color = v.vec(p::color);
        const auto brightness = v.scalar(p::brightness);
        const core::psyko::settings s{
            .freq = {fc * v.scalar(p::freq_red), fc * v.scalar(p::freq_green), fc * v.scalar(p::freq_blue)},
            .phase = {ph + v.scalar(p::phase_red), ph + v.scalar(p::phase_green), ph + v.scalar(p::phase_blue)},
            .tint = {static_cast<float>(brightness * color[0]), static_cast<float>(brightness * color[1]),
                     static_cast<float>(brightness * color[2])},
            .saturation = v.scalar_f(p::saturation),
            .scale_by_source = v.scalar_f(p::scale_by_source),
            .scale_by_src_amp = v.scalar_f(p::scale_by_src_amp),
            .offset = v.scalar_f(p::offset),
            .mix = v.scalar_f(p::mix_with_source),
            .source_blur = v.scalar(p::source_blur),
            .source_scale = v.scalar(p::source_scale),
        };
        const double fy = v.scalar(p::noise_freq);
        const double fx = fy * v.scalar(p::noise_freq_relx);
        const double sx = v.scalar(p::noise_shift_x);
        const double sy = v.scalar(p::noise_shift_y);
        const int octaves = std::clamp(v.choice(p::noise_octaves), 1, 10);
        const std::uint32_t seed = core::noise::seed_bits(v.scalar(p::noise_seed));
        core::psyko::render(args, s, [&](const core::warp::vec2& xy) {
            return core::noise::fbm_2d(seed, (xy[0] + sx) * fx, (xy[1] + sy) * fy, octaves);
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
            .match_name = "f_psykoblobs",
            .name = "f_psykoblobs",
            .category = "Functional Render",
            .description =
                "Fills the clip with cycling color bands that follow fractal noise plus the source brightness.",
            .params = PARAMS,
            .mask_input = "Mask",
            .background_input = "",
            .time_varying = true,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::psykoblobs
