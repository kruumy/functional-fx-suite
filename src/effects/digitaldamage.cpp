#include "effects/digitaldamage.hpp"
#include "core/buffer.hpp"
#include "core/color.hpp"
#include "core/mask.hpp"
#include "core/noise.hpp"
#include "core/parallel.hpp"
#include "core/sample.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace functional_fx_suite::effects::digitaldamage
{
    enum class p : std::uint8_t
    {
        intensity,
        time_intensity,
        damage_size,
        damage_size_rel_x,
        shift,
        shift_details,
        shift_amount,
        shift_threshold,
        shift_blink_freq,
        shift_always,
        shift_details_end,
        brights_noise,
        brights_noise_details,
        brights_threshold,
        brights_band_threshold,
        brights_band_freq,
        brights_blink_freq,
        brights_always,
        brights_noise_details_end,
        pixelate,
        pixelate_details,
        pixelate_frequency,
        pixelate_hold,
        pixelate_threshold,
        pixelate_overdrive,
        pixelate_blink_freq,
        pixelate_always,
        pixelate_details_end,
        block_noise,
        block_noise_details,
        blocks_intensity,
        blocks_threshold,
        blocks_softness,
        blocks_chroma,
        blocks_blink_freq,
        blocks_always,
        blocks_affect_alpha,
        block_noise_details_end,
        invert,
        invert_details,
        invert_threshold,
        invert_darken,
        invert_pattern_freq,
        invert_blink_freq,
        invert_always,
        invert_details_end,
        seed,
        mask_use,
        blur_mask,
        invert_mask,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "intensity", "Intensity").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 5),
        param::scalar(2, "time_intensity", "Time Intensity").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 5),
        param::scalar(3, "damage_size", "Damage Size").value(1).range(0.001, UNBOUNDED).slider(0.001, 2),
        param::scalar(4, "damage_size_rel_x", "Damage Size Rel X").value(1).range(0.001, UNBOUNDED).slider(0.001, 2),
        param::toggle(5, "shift", "Shift").value(1),
        param::group(6, "shift_details", "Shift Details"),
        param::scalar(7, "shift_amount", "Shift Amount").value(0.1).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(8, "shift_threshold", "Shift Threshold").value(0.3).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(9, "shift_blink_freq", "Shift Blink Freq").value(10).range(0.1, UNBOUNDED).slider(0.1, 20),
        param::scalar(10, "shift_always", "Shift Always").value(0.3).range(-UNBOUNDED, 1).slider(0, 1),
        param::group_end(11, "shift_details_end"),
        param::toggle(12, "brights_noise", "Brights Noise").value(1),
        param::group(13, "brights_noise_details", "Brights Noise Details"),
        param::scalar(14, "brights_threshold", "Brights Threshold").value(0.6).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(15, "brights_band_threshold", "Brights Band Threshold")
            .value(0.4)
            .range(-UNBOUNDED, 1)
            .slider(0, 1),
        param::scalar(16, "brights_band_freq", "Brights Band Freq").value(6).range(0.1, UNBOUNDED).slider(0.1, 20),
        param::scalar(17, "brights_blink_freq", "Brights Blink Freq").value(10).range(0.1, UNBOUNDED).slider(0.1, 20),
        param::scalar(18, "brights_always", "Brights Always").value(0.23).range(-UNBOUNDED, 1).slider(0, 1),
        param::group_end(19, "brights_noise_details_end"),
        param::toggle(20, "pixelate", "Pixelate").value(1),
        param::group(21, "pixelate_details", "Pixelate Details"),
        param::scalar(22, "pixelate_frequency", "Pixelate Frequency").value(40).range(1, UNBOUNDED).slider(4, 200),
        param::scalar(23, "pixelate_hold", "Pixelate Hold").value(0.95).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(24, "pixelate_threshold", "Pixelate Threshold").value(0.1).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(25, "pixelate_overdrive", "Pixelate Overdrive")
            .value(1.6)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 10),
        param::scalar(26, "pixelate_blink_freq", "Pixelate Blink Freq").value(10).range(0.1, UNBOUNDED).slider(0.1, 20),
        param::scalar(27, "pixelate_always", "Pixelate Always").value(0.15).range(-UNBOUNDED, 1).slider(0, 1),
        param::group_end(28, "pixelate_details_end"),
        param::toggle(29, "block_noise", "Block Noise").value(1),
        param::group(30, "block_noise_details", "Block Noise Details"),
        param::scalar(31, "blocks_intensity", "Blocks Intensity").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(32, "blocks_threshold", "Blocks Threshold").value(0.4).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(33, "blocks_softness", "Blocks Softness").value(0.2).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(34, "blocks_chroma", "Blocks Chroma").value(0.95).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(35, "blocks_blink_freq", "Blocks Blink Freq").value(10).range(0.1, UNBOUNDED).slider(0.1, 20),
        param::scalar(36, "blocks_always", "Blocks Always").value(0.15).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(37, "blocks_affect_alpha", "Blocks Affect Alpha")
            .value(0)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 1),
        param::group_end(38, "block_noise_details_end"),
        param::toggle(39, "invert", "Invert").value(1),
        param::group(40, "invert_details", "Invert Details"),
        param::scalar(41, "invert_threshold", "Invert Threshold").value(0.35).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(42, "invert_darken", "Invert Darken").value(0.4).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(43, "invert_pattern_freq", "Invert Pattern Freq").value(2).range(0.1, UNBOUNDED).slider(0.1, 5),
        param::scalar(44, "invert_blink_freq", "Invert Blink Freq").value(10).range(0.1, UNBOUNDED).slider(0.1, 20),
        param::scalar(45, "invert_always", "Invert Always").value(0.21).range(-UNBOUNDED, 1).slider(0, 1),
        param::group_end(46, "invert_details_end"),
        param::scalar(47, "seed", "Seed").value(1.23).range(-UNBOUNDED, UNBOUNDED).slider(0, 2.46),
        param::choice(48, "mask_use", "Mask Use", "Luma|Alpha"),
        param::scalar(49, "blur_mask", "Blur Mask").value(0.05).range(0, 1000).slider(0, 2),
        param::toggle(50, "invert_mask", "Invert Mask"),
    };

    // block noise arguments after the coordinates
    struct block_spec
    {
        double fx = 1; // cells per xy unit
        double fy = 1;
        double nfx = 1; // noise frequency over the cell centres and over time
        double nfy = 1;
        double nft = 1;
        double wiggle = 0; // grid offset in cells, changing wiggle_speed times a second
        double wiggle_speed = 0;
        int octaves = 1;
        double influence = 1; // amplitude of each next octave
        double size = 1;      // cell frequency scale of each next octave
        double noise_scale = 1;
        double shift = 0; // grid offset in cells
    };

    // a smooth noise value at its centre, summed over octaves
    // a smooth noise value at its centre, summed and spread to -1..1
    static double block_noise(const block_spec& b, double x, double y, double t, std::uint32_t seed)
    {
        double sum = 0.0;
        double norm = 0.0;
        double amp = 1.0;
        double fx = b.fx;
        double fy = b.fy;
        double nfx = b.nfx;
        double nfy = b.nfy;
        for (int o = 0; o < b.octaves; ++o)
        {
            const std::uint32_t so = core::noise::combine(seed, static_cast<std::uint32_t>(o));
            const double wx = b.shift + b.wiggle * (core::noise::value_2d(so + 1, t * b.wiggle_speed, 0.0) - 0.5);
            const double wy = b.shift + b.wiggle * (core::noise::value_2d(so + 2, t * b.wiggle_speed, 0.0) - 0.5);
            const double cx = (std::floor(x * fx + wx) + 0.5 - wx) / fx;
            const double cy = (std::floor(y * fy + wy) + 0.5 - wy) / fy;
            sum += amp * (core::noise::value_3d(so, cx * nfx, cy * nfy, t * b.nft) * 2.0 - 1.0);
            norm += amp * amp;
            amp *= b.influence;
            fx *= b.size;
            fy *= b.size;
            nfx *= b.size * b.noise_scale;
            nfy *= b.size * b.noise_scale;
        }
        // rms-normalized and widened to a spread of about 0.5, so the thresholds (0.1 .. 0.5) pass often enough
        constexpr double SPREAD = 1.25;
        return norm > 0.0 ? std::clamp(sum / std::sqrt(norm) * SPREAD, -1.0, 1.0) : 0.0;
    }

    // (v - lo) / (hi - lo) clamped to 0..1
    static double rescale01(double v, double lo, double hi)
    {
        return hi != lo ? std::clamp((v - lo) / (hi - lo), 0.0, 1.0) : v >= lo ? 1.0 : 0.0;
    }

    // a random level per 1 / freq seconds, on above interp(1, 1 - always, master)
    static bool blink(double freq, double always, double master, double seed, double t)
    {
        const double level = core::noise::unit(
            core::noise::combine(core::noise::seed_bits(seed), core::noise::cell(std::floor(t * freq))));
        return level > 1.0 - always * master;
    }

    // a random sawtooth with a random start and slope per second where the slope is 0 for
    // `dwell` of the seconds
    static double twitchy_seed(double t, double dwell, double seed)
    {
        const std::uint32_t s = core::noise::seed_bits(seed);
        const std::uint32_t second = core::noise::cell(std::floor(t));
        const double start = core::noise::unit(core::noise::combine(s, second));
        const double slope = core::noise::unit(core::noise::combine(s + 1, second)) < dwell
                                 ? 0.0
                                 : core::noise::unit(core::noise::combine(s + 2, second));
        return start + slope * (t - std::floor(t));
    }

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const core::frame& f = args.geometry;
        const core::image& src = args.src;
        const double t = args.time;
        const double seed = v.scalar(p::seed);
        const double intensity = v.scalar(p::intensity);
        const double master = v.scalar(p::time_intensity);
        const double high_intensity = std::max(intensity - 1.0, 0.0) * 0.5;
        const double size = std::max(0.001, v.scalar(p::damage_size));
        const double dfx = 1.0 / size / std::max(0.001, v.scalar(p::damage_size_rel_x));
        const double dfy = 1.0 / size;
        const double time_seed = seed + t;
        const auto frame_number = static_cast<double>(std::lround(t / args.frame_duration));

        const bool shift_on = v.toggle(p::shift) &&
                              blink(v.scalar(p::shift_blink_freq), v.scalar(p::shift_always), master, seed + 2.0, t);
        const bool brights_on =
            v.toggle(p::brights_noise) &&
            blink(v.scalar(p::brights_blink_freq), v.scalar(p::brights_always), master, seed + 1.1112, t - 1.5);
        const bool pixelate_on = v.toggle(p::pixelate) && blink(v.scalar(p::pixelate_blink_freq),
                                                                v.scalar(p::pixelate_always), master, seed + 4.11, t);
        const bool blocks1 = v.toggle(p::block_noise) && blink(v.scalar(p::blocks_blink_freq),
                                                               v.scalar(p::blocks_always), master, seed + 3.1415, t);
        const bool blocks2 = v.toggle(p::block_noise) &&
                             blink(v.scalar(p::blocks_blink_freq), v.scalar(p::blocks_always), master, seed + 3.15, t);
        const bool invert_on = v.toggle(p::invert) &&
                               blink(v.scalar(p::invert_blink_freq), v.scalar(p::invert_always), master, seed + 0.7, t);

        const block_spec color_spec{dfx * 11, dfy * 19, dfx * 10, dfy * 17, 1, 1, 1, 5, 0.88, 0.66, 1.3, 0};
        const block_spec bigger_spec{dfx * 5.5, dfy * 10.4, dfx * 1.6, dfy * 3, 1, 1, 1, 5, 0.56, 0.66, 1.3, 0};
        const block_spec med_spec{dfx * 8, dfy * 12, dfx * 2, dfy * 5, 1, 1, 1, 5, 0.56, 0.66, 1.3, 0};
        const block_spec shift_spec{dfx * 0.5, dfy * 6.5, dfx * 1.8, dfy * 6.8, 2.5, 0.7, 5, 4, 0.74, 0.7, 1.5, 0};
        const block_spec bands_spec{1.5, 4, 0.2, 15, 0, 1, 0, 5, 0.78, 0.6, 1.15, 0.5};
        const double ipf = v.scalar(p::invert_pattern_freq);
        const block_spec invert_spec{dfx * ipf * 0.2, dfy * ipf, dfx * 0.2, dfy * 15, 0, 1, 0, 4, 0.78, 0.6, 1.15, 0.5};

        // color noise blocks per channel and their difference from the bigger noise blocks
        struct noise_blocks
        {
            std::array<double, 3> rgb{};
            std::array<double, 3> color{};
        };
        const auto noise_blocks_at = [&](double x, double y) {
            noise_blocks n;
            constexpr std::array<double, 3> THRESHOLDS{0.31, 0.36, 0.52};
            const double bigger =
                rescale01(block_noise(bigger_spec, x, y, t, core::noise::seed_bits(time_seed + 4.0)), 0.2, 0.25);
            for (int c = 0; c < 3; ++c)
            {
                n.rgb[c] = rescale01(block_noise(color_spec, x, y, t, core::noise::seed_bits(time_seed + 1.0 + c)),
                                     THRESHOLDS[c], 1.0);
                n.color[c] = std::abs(n.rgb[c] - bigger);
            }
            return n;
        };

        // shift and brights noise read the source at each pixel, so they fill one full-frame stage
        const double shift_threshold = v.scalar(p::shift_threshold) - high_intensity;
        const double shift_amount = v.scalar(p::shift_amount) * intensity;
        const double brights_threshold = v.scalar(p::brights_threshold);
        const double band_freq = v.scalar(p::brights_band_freq);
        const double band_threshold = 1.0 + (v.scalar(p::brights_band_threshold) - 1.0) * intensity;
        const std::uint32_t band_seed = core::noise::seed_bits(time_seed);
        core::buffer stage(src.bounds, 4);
        const core::image& st = stage.view();
        core::parallel::for_range(src.bounds.y0, src.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                const double yy = f.to_ny(y + 0.5) * core::XY_PER_FRAME_UNIT;
                // bands of rows where a noise along y passes the threshold
                const bool band = brights_on && core::noise::fbm_2d(band_seed, 0.0, yy * band_freq, 4) > band_threshold;
                for (int x = src.bounds.x0; x < src.bounds.x1; ++x)
                {
                    const double xx = f.to_nx(x + 0.5) * core::XY_PER_FRAME_UNIT;
                    float* d = st.at(x, y);
                    const float* s = src.at(x, y);
                    std::copy_n(s, 4, d);
                    if (shift_on)
                    {
                        double h = block_noise(shift_spec, xx, yy, t, core::noise::seed_bits(seed + 1.34));
                        h = h < shift_threshold && h > -shift_threshold ? 0.0 : h;
                        if (const double dx = shift_amount * h; dx != 0.0)
                        {
                            const core::sample::pixel q =
                                core::sample::bilinear(src, x + 0.5 + f.to_px(dx / core::XY_PER_FRAME_UNIT), y + 0.5,
                                                       core::sample::wrap::none);
                            std::ranges::copy(q, d);
                        }
                    }
                    if (band && core::color::luma(s[0], s[1], s[2]) > brights_threshold)
                    {
                        const noise_blocks n = noise_blocks_at(xx, yy);
                        std::ranges::transform(n.color, d, [](double c) { return static_cast<float>(c); });
                    }
                }
            }
        });

        const double pix_freq = std::max(1.0, v.scalar(p::pixelate_frequency));
        const double cell_x = 2.0 / pix_freq / dfx;
        const double cell_y = 2.0 / pix_freq / dfy;
        const std::uint32_t bands_seed =
            core::noise::seed_bits(twitchy_seed(t, v.scalar(p::pixelate_hold), seed + 9.876));
        const double pix_threshold = 1.0 + (v.scalar(p::pixelate_threshold) - 1.0) * intensity;
        const double overdrive = v.scalar(p::pixelate_overdrive);
        const double blocks_threshold = v.scalar(p::blocks_threshold) - 0.5;
        const double softness = v.scalar(p::blocks_softness);
        const double chroma = v.scalar(p::blocks_chroma);
        const auto blocks_intensity = v.scalar_f(p::blocks_intensity);
        const auto affect_alpha = v.scalar_f(p::blocks_affect_alpha);
        const double clamped_intensity = std::clamp(intensity, 0.0, 1.0);
        const double big_threshold = 1.0 - 0.8 * clamped_intensity + blocks_threshold;
        const double med_threshold = 1.0 - 0.7 * clamped_intensity + blocks_threshold;
        const std::uint32_t invert_seed = core::noise::seed_bits(seed + 0.7 + std::floor(frame_number / 3.0) * 3.0);
        const double invert_threshold = 1.0 + (v.scalar(p::invert_threshold) - 1.0) * intensity;
        const auto darken = v.scalar_f(p::invert_darken);

        const core::image& dst = args.dst;
        core::parallel::for_range(dst.bounds.y0, dst.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                const double yy = f.to_ny(y + 0.5) * core::XY_PER_FRAME_UNIT;
                for (int x = dst.bounds.x0; x < dst.bounds.x1; ++x)
                {
                    float* d = dst.at(x, y);
                    if (!src.bounds.contains(x, y))
                    {
                        std::fill_n(d, 4, 0.0f);
                        continue;
                    }
                    const double xx = f.to_nx(x + 0.5) * core::XY_PER_FRAME_UNIT;
                    std::array<float, 4> c{};
                    std::copy_n(st.at(x, y), 4, c.begin());

                    if (pixelate_on)
                    {
                        const double raw = std::clamp(block_noise(bands_spec, xx, yy, t, bands_seed), 0.0, 1.0);
                        if (const auto k =
                                static_cast<float>(rescale01(raw, pix_threshold, pix_threshold + 0.2) * overdrive);
                            k != 0.0f)
                        {
                            // each pixel takes its cell centre (rounding is a floor), Overdrive extrapolates
                            const double cx = (std::floor(xx / cell_x) + 0.5) * cell_x;
                            const double cy = (std::floor(yy / cell_y) + 0.5) * cell_y;
                            const core::sample::pixel q = core::sample::bilinear(
                                st, f.to_px_x(cx / core::XY_PER_FRAME_UNIT), f.to_px_y(cy / core::XY_PER_FRAME_UNIT),
                                core::sample::wrap::none);
                            for (int i = 0; i < 4; ++i)
                            {
                                c[i] = core::color::lerp(c[i], q[i], k);
                            }
                        }
                    }

                    if (blocks1 || blocks2)
                    {
                        const noise_blocks n = noise_blocks_at(xx, yy);
                        // a block's color counts only where its monochrome passes 0.1
                        const auto hard = [](std::array<double, 3> b) {
                            return core::color::luma(static_cast<float>(b[0]), static_cast<float>(b[1]),
                                                     static_cast<float>(b[2])) > 0.1f
                                       ? b
                                       : std::array<double, 3>{};
                        };
                        std::array<double, 3> big{};
                        std::array<double, 3> med{};
                        if (blocks1)
                        {
                            const double level =
                                rescale01(block_noise(bigger_spec, xx, yy, t, core::noise::seed_bits(seed + 7.89)),
                                          big_threshold, big_threshold + softness);
                            for (int i = 0; i < 3; ++i)
                            {
                                big[i] = level * (1.0 + (n.rgb[i] - 1.0) * chroma);
                            }
                            big = hard(big);
                        }
                        if (blocks2)
                        {
                            const double level =
                                rescale01(block_noise(med_spec, xx, yy, t, core::noise::seed_bits(seed + 7.99)),
                                          med_threshold, med_threshold + softness);
                            for (int i = 0; i < 3; ++i)
                            {
                                med[i] = level * (1.0 + (n.color[i] - 1.0) * chroma);
                            }
                            med = hard(med);
                        }
                        float strongest = 0.0f;
                        for (int i = 0; i < 3; ++i)
                        {
                            const auto block = static_cast<float>(std::abs(med[i] - (big[i] + c[i])));
                            strongest = std::max(strongest, block);
                            c[i] = core::color::lerp(c[i], block, blocks_intensity);
                        }
                        c[3] = core::color::screen(c[3], affect_alpha * strongest);
                    }

                    if (invert_on &&
                        std::clamp(block_noise(invert_spec, xx, yy, t, invert_seed), 0.0, 1.0) > invert_threshold)
                    {
                        for (int i = 0; i < 3; ++i)
                        {
                            c[i] = (1.0f - c[i]) * darken;
                        }
                    }
                    std::ranges::copy(c, d);
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
            .match_name = "f_digitaldamage",
            .name = "f_digitaldamage",
            .category = "Functional Stylize",
            .description = "Simulates a bad digital transmission: shifted bands, noisy brights, pixelated strips, "
                           "colored blocks and inverted bars that blink on and off.",
            .params = PARAMS,
            .mask_input = "Mask",
            .background_input = "",
            .time_varying = true,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::digitaldamage
