#include "effects/jpegdamage.hpp"
#include "core/color.hpp"
#include "core/mask.hpp"
#include "core/noise.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace functional_fx_suite::effects::jpegdamage
{
    enum class p : std::uint8_t
    {
        quality,
        res_factor,
        res_rel_x,
        frequency,
        all_freq_scale,
        x_freq_scale,
        y_freq_scale,
        low_freq_scale,
        mid_freq_scale,
        high_freq_scale,
        affect_luma,
        affect_chroma,
        frequency_end,
        errors,
        error_rate,
        err_block_density,
        error_amp,
        error_coherence,
        jitter_frames,
        rand_seed,
        errors_end,
        scale_lights,
        offset_darks,
        saturation,
        mask_use,
        blur_mask,
        invert_mask,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "quality", "Quality").value(0.1).range(0.01, 1),
        param::scalar(2, "res_factor", "Res Factor").value(1).range(1, UNBOUNDED).slider(1, 32).digits(0),
        param::scalar(3, "res_rel_x", "Res Rel X").value(1).range(0.01, UNBOUNDED).slider(0.01, 2),
        param::group(4, "frequency", "Frequency"),
        param::scalar(5, "all_freq_scale", "All Freq Scale").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 10),
        param::scalar(6, "x_freq_scale", "X Freq Scale").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 10),
        param::scalar(7, "y_freq_scale", "Y Freq Scale").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 10),
        param::scalar(8, "low_freq_scale", "Low Freq Scale").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 10),
        param::scalar(9, "mid_freq_scale", "Mid Freq Scale").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 10),
        param::scalar(10, "high_freq_scale", "High Freq Scale").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 10),
        param::scalar(11, "affect_luma", "Affect Luma").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 10),
        param::scalar(12, "affect_chroma", "Affect Chroma").value(0.5).range(-UNBOUNDED, UNBOUNDED).slider(0, 10),
        param::group_end(13, "frequency_end"),
        param::group(14, "errors", "Errors"),
        param::scalar(15, "error_rate", "Error Rate").value(0).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(16, "err_block_density", "Err Block Density").value(0.75).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(17, "error_amp", "Error Amp").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(18, "error_coherence", "Error Coherence").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(19, "jitter_frames", "Jitter Frames")
            .value(1)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 30)
            .digits(0),
        param::scalar(20, "rand_seed", "Rand Seed").value(0.123).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::group_end(21, "errors_end"),
        param::scalar(22, "scale_lights", "Scale Lights").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(23, "offset_darks", "Offset Darks").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-5, 1),
        param::scalar(24, "saturation", "Saturation").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::choice(25, "mask_use", "Mask Use", "Luma|Alpha"),
        param::scalar(26, "blur_mask", "Blur Mask").value(0.05).range(0, 1000).slider(0, 2),
        param::toggle(27, "invert_mask", "Invert Mask"),
    };

    static constexpr int N = 8;
    using block = std::array<float, N * N>;

    // quantization tables from the JPEG standard (ITU-T T.81 annex K), row-major by vertical frequency
    static constexpr block LUMA_TABLE{16, 11, 10, 16, 24,  40,  51,  61,  12, 12, 14, 19, 26,  58,  60,  55,
                                      14, 13, 16, 24, 40,  57,  69,  56,  14, 17, 22, 29, 51,  87,  80,  62,
                                      18, 22, 37, 56, 68,  109, 103, 77,  24, 35, 55, 64, 81,  104, 113, 92,
                                      49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99};
    static constexpr block CHROMA_TABLE{17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99,
                                        24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
                                        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
                                        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};

    // orthonormal DCT-II basis: BASIS[k * N + n] = c(k) cos((2n + 1) k pi / 2N)
    static const block& basis()
    {
        static const block b = [] {
            block out{};
            for (int k = 0; k < N; ++k)
            {
                const double ck = k == 0 ? std::sqrt(1.0 / N) : std::sqrt(2.0 / N);
                for (int n = 0; n < N; ++n)
                {
                    out[k * N + n] = static_cast<float>(ck * std::cos((2 * n + 1) * k * std::numbers::pi / (2 * N)));
                }
            }
            return out;
        }();
        return b;
    }

    // out = B * in * B^T (forward) or B^T * in * B (inverse)
    static block transform(const block& in, bool inverse)
    {
        const block& b = basis();
        block tmp{};
        block out{};
        for (int i = 0; i < N; ++i)
        {
            for (int j = 0; j < N; ++j)
            {
                float sum = 0.0f;
                for (int k = 0; k < N; ++k)
                {
                    sum += (inverse ? b[k * N + i] : b[i * N + k]) * in[k * N + j];
                }
                tmp[i * N + j] = sum;
            }
        }
        for (int i = 0; i < N; ++i)
        {
            for (int j = 0; j < N; ++j)
            {
                float sum = 0.0f;
                for (int k = 0; k < N; ++k)
                {
                    sum += tmp[i * N + k] * (inverse ? b[k * N + j] : b[j * N + k]);
                }
                out[i * N + j] = sum;
            }
        }
        return out;
    }

    // what a corrupted stretch of the bitstream does to one block
    struct damage
    {
        std::array<float, 3> dc{}; // offset to each channel's DC coefficient
        float ac = 0.0f;           // strength of the wrong AC coefficients
        std::uint32_t seed = 0;
    };

    struct settings
    {
        std::array<block, 3> steps{}; // per channel (Y, Cb, Cr); 0 leaves a coefficient alone
        double error_rate = 0;
        double density = 0;
        double amp = 0;
        double coherence = 0;
        std::uint32_t seed = 0;
    };

    static settings read_settings(const core::param_values& v, const core::render_args& args)
    {
        settings s;
        // IJG quality scaling: 50 keeps the tables, lower coarsens, higher refines
        const double q = std::clamp(v.scalar(p::quality) * 100.0, 1.0, 100.0);
        const double table_scale = (q < 50.0 ? 5000.0 / q : 200.0 - 2.0 * q) / 100.0;
        const double all = v.scalar(p::all_freq_scale);
        const std::array bands{v.scalar(p::low_freq_scale), v.scalar(p::mid_freq_scale), v.scalar(p::high_freq_scale)};
        const double fx = v.scalar(p::x_freq_scale);
        const double fy = v.scalar(p::y_freq_scale);
        const std::array affect{v.scalar(p::affect_luma), v.scalar(p::affect_chroma), v.scalar(p::affect_chroma)};
        for (int c = 0; c < 3; ++c)
        {
            const block& table = c == 0 ? LUMA_TABLE : CHROMA_TABLE;
            for (int vy = 0; vy < N; ++vy)
            {
                for (int ux = 0; ux < N; ++ux)
                {
                    // low, mid and high spread linearly over the block's diagonal frequency
                    const double pos = (ux + vy) / (2.0 * (N - 1)) * 2.0;
                    const double band = pos < 1.0 ? bands[0] + (bands[1] - bands[0]) * pos
                                                  : bands[1] + (bands[2] - bands[1]) * (pos - 1.0);
                    const double dir = ux + vy == 0 ? 1.0 : (ux * fx + vy * fy) / (ux + vy);
                    const double base = std::max(1.0, table[vy * N + ux] * table_scale);
                    s.steps[c][vy * N + ux] = static_cast<float>(std::max(0.0, base * all * band * dir * affect[c]));
                }
            }
        }
        s.error_rate = v.scalar(p::error_rate);
        s.density = v.scalar(p::err_block_density);
        s.amp = v.scalar(p::error_amp);
        s.coherence = v.scalar(p::error_coherence);
        // the errors hold for Jitter Frames frames, then jump
        double seed = v.scalar(p::rand_seed);
        if (const auto jitter = static_cast<double>(std::lround(v.scalar(p::jitter_frames))); jitter > 0.0)
        {
            const auto frame_number = static_cast<double>(std::lround(args.time / args.frame_duration));
            seed += std::floor(frame_number / jitter) * jitter;
        }
        s.seed = core::noise::seed_bits(seed);
        return s;
    }

    // errors start at random blocks and run on in scan order, like a corrupted entropy-coded stream; a run keeps
    // one DC offset (coherence sets how long it lasts) and damages Err Block Density of its blocks
    static std::vector<damage> plan_damage(const settings& s, int blocks)
    {
        std::vector<damage> out(static_cast<std::size_t>(blocks));
        constexpr double START_PER_RATE = 0.02;
        constexpr double MEAN_RUN_PER_COHERENCE = 20.0;
        constexpr float DC_RANGE = 128.0f;
        const double start = s.error_rate * START_PER_RATE;
        if (start <= 0.0)
        {
            return out;
        }
        const double end_chance = 1.0 / (1.0 + std::max(0.0, s.coherence) * MEAN_RUN_PER_COHERENCE);
        bool running = false;
        std::array<float, 3> dc{};
        for (int i = 0; i < blocks; ++i)
        {
            const std::uint32_t h = core::noise::combine(s.seed, static_cast<std::uint32_t>(i));
            if (running && core::noise::unit(core::noise::combine(h, 1)) < end_chance)
            {
                running = false;
            }
            if (!running && core::noise::unit(core::noise::combine(h, 2)) < start)
            {
                running = true;
                for (int c = 0; c < 3; ++c)
                {
                    dc[c] =
                        static_cast<float>(core::noise::signed_unit(core::noise::combine(h, 3 + c)) * s.amp) * DC_RANGE;
                }
            }
            if (running && core::noise::unit(core::noise::combine(h, 6)) < s.density)
            {
                out[static_cast<std::size_t>(i)] = damage{dc, static_cast<float>(s.amp), h};
            }
        }
        return out;
    }

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const settings s = read_settings(v, args);
        const core::image& src = args.src;

        // the image is coded on a grid of Res Factor sized cells, aligned to the source's top-left
        const double cell_y = std::max(1.0, std::round(v.scalar(p::res_factor)));
        const double cell_x = std::max(0.01, cell_y * v.scalar(p::res_rel_x));
        const int gw = static_cast<int>(std::ceil(src.bounds.width() / cell_x));
        const int gh = static_cast<int>(std::ceil(src.bounds.height() / cell_y));
        const int bw = (gw + N - 1) / N;
        const int bh = (gh + N - 1) / N;
        if (bw <= 0 || bh <= 0)
        {
            return;
        }

        // straight rgb averaged per cell, as JFIF YCbCr on a 0..255 scale (edge cells repeat to fill the last block)
        core::buffer coded(core::rect{0, 0, bw * N, bh * N}, 3);
        const core::image& g = coded.view();
        core::parallel::for_range(0, bh * N, [&](int y_begin, int y_end) {
            for (int gy = y_begin; gy < y_end; ++gy)
            {
                const int cy = std::min(gy, gh - 1);
                const int y0 = src.bounds.y0 + static_cast<int>(cy * cell_y);
                const int y1 =
                    std::max(y0 + 1, std::min(src.bounds.y1, src.bounds.y0 + static_cast<int>((cy + 1) * cell_y)));
                for (int gx = 0; gx < bw * N; ++gx)
                {
                    const int cx = std::min(gx, gw - 1);
                    const int x0 = src.bounds.x0 + static_cast<int>(cx * cell_x);
                    const int x1 =
                        std::max(x0 + 1, std::min(src.bounds.x1, src.bounds.x0 + static_cast<int>((cx + 1) * cell_x)));
                    std::array<double, 3> sum{};
                    for (int y = y0; y < y1; ++y)
                    {
                        for (int x = x0; x < x1; ++x)
                        {
                            const float* px = src.at(x, y);
                            const float inv = px[3] > 0.0f ? 1.0f / px[3] : 0.0f;
                            for (int c = 0; c < 3; ++c)
                            {
                                sum[c] += px[c] * inv;
                            }
                        }
                    }
                    const double n = static_cast<double>(y1 - y0) * (x1 - x0) / 255.0;
                    const double r = sum[0] / n;
                    const double gr = sum[1] / n;
                    const double b = sum[2] / n;
                    float* d = g.at(gx, gy);
                    d[0] = static_cast<float>(0.299 * r + 0.587 * gr + 0.114 * b);
                    d[1] = static_cast<float>(-0.168736 * r - 0.331264 * gr + 0.5 * b);
                    d[2] = static_cast<float>(0.5 * r - 0.418688 * gr - 0.081312 * b);
                }
            }
        });

        const std::vector<damage> errors = plan_damage(s, bw * bh);
        core::parallel::for_range(0, bh, [&](int by_begin, int by_end) {
            for (int by = by_begin; by < by_end; ++by)
            {
                for (int bx = 0; bx < bw; ++bx)
                {
                    const damage& e = errors[static_cast<std::size_t>(by) * bw + bx];
                    for (int c = 0; c < 3; ++c)
                    {
                        block pixels{};
                        for (int i = 0; i < N * N; ++i)
                        {
                            pixels[i] = g.at(bx * N + i % N, by * N + i / N)[c] - (c == 0 ? 128.0f : 0.0f);
                        }
                        block coef = transform(pixels, false);
                        for (int i = 0; i < N * N; ++i)
                        {
                            const float step = s.steps[c][i];
                            coef[i] = step > 0.0f ? std::round(coef[i] / step) * step : coef[i];
                        }
                        coef[0] += e.dc[c] * N;
                        // a few wrong low-frequency coefficients give the striped look of a misdecoded block
                        constexpr int WRONG_COEFS = 3;
                        constexpr int LOW = 4;
                        constexpr float AC_RANGE = 48.0f;
                        for (int k = 0; e.ac != 0.0f && k < WRONG_COEFS; ++k)
                        {
                            const std::uint32_t hk =
                                core::noise::combine(e.seed, static_cast<std::uint32_t>(c * 16 + k));
                            const auto u = static_cast<int>(hk % LOW);
                            const auto w = static_cast<int>((hk / LOW) % LOW);
                            const int i = u + w == 0 ? 1 : w * N + u;
                            coef[i] += static_cast<float>(core::noise::signed_unit(core::noise::hash(hk))) * e.ac *
                                       AC_RANGE * N;
                        }
                        pixels = transform(coef, true);
                        for (int i = 0; i < N * N; ++i)
                        {
                            g.at(bx * N + i % N, by * N + i / N)[c] = pixels[i] + (c == 0 ? 128.0f : 0.0f);
                        }
                    }
                }
            }
        });

        const float saturation = v.scalar_f(p::saturation);
        // scale lights, offset darks: x * sign(L) max(0, |L| - D) + D
        const float lights = v.scalar_f(p::scale_lights);
        const float darks = v.scalar_f(p::offset_darks);
        const float gain = (lights < 0.0f ? -1.0f : 1.0f) * std::max(0.0f, std::abs(lights) - darks);
        const core::image& dst = args.dst;
        core::parallel::for_range(dst.bounds.y0, dst.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                for (int x = dst.bounds.x0; x < dst.bounds.x1; ++x)
                {
                    float* d = dst.at(x, y);
                    if (!src.bounds.contains(x, y))
                    {
                        std::fill_n(d, 4, 0.0f);
                        continue;
                    }
                    const int gx = std::min(gw - 1, static_cast<int>((x - src.bounds.x0) / cell_x));
                    const int gy = std::min(gh - 1, static_cast<int>((y - src.bounds.y0) / cell_y));
                    const float* ycc = g.at(gx, gy);
                    constexpr float INV_255 = 1.0f / 255.0f;
                    const core::color::rgb rgb{(ycc[0] + 1.402f * ycc[2]) * INV_255,
                                               (ycc[0] - 0.344136f * ycc[1] - 0.714136f * ycc[2]) * INV_255,
                                               (ycc[0] + 1.772f * ycc[1]) * INV_255};
                    const core::color::rgb out = core::color::sat_scale(rgb, saturation);
                    const float a = src.at(x, y)[3];
                    std::ranges::transform(out, d, [&](float c) { return (c * gain + darks) * a; });
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
            .match_name = "f_jpegdamage",
            .name = "f_jpegdamage",
            .category = "Functional Stylize",
            .description = "Simulates heavy JPEG compression, with optional corrupted blocks.",
            .params = PARAMS,
            .mask_input = "Mask",
            .background_input = "",
            .time_varying = true,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::jpegdamage
