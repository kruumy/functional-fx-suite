#include "effects/flicker.hpp"
#include "core/noise.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace functional_fx_suite::effects::flicker
{
    enum class p : std::uint8_t
    {
        amplitude,
        direction,
        rand_luma_amp,
        rand_color_amp,
        rand_freq,
        wave,
        wave_amp,
        wave_freq,
        wave_r_phase,
        wave_g_phase,
        wave_b_phase,
        wave_end,
        red_amp,
        green_amp,
        blue_amp,
        brightness,
        seed,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    // phases are in cycles
    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "amplitude", "Amplitude").value(0.2).range(0, UNBOUNDED).slider(0, 1),
        param::choice(17, "direction", "Direction", "Both|Brighter|Darker"),
        param::scalar(2, "rand_luma_amp", "Rand Luma Amp").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(3, "rand_color_amp", "Rand Color Amp").value(0).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(4, "rand_freq", "Rand Freq").value(30).range(-UNBOUNDED, UNBOUNDED).slider(0, 60),
        param::group(5, "wave", "Wave"),
        param::scalar(6, "wave_amp", "Wave Amp").value(0).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(7, "wave_freq", "Wave Freq").value(5).range(-UNBOUNDED, UNBOUNDED).slider(0, 60),
        param::scalar(8, "wave_r_phase", "Wave R Phase").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(9, "wave_g_phase", "Wave G Phase").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(10, "wave_b_phase", "Wave B Phase").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::group_end(11, "wave_end"),
        param::scalar(12, "red_amp", "Red Amp").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(13, "green_amp", "Green Amp").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(14, "blue_amp", "Blue Amp").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(15, "brightness", "Brightness").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(16, "seed", "Seed").value(0.123).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
    };

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const double t = args.time;
        const double rand_t = t * v.scalar(p::rand_freq);
        const double seed = v.scalar(p::seed);
        // smooth random levels over time: one shared by all channels, one per channel
        const double luma = core::noise::value_2d(core::noise::seed_bits(seed), rand_t, 0.0) - 0.5;
        const std::array amp{v.scalar(p::red_amp), v.scalar(p::green_amp), v.scalar(p::blue_amp)};
        const std::array phase{v.scalar(p::wave_r_phase), v.scalar(p::wave_g_phase), v.scalar(p::wave_b_phase)};
        const int direction = v.choice(p::direction);
        std::array<float, 3> gain{};
        for (int c = 0; c < 3; ++c)
        {
            const double chroma = core::noise::value_2d(core::noise::seed_bits(seed + 0.3333 + c), rand_t, 0.0) - 0.5;
            const double rand = 2.0 * (v.scalar(p::rand_luma_amp) * luma + v.scalar(p::rand_color_amp) * chroma);
            const double wave = std::sin(2.0 * std::numbers::pi * (t * v.scalar(p::wave_freq) + 0.25 - phase[c])) *
                                v.scalar(p::wave_amp);
            double swing = v.scalar(p::amplitude) * amp[c] * (rand + wave);
            // one-sided flicker folds the dips over instead of clipping them, so it never sits still
            if (direction != 0)
            {
                swing = direction == 1 ? std::abs(swing) : -std::abs(swing);
            }
            gain[c] = static_cast<float>(v.scalar(p::brightness) + swing);
        }

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
                    const float* s = args.src.at(x, y);
                    for (int c = 0; c < 3; ++c)
                    {
                        d[c] = s[c] * gain[c];
                    }
                    d[3] = s[3];
                }
            }
        });
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_flicker",
            .name = "f_flicker",
            .category = "Functional Lighting",
            .description = "Varies the brightness and color of the clip over time, randomly or in waves.",
            .params = PARAMS,
            .mask_input = "",
            .background_input = "",
            .time_varying = true,
            .reads_outside = false,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::flicker
