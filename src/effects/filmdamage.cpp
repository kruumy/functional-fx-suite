#include "effects/filmdamage.hpp"
#include "core/blur.hpp"
#include "core/color.hpp"
#include "core/damage.hpp"
#include "core/edges.hpp"
#include "core/glow.hpp"
#include "core/mask.hpp"
#include "core/noise.hpp"
#include "core/parallel.hpp"
#include "core/sample.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace functional_fx_suite::effects::filmdamage
{
    enum class p : std::uint8_t
    {
        grain_amp,
        grain_details,
        grain_amp_red,
        grain_amp_green,
        grain_amp_blue,
        grain_amp_darks,
        grain_amp_brights,
        grain_blur,
        grain_blur_red,
        grain_blur_green,
        grain_blur_blue,
        grain_mono,
        grain_hold,
        grain_details_end,
        color_correct,
        saturation,
        scale_lights,
        offset_darks,
        tint_lights,
        tint_darks,
        color_correct_end,
        stain_density,
        stains_details,
        vary_stain_density,
        stain_print,
        stain_negative,
        stain_size,
        vary_stain_size,
        stain_opacity,
        vary_stain_opacity,
        vary_stain_brightness,
        vary_stain_color,
        stain_color1,
        stain_color2,
        stains_details_end,
        dust_density,
        dust_details,
        vary_dust_density,
        dust_on_print,
        dust_on_negative,
        dust_size,
        vary_dust_size,
        dust_opacity,
        vary_dust_opacity,
        vary_dust_brightness,
        vary_dust_color,
        dust_color1,
        dust_color2,
        dust_details_end,
        hairs,
        hair_details,
        hair_persistence,
        hair_wiggle_amp,
        hair_wiggle_freq,
        hair_opacity,
        hair_size,
        vary_hair_size,
        hair_color,
        hair_details_end,
        scratches,
        scratches_details,
        black_scratches,
        white_scratches,
        black_scratch_length,
        white_scratch_length,
        scratch_width,
        vary_scratches_width,
        scratches_taper,
        scratch_opacity,
        scratch_roughness,
        scratch_rough_freq,
        gaps,
        gaps_freq,
        scratch_area_center,
        scratch_area_width,
        weave_amount,
        weave_frequency,
        scratches_details_end,
        shake_amplitude,
        shake_details,
        shake_frequency,
        shake_jumpiness,
        shake_random,
        shake_always,
        interframe_border_height,
        shake_time_offset,
        shake_motion_blur,
        shake_details_end,
        vignette_darkness,
        vignette_details,
        vignette_radius,
        vignette_edge_softness,
        vignette_rel_height,
        vignette_details_end,
        flicker,
        flicker_details,
        flicker_rand_amp,
        flicker_rand_freq,
        flicker_wave_amp,
        flicker_wave_freq,
        flicker_details_end,
        defocus,
        defocus_details,
        defocus_rand_amp,
        defocus_rand_freq,
        defocus_wave_amp,
        defocus_wave_freq,
        defocus_details_end,
        seed,
        flip_stamps_vertically,
        mask_use,
        blur_mask,
        invert_mask,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "grain_amp", "Grain Amp").value(0.1).range(-UNBOUNDED, 2).slider(0, 1),
        param::group(2, "grain_details", "Grain Details"),
        param::scalar(3, "grain_amp_red", "Grain Amp Red").value(0.9).range(-UNBOUNDED, UNBOUNDED).slider(0, 1.8),
        param::scalar(4, "grain_amp_green", "Grain Amp Green").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(5, "grain_amp_blue", "Grain Amp Blue").value(1.6).range(-UNBOUNDED, UNBOUNDED).slider(0, 3.2),
        param::scalar(6, "grain_amp_darks", "Grain Amp Darks").value(0.2).range(-UNBOUNDED, 2).slider(0, 2),
        param::scalar(7, "grain_amp_brights", "Grain Amp Brights").value(0).range(-UNBOUNDED, 2).slider(0, 2),
        param::scalar(8, "grain_blur", "Grain Blur").value(0).range(-UNBOUNDED, UNBOUNDED).slider(0, 0.2),
        param::scalar(9, "grain_blur_red", "Grain Blur Red").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(10, "grain_blur_green", "Grain Blur Green")
            .value(0.9)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 1.8),
        param::scalar(11, "grain_blur_blue", "Grain Blur Blue").value(1.2).range(-UNBOUNDED, UNBOUNDED).slider(0, 2.4),
        param::toggle(12, "grain_mono", "Grain Mono"),
        param::choice(13, "grain_hold", "Grain Hold",
                      "Field|Frame|3:2 Pulldown at 0|3:2 Pulldown at 1|3:2 Pulldown at 2|3:2 Pulldown at 3|"
                      "3:2 Pulldown at 4")
            .value(1),
        param::group_end(14, "grain_details_end"),
        param::group(15, "color_correct", "Color Correct"),
        param::scalar(16, "saturation", "Saturation").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(17, "scale_lights", "Scale Lights").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(18, "offset_darks", "Offset Darks").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-5, 1),
        param::color(19, "tint_lights", "Tint Lights").value({1, 1, 1}),
        param::color(20, "tint_darks", "Tint Darks").value({0, 0, 0}),
        param::group_end(21, "color_correct_end"),
        param::scalar(22, "stain_density", "Stain Density").value(0.2).range(-UNBOUNDED, UNBOUNDED).slider(0, 20),
        param::group(23, "stains_details", "Stains Details"),
        param::scalar(24, "vary_stain_density", "Vary Stain Density").value(0.2).range(0, UNBOUNDED).slider(0, 1),
        param::scalar(25, "stain_print", "Stain Print").value(1).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(26, "stain_negative", "Stain Negative").value(0).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(27, "stain_size", "Stain Size").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(28, "vary_stain_size", "Vary Stain Size").value(0.5).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(29, "stain_opacity", "Stain Opacity").value(0.5).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(30, "vary_stain_opacity", "Vary Stain Opacity")
            .value(0.5)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 1),
        param::scalar(31, "vary_stain_brightness", "Vary Stain Brightness")
            .value(0)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 1),
        param::scalar(32, "vary_stain_color", "Vary Stain Color").value(0).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::color(33, "stain_color1", "Stain Color1").value({0, 0, 0}),
        param::color(34, "stain_color2", "Stain Color2").value({0.25, 0.125, 0}),
        param::group_end(35, "stains_details_end"),
        param::scalar(36, "dust_density", "Dust Density").value(30).range(-UNBOUNDED, UNBOUNDED).slider(0, 200),
        param::group(37, "dust_details", "Dust Details"),
        param::scalar(38, "vary_dust_density", "Vary Dust Density").value(0.2).range(0, UNBOUNDED).slider(0, 1),
        param::scalar(39, "dust_on_print", "Dust On Print").value(1).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(40, "dust_on_negative", "Dust On Negative").value(0).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(41, "dust_size", "Dust Size").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(42, "vary_dust_size", "Vary Dust Size").value(0.5).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(43, "dust_opacity", "Dust Opacity").value(0.8).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(44, "vary_dust_opacity", "Vary Dust Opacity")
            .value(0.5)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 1),
        param::scalar(45, "vary_dust_brightness", "Vary Dust Brightness")
            .value(0)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 1),
        param::scalar(46, "vary_dust_color", "Vary Dust Color").value(0).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::color(47, "dust_color1", "Dust Color1").value({0, 0, 0}),
        param::color(48, "dust_color2", "Dust Color2").value({0, 0, 0}),
        param::group_end(49, "dust_details_end"),
        param::scalar(50, "hairs", "Hairs").value(2).range(-UNBOUNDED, UNBOUNDED).slider(0, 50),
        param::group(51, "hair_details", "Hair Details"),
        param::scalar(52, "hair_persistence", "Hair Persistence").value(3).range(0.1, UNBOUNDED).slider(0.1, 6),
        param::scalar(53, "hair_wiggle_amp", "Hair Wiggle Amp").value(0.1).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(54, "hair_wiggle_freq", "Hair Wiggle Freq").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(55, "hair_opacity", "Hair Opacity").value(1).range(0, 1),
        param::scalar(56, "hair_size", "Hair Size").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(57, "vary_hair_size", "Vary Hair Size").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::color(58, "hair_color", "Hair Color").value({0, 0, 0}),
        param::group_end(59, "hair_details_end"),
        param::scalar(60, "scratches", "Scratches").value(5).range(-UNBOUNDED, UNBOUNDED).slider(0, 500).digits(0),
        param::group(61, "scratches_details", "Scratches Details"),
        param::scalar(62, "black_scratches", "Black Scratches").value(1).range(0, 1),
        param::scalar(63, "white_scratches", "White Scratches").value(0.1).range(0, 1),
        param::scalar(64, "black_scratch_length", "Black Scratch Length")
            .value(10)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 20),
        param::scalar(65, "white_scratch_length", "White Scratch Length")
            .value(2)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 4),
        param::scalar(66, "scratch_width", "Scratch Width").value(0.15).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(67, "vary_scratches_width", "Vary Scratches Width").value(1).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(68, "scratches_taper", "Scratches Taper").value(0.1).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(69, "scratch_opacity", "Scratch Opacity").value(1).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(70, "scratch_roughness", "Scratch Roughness").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(71, "scratch_rough_freq", "Scratch Rough Freq")
            .value(150)
            .range(0.01, UNBOUNDED)
            .slider(0.01, 300),
        param::scalar(72, "gaps", "Gaps").value(0.28).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(73, "gaps_freq", "Gaps Freq").value(120).range(-UNBOUNDED, UNBOUNDED).slider(0, 300),
        param::scalar(74, "scratch_area_center", "Scratch Area Center")
            .value(0)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(-2, 2),
        param::scalar(75, "scratch_area_width", "Scratch Area Width")
            .value(1)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 2),
        param::scalar(76, "weave_amount", "Weave Amount").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(77, "weave_frequency", "Weave Frequency").value(0.1).range(0.01, UNBOUNDED).slider(0.01, 10),
        param::group_end(78, "scratches_details_end"),
        param::scalar(79, "shake_amplitude", "Shake Amplitude").value(0).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::group(80, "shake_details", "Shake Details"),
        param::scalar(81, "shake_frequency", "Shake Frequency").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 5),
        param::scalar(82, "shake_jumpiness", "Shake Jumpiness").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(83, "shake_random", "Shake Random").value(0.1).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(84, "shake_always", "Shake Always").value(0.5).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(85, "interframe_border_height", "Interframe Border Height")
            .value(0.1)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 1),
        param::scalar(86, "shake_time_offset", "Shake Time Offset").value(0).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(87, "shake_motion_blur", "Shake Motion Blur")
            .value(0.1)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 2),
        param::group_end(88, "shake_details_end"),
        param::scalar(89, "vignette_darkness", "Vignette Darkness").value(0.1).range(-UNBOUNDED, 1).slider(0, 1),
        param::group(90, "vignette_details", "Vignette Details"),
        param::scalar(91, "vignette_radius", "Vignette Radius").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(92, "vignette_edge_softness", "Vignette Edge Softness")
            .value(0.5)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 1),
        param::scalar(93, "vignette_rel_height", "Vignette Rel Height")
            .value(0.75)
            .range(0.1, UNBOUNDED)
            .slider(0.1, 2),
        param::group_end(94, "vignette_details_end"),
        param::scalar(95, "flicker", "Flicker").value(0.2).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::group(96, "flicker_details", "Flicker Details"),
        param::scalar(97, "flicker_rand_amp", "Flicker Rand Amp").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(98, "flicker_rand_freq", "Flicker Rand Freq")
            .value(10)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 50),
        param::scalar(99, "flicker_wave_amp", "Flicker Wave Amp").value(0).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(100, "flicker_wave_freq", "Flicker Wave Freq")
            .value(5)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 50),
        param::group_end(101, "flicker_details_end"),
        param::scalar(102, "defocus", "Defocus").value(0).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::group(103, "defocus_details", "Defocus Details"),
        param::scalar(104, "defocus_rand_amp", "Defocus Rand Amp").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(105, "defocus_rand_freq", "Defocus Rand Freq")
            .value(10)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 50),
        param::scalar(106, "defocus_wave_amp", "Defocus Wave Amp").value(0).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(107, "defocus_wave_freq", "Defocus Wave Freq")
            .value(5)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 50),
        param::group_end(108, "defocus_details_end"),
        param::scalar(109, "seed", "Seed").value(0.123).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::toggle(110, "flip_stamps_vertically", "Flip Stamps Vertically"),
        param::choice(111, "mask_use", "Mask Use", "Luma|Alpha"),
        param::scalar(112, "blur_mask", "Blur Mask").value(0.05).range(0, 1000).slider(0, 2),
        param::toggle(113, "invert_mask", "Invert Mask"),
    };

    // fixed values of parameters not in the UI
    static constexpr double MIDTONE_POS = 0.5;
    static constexpr double SCRATCH_LUMA_FREQ = 28;
    static constexpr double VARY_SCRATCH_LUMA = 0.1;
    static constexpr double VARY_LUMA_ALONG_SCRATCH = 0.2;
    static constexpr double VARY_SCRATCH_OPACITY = 0.1; // vary_opacity_along_scratch is hardcoded to 0

    using rgb = std::array<double, 3>;

    // a gaussian-like value with standard deviation 0.5 from one hash
    static double grain_noise(std::uint32_t h)
    {
        double sum = 0.0;
        for (std::uint32_t i = 0; i < 4; ++i)
        {
            sum += core::noise::unit(core::noise::combine(h, i));
        }
        return (sum - 2.0) * std::numbers::sqrt3 * 0.5;
    }

    // which film frame the grain belongs to: per field (rows alternate), per frame, or following a 3:2 pulldown
    // cadence (two film frames over five video fields) starting at the given frame
    static std::int64_t grain_frame(int hold, std::int64_t frame, int parity)
    {
        const std::int64_t field = 2 * frame + parity;
        if (hold <= 0)
        {
            return field;
        }
        if (hold == 1)
        {
            return frame;
        }
        const std::int64_t f = 2 * (frame + (hold - 2)) + parity;
        const std::int64_t group = f >= 0 ? f / 5 : (f - 4) / 5;
        return 2 * group + (f - group * 5 >= 2 ? 1 : 0);
    }

    // stains / dust arguments
    struct stamp_spec
    {
        double seed = 0;
        rgb color1{};
        rgb color2{};
        double vary_color = 0;
        double brightness = 1;
        double vary_brightness = 0;
        double opacity = 0;
        double vary_opacity = 0;
        double density = 0; // stamps per frame
        double vary_density = 0;
        double scale = 1;
        double vary_scale = 0;
        double base_radius = 0; // xy units at scale 1
        bool soft = false;
    };

    struct stamp
    {
        double x = 0;
        double y = 0;
        double r = 0;
        std::array<float, 3> color{};
        float opacity = 0;
        std::uint32_t seed = 0;
        bool soft = false;
    };

    // stains / dust: Density random blobs per frame with
    // irregular edges, colors between Color1 and Color2
    static void add_stamps(std::vector<stamp>& out, const stamp_spec& s, std::int64_t frame, double aspect, bool flip)
    {
        namespace n = core::noise;
        const std::uint32_t h0 = n::combine(n::seed_bits(s.seed), static_cast<std::uint32_t>(frame));
        const double count = s.density + s.vary_density * n::signed_unit(n::combine(h0, 0));
        constexpr double MAX_STAMPS = 10000;
        const auto total =
            static_cast<int>(std::clamp(std::floor(count + n::unit(n::combine(h0, 1))), 0.0, MAX_STAMPS));
        for (int i = 0; i < total; ++i)
        {
            const std::uint32_t h = n::combine(h0, static_cast<std::uint32_t>(i) + 2);
            stamp st;
            st.x = n::signed_unit(n::combine(h, 0));
            st.y = n::signed_unit(n::combine(h, 1)) * aspect * (flip ? -1.0 : 1.0);
            st.r = s.base_radius * std::max(0.0, s.scale * (1.0 + s.vary_scale * n::signed_unit(n::combine(h, 2))));
            const double mix = n::unit(n::combine(h, 3));
            const double bright = s.brightness * (1.0 + s.vary_brightness * n::signed_unit(n::combine(h, 4)));
            for (int c = 0; c < 3; ++c)
            {
                const double base = s.color1[c] + (s.color2[c] - s.color1[c]) * mix;
                const double jitter = s.vary_color * n::signed_unit(n::combine(h, 5 + c));
                st.color[c] = static_cast<float>((base + jitter) * bright);
            }
            st.opacity =
                static_cast<float>(std::clamp(s.opacity + s.vary_opacity * n::signed_unit(n::combine(h, 8)), 0.0, 1.0));
            st.seed = n::combine(h, 9);
            st.soft = s.soft;
            if (st.r > 0.0 && st.opacity > 0.0f)
            {
                out.push_back(st);
            }
        }
    }

    // coverage of one blob at (x, y), xy units; px is one pixel in xy units
    static double stamp_alpha(const stamp& s, double x, double y, double px)
    {
        const double dx = x - s.x;
        const double dy = y - s.y;
        const double r = std::hypot(dx, dy);
        if (r > s.r * 1.25 + px)
        {
            return 0.0;
        }
        // the edge wanders with the angle around the centre
        const double a = std::atan2(dy, dx);
        constexpr double EDGE_FREQ = 1.5;
        const double edge = core::noise::value_2d(s.seed, std::cos(a) * EDGE_FREQ + 7.0, std::sin(a) * EDGE_FREQ + 7.0);
        const double reach = s.r * (0.75 + 0.5 * edge);
        if (!s.soft)
        {
            return std::clamp((reach - r) / px + 0.5, 0.0, 1.0);
        }
        constexpr double MOTTLE_FREQ = 3.0;
        const double mottle = core::noise::value_2d(s.seed + 1, dx / s.r * MOTTLE_FREQ, dy / s.r * MOTTLE_FREQ);
        return std::clamp((reach - r) / std::max(px, 0.5 * s.r), 0.0, 1.0) * (0.4 + 0.6 * mottle);
    }

    static void draw_stamps(const core::image& img, const std::vector<stamp>& stamps, const core::frame& f)
    {
        if (stamps.empty())
        {
            return;
        }
        const double px = core::XY_PER_FRAME_UNIT / f.width;
        core::parallel::for_range(img.bounds.y0, img.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                const double yy = f.to_ny(y + 0.5) * core::XY_PER_FRAME_UNIT;
                for (const stamp& s : stamps)
                {
                    const double reach = s.r * 1.25 + px;
                    if (std::abs(yy - s.y) > reach)
                    {
                        continue;
                    }
                    const int x0 =
                        std::max(img.bounds.x0,
                                 static_cast<int>(std::floor(f.to_px_x((s.x - reach) / core::XY_PER_FRAME_UNIT))));
                    const int x1 = std::min(
                        img.bounds.x1, static_cast<int>(std::ceil(f.to_px_x((s.x + reach) / core::XY_PER_FRAME_UNIT))));
                    for (int x = x0; x < x1; ++x)
                    {
                        const double xx = f.to_nx(x + 0.5) * core::XY_PER_FRAME_UNIT;
                        const auto k = static_cast<float>(stamp_alpha(s, xx, yy, px)) * s.opacity;
                        float* d = img.at(x, y);
                        for (int c = 0; c < 3; ++c)
                        {
                            d[c] = core::color::lerp(d[c], s.color[c], k);
                        }
                    }
                }
            }
        });
    }

    struct scratch
    {
        std::uint32_t seed = 0;
        double x = 0;          // xy units
        double half_width = 0; // xy units
        double yc = 0;         // centre and half length along y
        double half_length = 0;
        float strength = 0;
        bool white = false;
    };

    // scratches: vertical lines that last Length frames each,
    // spread over the scratch area, weaving slowly, rough along their length, broken by Gaps and tapered at the ends
    static std::vector<scratch> make_scratches(const core::param_values& v, std::int64_t frame, double t, double aspect)
    {
        namespace n = core::noise;
        std::vector<scratch> out;
        const double total = std::max(0.0, std::round(v.scalar(p::scratches)));
        const double center = v.scalar(p::scratch_area_center);
        const double area = v.scalar(p::scratch_area_width);
        const double width = v.scalar(p::scratch_width);
        const double vary_width = v.scalar(p::vary_scratches_width);
        const double opacity = v.scalar(p::scratch_opacity);
        const double weave = v.scalar(p::weave_amount);
        const double weave_freq = v.scalar(p::weave_frequency);
        const std::uint32_t seed = n::seed_bits(v.scalar(p::seed) + 0.3);
        constexpr double WIDTH_UNIT = 0.02;
        constexpr double WEAVE_UNIT = 0.02;
        constexpr int MAX_SCRATCHES = 4096;
        for (const bool white : {false, true})
        {
            const double count = total * v.scalar(white ? p::white_scratches : p::black_scratches);
            const double length = std::max(1.0, v.scalar(white ? p::white_scratch_length : p::black_scratch_length));
            const int slots = std::min(MAX_SCRATCHES, static_cast<int>(std::ceil(count)));
            for (int k = 0; k < slots; ++k)
            {
                const std::uint32_t hk = n::combine(seed, static_cast<std::uint32_t>(k * 2 + (white ? 1 : 0)));
                // each slot holds one scratch for Length frames, staggered across slots
                const double phase = n::unit(n::combine(hk, 0)) * length;
                const auto life = static_cast<std::int64_t>(std::floor((static_cast<double>(frame) + phase) / length));
                const std::uint32_t h = n::combine(hk, static_cast<std::uint32_t>(life) + 1);
                if (k + 1 > count && n::unit(n::combine(h, 0)) >= count - k)
                {
                    continue;
                }
                scratch s;
                s.seed = h;
                s.white = white;
                s.x = center + area * n::signed_unit(n::combine(h, 1)) +
                      weave * WEAVE_UNIT * (n::value_2d(n::combine(h, 2), t * weave_freq, 0.0) - 0.5) * 2.0;
                s.half_width = 0.5 * width * WIDTH_UNIT * (1.0 - vary_width * n::unit(n::combine(h, 3)));
                s.yc = n::signed_unit(n::combine(h, 4)) * aspect;
                s.half_length = aspect * (0.75 + 1.5 * n::unit(n::combine(h, 5)));
                s.strength = static_cast<float>(opacity * (1.0 - VARY_SCRATCH_OPACITY * n::unit(n::combine(h, 6))) *
                                                (1.0 - VARY_SCRATCH_LUMA * n::unit(n::combine(h, 7))));
                if (s.half_width > 0.0 && s.strength != 0.0f)
                {
                    out.push_back(s);
                }
            }
        }
        return out;
    }

    static void draw_scratches(const core::image& img, const std::vector<scratch>& scratches,
                               const core::param_values& v, const core::frame& f, bool flip)
    {
        if (scratches.empty())
        {
            return;
        }
        const double roughness = v.scalar(p::scratch_roughness);
        const double rough_freq = v.scalar(p::scratch_rough_freq);
        const double gaps = v.scalar(p::gaps);
        const double gaps_freq = v.scalar(p::gaps_freq);
        const double taper = v.scalar(p::scratches_taper);
        const double px = core::XY_PER_FRAME_UNIT / f.width;
        core::parallel::for_range(img.bounds.y0, img.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                const double yy = f.to_ny(y + 0.5) * core::XY_PER_FRAME_UNIT * (flip ? -1.0 : 1.0);
                for (const scratch& s : scratches)
                {
                    const double along = s.half_length - std::abs(yy - s.yc);
                    if (along <= 0.0 || core::noise::value_2d(s.seed + 1, 0.5, yy * gaps_freq) < gaps * 0.5)
                    {
                        continue;
                    }
                    const double tapered = taper > 0.0 ? std::min(1.0, along / (taper * s.half_length)) : 1.0;
                    const double half = s.half_width * tapered;
                    const double x = s.x + roughness * s.half_width *
                                               (core::noise::fbm_2d(s.seed + 2, 0.5, yy * rough_freq, 2) - 0.5) * 2.0;
                    const auto strength = static_cast<float>(
                        s.strength * (1.0 - VARY_LUMA_ALONG_SCRATCH *
                                                core::noise::value_2d(s.seed + 3, 0.5, yy * SCRATCH_LUMA_FREQ)));
                    const int x0 =
                        std::max(img.bounds.x0,
                                 static_cast<int>(std::floor(f.to_px_x((x - half - px) / core::XY_PER_FRAME_UNIT))));
                    const int x1 =
                        std::min(img.bounds.x1,
                                 static_cast<int>(std::ceil(f.to_px_x((x + half + px) / core::XY_PER_FRAME_UNIT))));
                    for (int ix = x0; ix < x1; ++ix)
                    {
                        // pixel coverage of the line, at least a pixel wide so thin scratches fade instead of vanishing
                        const double dist = std::abs(f.to_nx(ix + 0.5) * core::XY_PER_FRAME_UNIT - x);
                        const double cover =
                            std::clamp((half + 0.5 * px - dist) / px, 0.0, 1.0) * std::min(1.0, 2.0 * half / px);
                        const auto k = static_cast<float>(cover) * strength;
                        float* d = img.at(ix, y);
                        for (int c = 0; c < 3; ++c)
                        {
                            d[c] = s.white ? d[c] + (1.0f - d[c]) * k : d[c] * (1.0f - k);
                        }
                    }
                }
            }
        });
    }

    struct hair
    {
        std::vector<std::array<double, 2>> points; // xy units
        double half_width = 0;
        double x0 = 0; // bounding box, xy units
        double x1 = 0;
        double y0 = 0;
        double y1 = 0;
    };

    // hairs: up to Hairs curly strands, each showing while a
    // slow noise (Persistence seconds) is above 1 - sqrt(Hairs / 10), wiggling with Wiggle Amp / Freq
    static std::vector<hair> make_hairs(const core::param_values& v, double t, double aspect, bool flip)
    {
        namespace n = core::noise;
        std::vector<hair> out;
        const double count = std::max(0.0, v.scalar(p::hairs));
        const double freq = 1.0 / v.scalar(p::hair_persistence);
        const double threshold = 1.0 - std::sqrt(count / 10.0);
        const double size = v.scalar(p::hair_size);
        const double vary = v.scalar(p::vary_hair_size);
        const double wiggle = v.scalar(p::hair_wiggle_amp);
        const double wiggle_freq = v.scalar(p::hair_wiggle_freq);
        const std::uint32_t seed = n::seed_bits(v.scalar(p::seed) + 0.4);
        constexpr int MAX_HAIRS = 256;
        constexpr int POINTS = 24;
        constexpr double LENGTH = 0.3;
        constexpr double HALF_WIDTH = 0.0015;
        constexpr double WIGGLE_UNIT = 0.05;
        const int slots = std::min(MAX_HAIRS, static_cast<int>(std::ceil(count)));
        for (int k = 0; k < slots; ++k)
        {
            const std::uint32_t hk = n::combine(seed, static_cast<std::uint32_t>(k));
            if (k + 1 > count && n::unit(n::combine(hk, 0)) >= count - k)
            {
                continue;
            }
            if (n::value_2d(hk + 1, t * freq, 0.0) <= threshold)
            {
                continue;
            }
            // the shape is kept while it shows, renewed every 1 / freq seconds
            const std::uint32_t h = n::combine(hk + 2, n::cell(std::floor(t * freq)));
            const double scale = std::max(0.0, size * (1.0 + 0.5 * vary * n::signed_unit(n::combine(h, 0))));
            if (scale <= 0.0)
            {
                continue;
            }
            hair hr;
            hr.half_width = HALF_WIDTH * std::max(0.5, size);
            double x = n::signed_unit(n::combine(h, 1));
            double y = n::signed_unit(n::combine(h, 2)) * aspect;
            double heading = n::unit(n::combine(h, 3)) * 2.0 * std::numbers::pi;
            const double step = LENGTH * scale / (POINTS - 1);
            for (int i = 0; i < POINTS; ++i)
            {
                const double wx = wiggle * WIGGLE_UNIT * (n::value_2d(h + 3, i * 0.3, t * wiggle_freq) - 0.5);
                const double wy = wiggle * WIGGLE_UNIT * (n::value_2d(h + 4, i * 0.3, t * wiggle_freq) - 0.5);
                hr.points.push_back({x + wx, (y + wy) * (flip ? -1.0 : 1.0)});
                heading += (n::value_2d(h + 5, i * 0.25, 0.5) - 0.5) * 1.5;
                x += std::cos(heading) * step;
                y += std::sin(heading) * step;
            }
            const auto [xmin, xmax] = std::ranges::minmax(hr.points, {}, [](const auto& q) { return q[0]; });
            const auto [ymin, ymax] = std::ranges::minmax(hr.points, {}, [](const auto& q) { return q[1]; });
            hr.x0 = xmin[0] - hr.half_width;
            hr.x1 = xmax[0] + hr.half_width;
            hr.y0 = ymin[1] - hr.half_width;
            hr.y1 = ymax[1] + hr.half_width;
            out.push_back(std::move(hr));
        }
        return out;
    }

    // coverage of a hair at (x, y): distance to its polyline against its half width, antialiased over a pixel
    static double hair_alpha(const hair& h, double x, double y, double px)
    {
        if (x < h.x0 - px || x > h.x1 + px || y < h.y0 - px || y > h.y1 + px)
        {
            return 0.0;
        }
        double best = 1e30;
        for (std::size_t i = 0; i + 1 < h.points.size(); ++i)
        {
            const auto& a = h.points[i];
            const auto& b = h.points[i + 1];
            const double ex = b[0] - a[0];
            const double ey = b[1] - a[1];
            const double len2 = ex * ex + ey * ey;
            const double s = len2 > 0.0 ? std::clamp(((x - a[0]) * ex + (y - a[1]) * ey) / len2, 0.0, 1.0) : 0.0;
            best = std::min(best, std::hypot(x - a[0] - s * ex, y - a[1] - s * ey));
        }
        return std::clamp((h.half_width + 0.5 * px - best) / px, 0.0, 1.0) * std::min(1.0, 2.0 * h.half_width / px);
    }

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const core::frame& f = args.geometry;
        const core::image& src = args.src;
        const double t = args.time;
        const auto frame = args.frame_number();
        const double seed = v.scalar(p::seed);
        const double aspect = src.bounds.height() / f.width;
        const bool flip = v.toggle(p::flip_stamps_vertically);

        // grain: per-channel noise scaled by Grain Amp x channel amp, weighted by a darks / midtones / brights curve
        core::buffer film = core::edges::straight_rgb(src);
        const core::image& fl = film.view();
        const double grain_amp = v.scalar(p::grain_amp);
        const rgb amps{grain_amp * v.scalar(p::grain_amp_red), grain_amp * v.scalar(p::grain_amp_green),
                       grain_amp * v.scalar(p::grain_amp_blue)};
        const double darks_amp = v.scalar(p::grain_amp_darks);
        const double brights_amp = v.scalar(p::grain_amp_brights);
        const bool mono = v.toggle(p::grain_mono);
        const int hold = v.choice(p::grain_hold);
        const std::uint32_t grain_seed = core::noise::seed_bits(seed + 0.123);
        const auto grain_hash = [&](int x, int y, int c) {
            const auto [px, py] = core::damage::frame_pixel(f, x, y);
            const std::uint32_t frame_hash = core::noise::combine(
                grain_seed, static_cast<std::uint32_t>(grain_frame(hold, frame, static_cast<int>(py & 1U))));
            const std::uint32_t pixel = core::noise::combine(core::noise::combine(frame_hash, px), py);
            return core::noise::combine(pixel, mono ? 0U : static_cast<std::uint32_t>(c));
        };
        core::buffer grain;
        const double grain_blur = std::max(0.0, v.scalar(p::grain_blur));
        if (grain_amp != 0.0 && grain_blur > 0.0)
        {
            grain.reset(src.bounds, 3);
            const core::image& g = grain.view();
            core::parallel::for_range(g.bounds.y0, g.bounds.y1, [&](int y_begin, int y_end) {
                for (int y = y_begin; y < y_end; ++y)
                {
                    for (int x = g.bounds.x0; x < g.bounds.x1; ++x)
                    {
                        for (int c = 0; c < 3; ++c)
                        {
                            g.at(x, y)[c] = static_cast<float>(grain_noise(grain_hash(x, y, c)));
                        }
                    }
                }
            });
            const std::array channel_blur{v.scalar(p::grain_blur_red), v.scalar(p::grain_blur_green),
                                          v.scalar(p::grain_blur_blue)};
            rgb amount{};
            rgb gain{};
            for (int c = 0; c < 3; ++c)
            {
                amount[c] = core::blur::amount_px(std::max(0.0, grain_blur * channel_blur[c]), f);
                // blurred noise loses contrast; this keeps the grain's strength roughly constant
                gain[c] = std::max(1.0, 2.0 * std::sqrt(std::numbers::pi) * amount[c]);
            }
            core::glow::blur_channels(g, amount, amount, gain, core::blur::edge::transparent);
        }

        const auto saturation = v.scalar_f(p::saturation);
        std::array<float, 3> lights{};
        std::array<float, 3> darks{};
        for (int c = 0; c < 3; ++c)
        {
            lights[c] = static_cast<float>(v.vec(p::tint_lights)[c] * v.scalar(p::scale_lights));
            darks[c] = static_cast<float>(v.vec(p::tint_darks)[c] + v.scalar(p::offset_darks));
        }
        core::parallel::for_range(fl.bounds.y0, fl.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                for (int x = fl.bounds.x0; x < fl.bounds.x1; ++x)
                {
                    float* d = fl.at(x, y);
                    core::color::rgb c{d[0], d[1], d[2]};
                    if (grain_amp != 0.0)
                    {
                        for (int ch = 0; ch < 3; ++ch)
                        {
                            const double level = std::clamp(static_cast<double>(c[ch]), 0.0, 1.0);
                            const double response =
                                level < MIDTONE_POS
                                    ? darks_amp + (1.0 - darks_amp) * level / MIDTONE_POS
                                    : 1.0 + (brights_amp - 1.0) * (level - MIDTONE_POS) / (1.0 - MIDTONE_POS);
                            const double n = grain.view().data != nullptr ? grain.view().at(x, y)[ch]
                                                                          : grain_noise(grain_hash(x, y, ch));
                            c[ch] += static_cast<float>(amps[ch] * response * n);
                        }
                    }
                    c = core::color::sat_scale(c, saturation);
                    for (int ch = 0; ch < 3; ++ch)
                    {
                        d[ch] = core::color::scale_lights_offset_darks(c[ch], lights[ch], darks[ch]);
                    }
                }
            }
        });

        // the negative gets stains and dust in inverted colors, then the scratches; the print gets its own
        const auto stamps = [&](bool negative, bool dust) {
            const double density = dust ? v.scalar(p::dust_density) : v.scalar(p::stain_density);
            const double on = dust ? v.scalar(negative ? p::dust_on_negative : p::dust_on_print)
                                   : v.scalar(negative ? p::stain_negative : p::stain_print);
            const rgb c1 = v.vec(dust ? p::dust_color1 : p::stain_color1);
            const rgb c2 = v.vec(dust ? p::dust_color2 : p::stain_color2);
            const auto invert = [negative](const rgb& c) { return negative ? rgb{1 - c[0], 1 - c[1], 1 - c[2]} : c; };
            const double opacity = v.scalar(dust ? p::dust_opacity : p::stain_opacity);
            constexpr double STAIN_RADIUS = 0.08;
            constexpr double DUST_RADIUS = 0.012;
            const stamp_spec s{
                .seed = seed + (dust ? (negative ? 0.5 : 0.6) : (negative ? 0.0 : 0.1)),
                .color1 = invert(c1),
                .color2 = invert(c2),
                .vary_color = v.scalar(dust ? p::vary_dust_color : p::vary_stain_color),
                .brightness = 1.0,
                .vary_brightness = v.scalar(dust ? p::vary_dust_brightness : p::vary_stain_brightness),
                .opacity = opacity,
                .vary_opacity = opacity * v.scalar(dust ? p::vary_dust_opacity : p::vary_stain_opacity),
                .density = on * density,
                .vary_density = on * density * v.scalar(dust ? p::vary_dust_density : p::vary_stain_density),
                .scale = dust ? v.scalar(p::dust_size) * 0.5 : v.scalar(p::stain_size),
                .vary_scale = v.scalar(dust ? p::vary_dust_size : p::vary_stain_size),
                .base_radius = dust ? DUST_RADIUS : STAIN_RADIUS,
                .soft = !dust,
            };
            std::vector<stamp> out;
            add_stamps(out, s, frame, aspect, flip);
            return out;
        };
        draw_stamps(fl, stamps(true, false), f);
        draw_stamps(fl, stamps(true, true), f);
        draw_scratches(fl, make_scratches(v, frame, t, aspect), v, f, flip);
        draw_stamps(fl, stamps(false, false), f);
        draw_stamps(fl, stamps(false, true), f);

        // shake: the frame slips vertically, showing the black border between film frames
        const double fps = 1.0 / args.frame_duration;
        const double ytime = (static_cast<double>(frame) - v.scalar(p::shake_time_offset)) * (30.0 / fps);
        const double shake_always = v.scalar(p::shake_always);
        core::damage::roll shake;
        if (shake_always > 0.0)
        {
            const double freq = v.scalar(p::shake_frequency) / 4.0;
            shake = core::damage::sawtooth_plus_noise(seed + 0.3, ytime, 6, -1.0, 1.0, v.scalar(p::shake_random),
                                                      v.scalar(p::shake_jumpiness), 1.2 / (shake_always + 0.2) - 1.0,
                                                      freq, 5.0 * freq);
        }
        const double amplitude = v.scalar(p::shake_amplitude) * 2.0 * aspect;
        const double yshift = shake.value * amplitude;
        const double yslope = std::abs(shake.slope * amplitude);
        core::buffer shaken;
        const core::image* proj = &fl;
        if (yshift != 0.0)
        {
            shaken.reset(src.bounds, 3);
            const core::image& s = shaken.view();
            const double wrap = (1.0 + v.scalar(p::interframe_border_height)) * 2.0 * aspect;
            core::parallel::for_range(s.bounds.y0, s.bounds.y1, [&](int y_begin, int y_end) {
                for (int y = y_begin; y < y_end; ++y)
                {
                    const double yy = f.to_ny(y + 0.5) * core::XY_PER_FRAME_UNIT;
                    const double sy = yy + yshift + aspect;
                    const double wy = sy - std::floor(sy / wrap) * wrap - aspect;
                    const double py = f.to_px_y(wy / core::XY_PER_FRAME_UNIT);
                    for (int x = s.bounds.x0; x < s.bounds.x1; ++x)
                    {
                        const core::sample::pixel q = core::sample::bilinear(fl, x + 0.5, py, core::sample::wrap::none);
                        std::copy_n(q.begin(), 3, s.at(x, y));
                    }
                }
            });
            proj = &shaken.view();
        }

        // projection: defocus and motion blur, the blur's gain is the flicker
        const double flicker_rand =
            v.scalar(p::flicker_rand_amp) *
            (core::noise::value_2d(core::noise::seed_bits(seed), t * v.scalar(p::flicker_rand_freq), 0.0) - 0.5) * 2.0;
        const double flicker_wave = std::sin(2.0 * std::numbers::pi * (t * v.scalar(p::flicker_wave_freq) - 0.25)) *
                                    v.scalar(p::flicker_wave_amp);
        const double blur_rand =
            v.scalar(p::defocus_rand_amp) *
            (core::noise::value_2d(core::noise::seed_bits(seed + 0.2), t * v.scalar(p::defocus_rand_freq), 0.0) - 0.5) *
            2.0;
        const double blur_wave = std::sin(2.0 * std::numbers::pi * (t * v.scalar(p::defocus_wave_freq) - 0.75)) *
                                 v.scalar(p::defocus_wave_amp);
        const double defocus = v.scalar(p::defocus) * (blur_rand + blur_wave);
        const double blur_x = std::max(0.0, defocus);
        const double blur_y = std::max(0.0, defocus + v.scalar(p::shake_motion_blur) * yslope);
        if (blur_x > 0.0 || blur_y > 0.0)
        {
            if (proj == &fl)
            {
                shaken = core::buffer::copy_of(fl);
                proj = &shaken.view();
            }
            core::blur::gaussian(*proj, core::blur::amount_px(blur_x, f), core::blur::amount_px(blur_y, f));
        }
        const auto gain = static_cast<float>(1.0 + v.scalar(p::flicker) * (flicker_rand + flicker_wave));

        const double vignette = v.scalar(p::vignette_darkness);
        const std::vector<hair> hairs = make_hairs(v, t, aspect, flip);
        const auto hair_color = v.vec(p::hair_color);
        const auto hair_opacity = v.scalar_f(p::hair_opacity);
        const std::uint32_t frame_seed = core::noise::seed_bits(seed + static_cast<double>(frame));
        const double px = core::XY_PER_FRAME_UNIT / f.width;
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
                    const float* s = proj->at(x, y);
                    const auto keep = static_cast<float>(
                        vignette != 0.0
                            ? 1.0 + (core::damage::vignette_keep(xx, yy, v.scalar(p::vignette_radius),
                                                                 v.scalar(p::vignette_rel_height),
                                                                 v.scalar(p::vignette_edge_softness),
                                                                 core::damage::pixel_static(frame_seed, f, x, y)) -
                                     1.0) *
                                        vignette
                            : 1.0);
                    std::array<float, 3> c{};
                    for (int ch = 0; ch < 3; ++ch)
                    {
                        c[ch] = s[ch] * gain * keep;
                    }
                    for (const hair& h : hairs)
                    {
                        if (const auto k = static_cast<float>(hair_alpha(h, xx, yy, px)) * hair_opacity; k > 0.0f)
                        {
                            for (int ch = 0; ch < 3; ++ch)
                            {
                                c[ch] = core::color::lerp(c[ch], static_cast<float>(hair_color[ch]), k);
                            }
                        }
                    }
                    const float a = src.at(x, y)[3];
                    std::ranges::transform(c, d, [a](float value) { return value * a; });
                    d[3] = a;
                }
            }
        });

        core::mask::apply(args, core::mask::luma_or_alpha(v.choice(p::mask_use)), v.scalar(p::blur_mask),
                          v.toggle(p::invert_mask));
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_filmdamage",
            .name = "f_filmdamage",
            .category = "Functional Stylize",
            .description = "Simulates old projected film: grain, stains, dust, scratches, hairs, gate shake, "
                           "flicker, defocus and a vignette.",
            .params = PARAMS,
            .mask_input = "Mask",
            .background_input = "",
            .time_varying = true,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::filmdamage
