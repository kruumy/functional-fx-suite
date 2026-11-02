#include "effects/psykostripes.hpp"
#include "core/mask.hpp"
#include "core/psyko.hpp"
#include <array>

namespace functional_fx_suite::effects::psykostripes
{
    enum class p : std::uint8_t
    {
        stripe_dir,
        stripe_mag,
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

    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "stripe_dir", "Stripe Dir").value(45).range(-UNBOUNDED, UNBOUNDED).slider(-180, 180),
        param::scalar(2, "stripe_mag", "Stripe Mag").value(0.5).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(3, "source_blur", "Source Blur").value(0.088).range(0, UNBOUNDED).slider(0, 1),
        param::scalar(4, "source_scale", "Source Scale").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(5, "freq_colors", "Freq Colors").value(3).range(-UNBOUNDED, UNBOUNDED).slider(0.1, 20),
        param::scalar(6, "freq_red", "Freq Red").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(7, "freq_green", "Freq Green").value(1.1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2.2),
        param::scalar(8, "freq_blue", "Freq Blue").value(1.2).range(-UNBOUNDED, UNBOUNDED).slider(0, 2.4),
        param::scalar(9, "phase_start", "Phase Start").value(0.5).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(10, "phase_speed", "Phase Speed").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(11, "phase_red", "Phase Red").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(12, "phase_green", "Phase Green").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(13, "phase_blue", "Phase Blue").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(14, "brightness", "Brightness").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::color(15, "color", "Color").value({1, 1, 1}),
        param::scalar(16, "offset", "Offset").value(0).range(-8, 2).slider(-1, 1),
        param::scalar(17, "saturation", "Saturation").value(1).range(-UNBOUNDED, 10).slider(0, 2),
        param::scalar(18, "scale_by_source", "Scale By Source").value(0).range(0, 1),
        param::scalar(19, "scale_by_src_amp", "Scale By Src Amp").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 10),
        param::scalar(20, "mix_with_source", "Mix With Source").value(0).range(0, 1),
        param::choice(21, "mask_use", "Mask Use", "Luma|Alpha"),
        param::scalar(22, "blur_mask", "Blur Mask").value(0.05).range(0, 1000).slider(0, 2),
        param::toggle(23, "invert_mask", "Invert Mask"),
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
        // Stripe Mag along Stripe Dir: the stripes ramp along this vector
        const double turns = v.scalar(p::stripe_dir) / 360.0;
        const core::warp::vec2 grad = core::warp::rotate({v.scalar(p::stripe_mag), 0.0}, turns);
        core::psyko::render(args, s, [&](const core::warp::vec2& xy) { return (xy[0] * grad[0]) + (xy[1] * grad[1]); });

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
            .match_name = "f_psykostripes",
            .name = "f_psykostripes",
            .category = "Functional Render",
            .description = "Fills the clip with cycling color stripes, bent by the source brightness.",
            .params = PARAMS,
            .mask_input = "Mask",
            .background_input = "",
            .time_varying = true,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::psykostripes
