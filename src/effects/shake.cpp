#include "effects/shake.hpp"
#include "core/mask.hpp"
#include "core/noise.hpp"
#include "core/parallel.hpp"
#include "core/sample.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace functional_fx_suite::effects::shake
{
    enum class p : std::uint8_t
    {
        style,
        amplitude,
        frequency,
        phase,
        drift,
        center_bias,
        stillness,
        twitch_frequency,
        z_dist,
        motion_blur,
        mo_blur_length,
        seed,
        wrap,
        x_group,
        x_rand_amp,
        x_rand_freq,
        x_wave_amp,
        x_wave_freq,
        x_phase,
        x_group_end,
        y_group,
        y_rand_amp,
        y_rand_freq,
        y_wave_amp,
        y_wave_freq,
        y_phase,
        y_group_end,
        z_group,
        z_rand_amp,
        z_rand_freq,
        z_wave_amp,
        z_wave_freq,
        z_phase,
        z_group_end,
        tilt_group,
        tilt_rand_amp,
        tilt_rand_freq,
        tilt_wave_amp,
        tilt_wave_freq,
        tilt_phase,
        tilt_group_end,
        channels_group,
        red_amplitude,
        green_amplitude,
        blue_amplitude,
        red_phase,
        green_phase,
        blue_phase,
        rgb_randomness,
        rgb_frequency,
        channels_group_end,
        mask_use,
        blur_mask,
        invert_mask,
        count
    };

    // popup order of the "Style" menu
    enum class style : std::uint8_t
    {
        normal,
        twitchy,
        jumpy
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;
    static constexpr int STYLE = static_cast<int>(p::style);
    static constexpr std::uint32_t TWITCHY_ONLY = 1U << static_cast<int>(style::twitchy);
    static constexpr std::uint32_t JUMPY_ONLY = 1U << static_cast<int>(style::jumpy);

    // amplitudes are frame-width units, tilt degrees
    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::choice(1, "style", "Style", "Normal|Twitchy|Jumpy"),
        param::scalar(2, "amplitude", "Amplitude").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(3, "frequency", "Frequency").value(8).range(0, UNBOUNDED).slider(0.1, 30),
        param::scalar(4, "phase", "Phase").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::scalar(5, "drift", "Drift").value(0.3).range(0, 1).enabled_when(STYLE, JUMPY_ONLY),
        param::scalar(6, "center_bias", "Center Bias")
            .value(0)
            .range(0, UNBOUNDED)
            .slider(0, 1)
            .enabled_when(STYLE, JUMPY_ONLY),
        param::scalar(7, "stillness", "Stillness").value(0.7).range(0, 1).enabled_when(STYLE, TWITCHY_ONLY),
        param::scalar(8, "twitch_frequency", "Twitch Frequency")
            .value(2)
            .range(0, UNBOUNDED)
            .slider(0.1, 30)
            .enabled_when(STYLE, TWITCHY_ONLY),
        param::scalar(9, "z_dist", "Z Dist").value(1).range(0.001, UNBOUNDED).slider(0.1, 2),
        param::toggle(10, "motion_blur", "Motion Blur"),
        param::scalar(11, "mo_blur_length", "Mo Blur Length").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(12, "seed", "Seed").value(0).range(0, UNBOUNDED).slider(0, 1),
        param::choice(13, "wrap", "Wrap", "No|Tile|Reflect").value(2),
        param::group(14, "x_shake", "X Shake"),
        param::scalar(15, "x_rand_amp", "X Rand Amp").value(0.2).range(0, UNBOUNDED).slider(0, 0.5),
        param::scalar(16, "x_rand_freq", "X Rand Freq").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(17, "x_wave_amp", "X Wave Amp").value(0).range(0, UNBOUNDED).slider(0, 0.5),
        param::scalar(18, "x_wave_freq", "X Wave Freq").value(0.5).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(19, "x_phase", "X Phase").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::group_end(20, "x_shake_end"),
        param::group(21, "y_shake", "Y Shake"),
        param::scalar(22, "y_rand_amp", "Y Rand Amp").value(0.1).range(0, UNBOUNDED).slider(0, 0.5),
        param::scalar(23, "y_rand_freq", "Y Rand Freq").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(24, "y_wave_amp", "Y Wave Amp").value(0).range(0, UNBOUNDED).slider(0, 0.5),
        param::scalar(25, "y_wave_freq", "Y Wave Freq").value(0.5).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(26, "y_phase", "Y Phase").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::group_end(27, "y_shake_end"),
        param::group(28, "z_shake", "Z Shake"),
        param::scalar(29, "z_rand_amp", "Z Rand Amp").value(0).range(0, UNBOUNDED).slider(0, 0.5),
        param::scalar(30, "z_rand_freq", "Z Rand Freq").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(31, "z_wave_amp", "Z Wave Amp").value(0).range(0, UNBOUNDED).slider(0, 0.5),
        param::scalar(32, "z_wave_freq", "Z Wave Freq").value(0.5).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(33, "z_phase", "Z Phase").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::group_end(34, "z_shake_end"),
        param::group(35, "tilt_shake", "Tilt Shake"),
        param::scalar(36, "tilt_rand_amp", "Tilt Rand Amp").value(0).range(0, UNBOUNDED).slider(0, 90),
        param::scalar(37, "tilt_rand_freq", "Tilt Rand Freq").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(38, "tilt_wave_amp", "Tilt Wave Amp").value(0).range(0, UNBOUNDED).slider(0, 90),
        param::scalar(39, "tilt_wave_freq", "Tilt Wave Freq").value(0.5).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(40, "tilt_phase", "Tilt Phase").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-1, 1),
        param::group_end(41, "tilt_shake_end"),
        param::group(42, "channels", "Channels"),
        param::scalar(43, "red_amplitude", "Red Amplitude").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(44, "green_amplitude", "Green Amplitude").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(45, "blue_amplitude", "Blue Amplitude").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(46, "red_phase", "Red Phase").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-0.1, 0.1),
        param::scalar(47, "green_phase", "Green Phase").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-0.1, 0.1),
        param::scalar(48, "blue_phase", "Blue Phase").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-0.1, 0.1),
        param::scalar(49, "rgb_randomness", "RGB Randomness").value(0).range(0, UNBOUNDED).slider(0, 1),
        param::scalar(50, "rgb_frequency", "RGB Frequency").value(2).range(0, UNBOUNDED).slider(0.1, 30),
        param::group_end(51, "channels_end"),
        param::choice(52, "mask_use", "Mask Use", "Luma|Alpha"),
        param::scalar(53, "blur_mask", "Blur Mask").value(0.05).range(0, UNBOUNDED).slider(0, 2),
        param::toggle(54, "invert_mask", "Invert Mask"),
    };

    struct axis
    {
        double rand_amp = 0;
        double rand_freq = 0;
        double wave_amp = 0;
        double wave_freq = 0;
        double phase = 0;
    };

    // everything that drives one shake path; per-channel shakes get their own copy
    struct motion
    {
        std::array<axis, 4> axes{}; // x, y, z, tilt
        double amplitude = 1;
        double frequency = 8;
        double phase = 0;
        double seed = 0;
        double z_dist = 1;
        bool jumpy = false;
        double drift = 0;
        bool twitchy = false;
        double stillness = 0;
        double twitch_frequency = 0;
        double extra_amp = 0; // per-channel randomness on top of the shared shake
        std::uint32_t extra_seed = 0;
        double extra_freq = 0;
    };

    // q = zoom * rotate(n + shift), in normalized frame units
    struct transform
    {
        double zoom = 1;
        double cos_t = 1;
        double sin_t = 0;
        double shift_x = 0;
        double shift_y = 0;

        [[nodiscard]] std::array<double, 2> apply(double nx, double ny) const
        {
            const double x = nx + shift_x;
            const double y = ny + shift_y;
            return {zoom * (cos_t * x - sin_t * y), zoom * (sin_t * x + cos_t * y)};
        }
    };

    // smooth bump of radius 1.5 knots that the shake noise blends its knots with
    static double knot_weight(double x)
    {
        const double d = std::abs(x) / 1.5;
        return d < 0.5 ? 1.0 - 2.0 * d * d : d < 1.0 ? 2.0 * (1.0 - d) * (1.0 - d) : 0.0;
    }

    // random values in [-1, 1) at integer times, blended by knot_weight and normalized
    static double knot_noise(std::uint32_t seed, double t)
    {
        const double i = std::floor(t);
        double sum = 0.0;
        double weights = 0.0;
        for (int k = -1; k <= 2; ++k)
        {
            const double w = knot_weight(t - (i + k));
            sum += w * core::noise::signed_unit(core::noise::combine(seed, core::noise::cell(i) + k));
            weights += w;
        }
        return weights > 0.0 ? sum / weights : 0.0;
    }

    // random levels held over segments of random length (mean one cycle, repeating every 1024),
    // sliding to the next level for the last `drift` fraction of each segment
    static double jumpy_noise(std::uint32_t seed, double u, double drift)
    {
        constexpr int SEGMENTS = 1024;
        // segment lengths are squared uniforms rescaled to a mean of 1
        std::array<double, SEGMENTS + 1> starts{};
        const std::uint32_t length_seed = core::noise::combine(seed, 0x5e6aU);
        for (int j = 0; j < SEGMENTS; ++j)
        {
            const double v = core::noise::unit(core::noise::combine(length_seed, static_cast<std::uint32_t>(j)));
            starts[j + 1] = starts[j] + v * v;
        }
        const double scale = SEGMENTS / starts[SEGMENTS];
        const double cycle = std::floor(u / SEGMENTS);
        const double pos = (u - cycle * SEGMENTS) / scale;
        const auto it = std::upper_bound(starts.begin() + 1, starts.end(), pos);
        const auto j = static_cast<int>(std::min<std::ptrdiff_t>(it - starts.begin(), SEGMENTS) - 1);
        const double length = starts[j + 1] - starts[j];
        const double f = length > 0.0 ? (pos - starts[j]) / length : 1.0;
        const std::uint32_t index = core::noise::cell(cycle * SEGMENTS) + static_cast<std::uint32_t>(j);
        const double a = core::noise::signed_unit(core::noise::combine(seed, index));
        const double b = core::noise::signed_unit(core::noise::combine(seed, index + 1));
        const double t = drift > 0.0 ? std::clamp((f - (1.0 - drift)) / drift, 0.0, 1.0) : 0.0;
        return a + (b - a) * t;
    }

    // 1 while shaking, 0 while still; still for about `stillness` of the time
    static double twitch(std::uint32_t seed, double time, double freq, double stillness)
    {
        return core::noise::unit(core::noise::combine(seed, core::noise::cell(std::floor(time * freq)))) > stillness
                   ? 1.0
                   : 0.0;
    }

    static double smooth_twitch(std::uint32_t seed, double time, double freq, double stillness, double dt)
    {
        return (twitch(seed, time - dt, freq, stillness) + twitch(seed, time, freq, stillness) +
                twitch(seed, time + dt, freq, stillness)) /
               3.0;
    }

    static double shift(const motion& m, const axis& a, std::uint32_t seed, double t, double twitch_scale)
    {
        const double u = t * a.rand_freq;
        const double rand = m.jumpy ? jumpy_noise(seed, u, m.drift) : knot_noise(seed, u);
        const double wave = std::sin(2.0 * std::numbers::pi * t * a.wave_freq);
        const double extra =
            m.extra_amp != 0.0 ? knot_noise(core::noise::combine(seed, m.extra_seed), u * m.extra_freq) : 0.0;
        return twitch_scale *
               (m.amplitude * (a.rand_amp * rand + a.wave_amp * wave) + m.extra_amp * a.rand_amp * extra);
    }

    static transform evaluate(const motion& m, double time, double dt)
    {
        const double twitch_scale =
            m.twitchy ? smooth_twitch(core::noise::seed_bits(m.seed), time, m.twitch_frequency, m.stillness, dt) : 1.0;
        std::array<double, 4> s{};
        for (std::size_t k = 0; k < s.size(); ++k)
        {
            const axis& a = m.axes[k];
            const double t = (time - (a.phase + m.phase)) * m.frequency;
            s[k] = shift(m, a, core::noise::seed_bits(m.seed + 0.1 * static_cast<double>(k + 1)), t, twitch_scale);
        }
        const double angle = s[3] * std::numbers::pi / 180.0;
        return transform{m.z_dist + s[2], std::cos(angle), std::sin(angle), s[0], s[1]};
    }

    static motion read_motion(const core::param_values& v)
    {
        motion m;
        const auto read_axis = [&](p rand_amp) {
            const auto first = static_cast<std::uint8_t>(rand_amp);
            const auto at = [&](int offset) { return v.scalar(static_cast<p>(first + offset)); };
            return axis{at(0), at(1), at(2), at(3), at(4)};
        };
        m.axes = {read_axis(p::x_rand_amp), read_axis(p::y_rand_amp), read_axis(p::z_rand_amp),
                  read_axis(p::tilt_rand_amp)};
        m.amplitude = v.scalar(p::amplitude);
        m.frequency = v.scalar(p::frequency);
        m.phase = v.scalar(p::phase);
        m.seed = v.scalar(p::seed);
        m.z_dist = v.scalar(p::z_dist);
        switch (static_cast<style>(v.choice(p::style)))
        {
        case style::normal:
            break;
        case style::twitchy:
            m.frequency *= 10.0;
            m.twitchy = true;
            m.stillness = v.scalar(p::stillness);
            m.twitch_frequency = v.scalar(p::twitch_frequency);
            break;
        case style::jumpy:
            // jumps snap back to centre with probability center_bias, at four times the shake rate
            m.jumpy = true;
            m.drift = v.scalar(p::drift);
            m.twitchy = true;
            m.stillness = v.scalar(p::center_bias);
            m.twitch_frequency = 4.0 * m.frequency;
            break;
        }
        return m;
    }

    // one warp pass: current and next-frame transforms plus the motion-blur sample count
    struct pass
    {
        transform now;
        transform next;
        int samples = 1;
        double length = 0;
    };

    static pass make_pass(const motion& m, const core::render_args& args, bool blur, double blur_length)
    {
        pass ps{evaluate(m, args.time, args.frame_duration), {}, 1, 0};
        if (!blur || blur_length <= 0.0)
        {
            return ps;
        }
        ps.next = evaluate(m, args.time + args.frame_duration, args.frame_duration);
        ps.length = blur_length;
        // enough samples that neighbouring taps are about a pixel apart at the frame corners
        const core::rect& b = args.src.bounds;
        double travel = 0.0;
        for (const auto& [px, py] :
             {std::array{b.x0, b.y0}, std::array{b.x1, b.y0}, std::array{b.x0, b.y1}, std::array{b.x1, b.y1}})
        {
            const double nx = args.geometry.to_nx(px);
            const double ny = args.geometry.to_ny(py);
            const auto q0 = ps.now.apply(nx, ny);
            const auto q1 = ps.next.apply(nx, ny);
            travel = std::max(travel, std::hypot(q1[0] - q0[0], q1[1] - q0[1]) * args.geometry.width * blur_length);
        }
        constexpr int MAX_SAMPLES = 64;
        ps.samples = std::clamp(static_cast<int>(std::ceil(travel)) + 1, 1, MAX_SAMPLES);
        return ps;
    }

    static core::sample::pixel warp(const core::render_args& args, const pass& ps, double nx, double ny,
                                    core::sample::wrap mode)
    {
        const auto q0 = ps.now.apply(nx, ny);
        const core::frame& g = args.geometry;
        if (ps.samples <= 1)
        {
            return core::sample::bilinear(args.src, g.to_px_x(q0[0]), g.to_px_y(q0[1]), mode);
        }
        const auto q1 = ps.next.apply(nx, ny);
        core::sample::pixel sum{};
        for (int i = 0; i < ps.samples; ++i)
        {
            const double f = ps.length * i / (ps.samples - 1);
            const auto px = core::sample::bilinear(args.src, g.to_px_x(q0[0] + (q1[0] - q0[0]) * f),
                                                   g.to_px_y(q0[1] + (q1[1] - q0[1]) * f), mode);
            for (int c = 0; c < 4; ++c)
            {
                sum[c] += px[c];
            }
        }
        const float inv = 1.0f / static_cast<float>(ps.samples);
        for (float& c : sum)
        {
            c *= inv;
        }
        return sum;
    }

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const motion base = read_motion(v);
        const bool blur = v.toggle(p::motion_blur);
        const double blur_length = v.scalar(p::mo_blur_length);
        const auto mode = static_cast<core::sample::wrap>(std::clamp(v.choice(p::wrap), 0, 2));

        const std::array<double, 3> channel_amp{v.scalar(p::red_amplitude), v.scalar(p::green_amplitude),
                                                v.scalar(p::blue_amplitude)};
        const std::array<double, 3> channel_phase{v.scalar(p::red_phase), v.scalar(p::green_phase),
                                                  v.scalar(p::blue_phase)};
        const bool per_channel = std::ranges::any_of(channel_amp, [](double a) { return a != 1.0; }) ||
                                 std::ranges::any_of(channel_phase, [](double ph) { return ph != 0.0; }) ||
                                 v.scalar(p::rgb_randomness) != 0.0;

        std::array<pass, 3> passes{};
        const int pass_count = per_channel ? 3 : 1;
        for (int c = 0; c < pass_count; ++c)
        {
            motion m = base;
            if (per_channel)
            {
                m.amplitude *= channel_amp[c];
                m.phase += channel_phase[c];
                m.extra_amp = v.scalar(p::rgb_randomness);
                m.extra_seed = static_cast<std::uint32_t>(c + 1);
                m.extra_freq = v.scalar(p::rgb_frequency);
            }
            passes[c] = make_pass(m, args, blur, blur_length);
        }

        const core::image& dst = args.dst;
        core::parallel::for_range(dst.bounds.y0, dst.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                const double ny = args.geometry.to_ny(y + 0.5);
                for (int x = dst.bounds.x0; x < dst.bounds.x1; ++x)
                {
                    const double nx = args.geometry.to_nx(x + 0.5);
                    float* d = dst.at(x, y);
                    if (!per_channel)
                    {
                        const auto px = warp(args, passes[0], nx, ny, mode);
                        std::copy(px.begin(), px.end(), d);
                        continue;
                    }
                    // each channel follows its own path; alpha is the largest of the three so no premultiplied
                    // channel exceeds it
                    float alpha = 0.0f;
                    for (int c = 0; c < 3; ++c)
                    {
                        const auto px = warp(args, passes[c], nx, ny, mode);
                        d[c] = px[c];
                        alpha = std::max(alpha, px[3]);
                    }
                    d[3] = alpha;
                }
            }
        });

        const auto mask_mode = v.choice(p::mask_use) == 1 ? core::mask::source::alpha : core::mask::source::luma;
        const core::buffer m = core::mask::build(args, mask_mode, v.scalar(p::blur_mask), v.toggle(p::invert_mask));
        if (m.view().data != nullptr)
        {
            core::mask::dissolve(args, m.view());
        }
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_shake",
            .name = "f_shake",
            .category = "Functional Distort",
            .description = "Shakes the clip over time with repeatable random and periodic translation, zoom and tilt.",
            .params = PARAMS,
            .mask_input = "Mask",
            .background_input = "",
            .time_varying = true,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::shake
