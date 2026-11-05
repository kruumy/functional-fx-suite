#include "effects/tvdamage.hpp"
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
#include <utility>
#include <vector>

namespace functional_fx_suite::effects::tvdamage
{
    enum class p : std::uint8_t
    {
        style,
        reception_master,
        interference_amp,
        ghost_amp,
        horizontal_hold,
        vertical_hold,
        bars_brightness,
        color_stripes_amplitude,
        fast_forward_amount,
        tape_dropout_brightness,
        vignette_darkness,
        static_group,
        static_amplitude,
        static_density,
        static_group_end,
        interference_details,
        frequency,
        dots_speed_x,
        dots_speed_y,
        jitter_amount,
        interference_details_end,
        ghosting_details,
        num_ghosts,
        negative_ghosts,
        spacing,
        vary_position,
        shift,
        blur,
        ghosting_details_end,
        horiz_hold_details,
        h_frequency,
        h_time_vary,
        h_octaves,
        border_width,
        horiz_hold_details_end,
        vert_hold_details,
        v_frequency,
        v_speed,
        v_random,
        border_height,
        border_data,
        vert_hold_details_end,
        bars_details,
        bar_roll_speed,
        bar_sharpness,
        bar_frequency,
        bar1_width,
        bar2_rel_frequency,
        bar2_sharpness,
        bars_details_end,
        stripes_details,
        color_frequency,
        color_angle,
        roll_speed,
        stripes_details_end,
        fast_forward_details,
        band_frequency,
        band_shift,
        band_height,
        fast_forward_details_end,
        dropouts_details,
        dropout_length,
        dropout_gap_length,
        dropout_y_freq,
        dropout_y_threshold,
        dropouts_always,
        dropouts_details_end,
        vignette_details,
        vignette_radius,
        vignette_edge_softness,
        vignette_rel_height,
        vignette_details_end,
        scanlines_group,
        scanlines,
        scanlines_rel_freq,
        scanlines_group_end,
        orthicon_group,
        orthicon,
        threshold,
        darks_width,
        orthicon_group_end,
        color_correct,
        hue_shift,
        saturation,
        scale_lights,
        offset_darks,
        tint_lights,
        tint_darks,
        color_correct_end,
        turn_off,
        turnoff_details,
        flare_width,
        flare_brightness,
        fade_out_time,
        turnoff_details_end,
        fish_eye,
        tv_pixels,
        seed,
        mask_use,
        blur_mask,
        invert_mask,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    // Color and Mono share every parameter, so Style picks between them; Color Angle is in degrees
    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::choice(1, "style", "Style", "Color|Mono"),
        param::scalar(2, "reception_master", "Reception Master").value(0.4).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(3, "interference_amp", "Interference Amp").value(0.6).range(-UNBOUNDED, UNBOUNDED).slider(0, 1.2),
        param::scalar(4, "ghost_amp", "Ghost Amp").value(0.6).range(-UNBOUNDED, UNBOUNDED).slider(0, 1.2),
        param::scalar(5, "horizontal_hold", "Horizontal Hold").value(0.5).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(6, "vertical_hold", "Vertical Hold").value(0.8).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(7, "bars_brightness", "Bars Brightness").value(0.3).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(8, "color_stripes_amplitude", "Color Stripes Amplitude")
            .value(0.2)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 1),
        param::scalar(9, "fast_forward_amount", "Fast Forward Amount")
            .value(0)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 1),
        param::scalar(10, "tape_dropout_brightness", "Tape Dropout Brightness")
            .value(0)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 1),
        param::scalar(11, "vignette_darkness", "Vignette Darkness").value(0).range(-UNBOUNDED, 1).slider(0, 1),
        param::group(12, "static_group", "Static"),
        param::scalar(13, "static_amplitude", "Static Amplitude")
            .value(0.8)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 1.6),
        param::scalar(14, "static_density", "Static Density").value(0.7).range(0.01, 1),
        param::group_end(15, "static_group_end"),
        param::group(16, "interference_details", "Interference Details"),
        param::scalar(17, "frequency", "Frequency").value(1.275).range(-UNBOUNDED, 500).slider(0, 2.55),
        param::scalar(18, "dots_speed_x", "Dots Speed X").value(100).range(-UNBOUNDED, UNBOUNDED).slider(-200, 200),
        param::scalar(19, "dots_speed_y", "Dots Speed Y").value(-10).range(-UNBOUNDED, UNBOUNDED).slider(-200, 200),
        param::scalar(20, "jitter_amount", "Jitter Amount").value(10).range(-UNBOUNDED, 1000).slider(0, 20),
        param::group_end(21, "interference_details_end"),
        param::group(22, "ghosting_details", "Ghosting Details"),
        param::scalar(23, "num_ghosts", "Num Ghosts").value(5).range(-UNBOUNDED, 30).slider(0, 10).digits(0),
        param::scalar(24, "negative_ghosts", "Negative Ghosts").value(0.5).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(25, "spacing", "Spacing").value(0.2).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(26, "vary_position", "Vary Position").value(0.3).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(27, "shift", "Shift").value(0.5).range(-1, 1),
        param::scalar(28, "blur", "Blur").value(0).range(0, UNBOUNDED).slider(0, 1),
        param::group_end(29, "ghosting_details_end"),
        param::group(30, "horiz_hold_details", "Horiz. Hold Details"),
        param::scalar(31, "h_frequency", "H Frequency").value(1.25).range(-UNBOUNDED, UNBOUNDED).slider(0, 2.5),
        param::scalar(32, "h_time_vary", "H Time Vary").value(0.5).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(33, "h_octaves", "H Octaves").value(3).range(1, 10).digits(0),
        param::scalar(34, "border_width", "Border Width").value(0.05).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::group_end(35, "horiz_hold_details_end"),
        param::group(36, "vert_hold_details", "Vert. Hold Details"),
        param::scalar(37, "v_frequency", "V Frequency").value(2).range(-UNBOUNDED, UNBOUNDED).slider(0, 4),
        param::scalar(38, "v_speed", "V Speed").value(2).range(-UNBOUNDED, UNBOUNDED).slider(0, 4),
        param::scalar(39, "v_random", "V Random").value(0.1).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(40, "border_height", "Border Height").value(0.1).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(41, "border_data", "Border Data").value(1).range(-UNBOUNDED, 10).slider(0, 2),
        param::group_end(42, "vert_hold_details_end"),
        param::group(43, "bars_details", "Bars Details"),
        param::scalar(44, "bar_roll_speed", "Bar Roll Speed").value(0.5).range(-10, 10),
        param::scalar(45, "bar_sharpness", "Bar Sharpness").value(0.5).range(0.1, 10),
        param::scalar(46, "bar_frequency", "Bar Frequency").value(1).range(0.1, UNBOUNDED).slider(0.1, 2),
        param::scalar(47, "bar1_width", "Bar1 Width").value(0.35).range(0, 1),
        param::scalar(48, "bar2_rel_frequency", "Bar2 Rel Frequency").value(6).range(1, UNBOUNDED).slider(1, 12),
        param::scalar(49, "bar2_sharpness", "Bar2 Sharpness").value(0.5).range(0.01, 10),
        param::group_end(50, "bars_details_end"),
        param::group(51, "stripes_details", "Stripes Details"),
        param::scalar(52, "color_frequency", "Color Frequency").value(10).range(1, 40),
        param::scalar(53, "color_angle", "Color Angle").value(160).range(-UNBOUNDED, UNBOUNDED).slider(-180, 180),
        param::scalar(54, "roll_speed", "Roll Speed").value(3).range(-UNBOUNDED, UNBOUNDED).slider(0, 6),
        param::group_end(55, "stripes_details_end"),
        param::group(56, "fast_forward_details", "Fast Forward Details"),
        param::scalar(57, "band_frequency", "Band Frequency").value(4).range(-UNBOUNDED, UNBOUNDED).slider(0, 8),
        param::scalar(58, "band_shift", "Band Shift").value(0.1).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(59, "band_height", "Band Height").value(0.16).range(-UNBOUNDED, 1).slider(0, 1),
        param::group_end(60, "fast_forward_details_end"),
        param::group(61, "dropouts_details", "Dropouts Details"),
        param::scalar(62, "dropout_length", "Dropout Length").value(0.25).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(63, "dropout_gap_length", "Dropout Gap Length").value(0.2).range(-UNBOUNDED, 2).slider(0, 2),
        param::scalar(64, "dropout_y_freq", "Dropout Y Freq").value(5).range(-UNBOUNDED, 50).slider(0, 50),
        param::scalar(65, "dropout_y_threshold", "Dropout Y Threshold").value(0.75).range(-UNBOUNDED, 1).slider(0, 1),
        param::scalar(66, "dropouts_always", "Dropouts Always").value(1).range(-UNBOUNDED, 1).slider(0, 1),
        param::group_end(67, "dropouts_details_end"),
        param::group(68, "vignette_details", "Vignette Details"),
        param::scalar(69, "vignette_radius", "Vignette Radius").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(70, "vignette_edge_softness", "Vignette Edge Softness")
            .value(0.5)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 1),
        param::scalar(71, "vignette_rel_height", "Vignette Rel Height")
            .value(0.75)
            .range(0.1, UNBOUNDED)
            .slider(0.1, 2),
        param::group_end(72, "vignette_details_end"),
        param::group(73, "scanlines_group", "Scanlines"),
        param::scalar(74, "scanlines", "Scanlines").value(0.1).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(75, "scanlines_rel_freq", "Scanlines Rel Freq")
            .value(1)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 2),
        param::group_end(76, "scanlines_group_end"),
        param::group(77, "orthicon_group", "Orthicon"),
        param::scalar(78, "orthicon", "Orthicon").value(0).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(79, "threshold", "Threshold").value(0.7).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(80, "darks_width", "Darks Width").value(0.2).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::group_end(81, "orthicon_group_end"),
        param::group(82, "color_correct", "Color Correct"),
        param::scalar(83, "hue_shift", "Hue Shift").value(0).range(-1, 1),
        param::scalar(84, "saturation", "Saturation").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(85, "scale_lights", "Scale Lights").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 2),
        param::scalar(86, "offset_darks", "Offset Darks").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-5, 1),
        param::color(87, "tint_lights", "Tint Lights").value({1, 1, 1}),
        param::color(88, "tint_darks", "Tint Darks").value({0, 0, 0}),
        param::group_end(89, "color_correct_end"),
        param::scalar(90, "turn_off", "Turn Off").value(0).range(-UNBOUNDED, 1).slider(0, 1),
        param::group(91, "turnoff_details", "TurnOff Details"),
        param::scalar(92, "flare_width", "Flare Width").value(1).range(-UNBOUNDED, UNBOUNDED).slider(0, 5),
        param::scalar(93, "flare_brightness", "Flare Brightness").value(2).range(-UNBOUNDED, UNBOUNDED).slider(0, 4),
        param::scalar(94, "fade_out_time", "Fade Out Time").value(0.7).range(-UNBOUNDED, 1).slider(0, 1),
        param::group_end(95, "turnoff_details_end"),
        param::scalar(96, "fish_eye", "Fish Eye").value(0).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(97, "tv_pixels", "Tv Pixels").value(720).range(1, UNBOUNDED).slider(1, 1440),
        param::scalar(98, "seed", "Seed").value(0.123).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::choice(99, "mask_use", "Mask Use", "Luma|Alpha"),
        param::scalar(100, "blur_mask", "Blur Mask").value(0.05).range(0, 1000).slider(0, 2),
        param::toggle(101, "invert_mask", "Invert Mask"),
    };

    static constexpr double TAU = 2.0 * std::numbers::pi;

    // floored, so the result takes the sign of m
    static double floored_mod(double a, double m)
    {
        return m != 0.0 ? a - std::floor(a / m) * m : 0.0;
    }

    // dropout arguments; lengths in xy units, scale in lines per xy unit
    struct dropout_spec
    {
        double amount = 0;
        double scale = 1;
        double line_rel_height = 1;
        double gap_min = 0;
        double gap_max = 0;
        double len_min = 0;
        double len_max = 0;
        double y_freq = 0; // 0 takes every line
        double y_threshold = 0;
        int y_octaves = 1;
        double y_lo = -1;
        double y_hi = 1;
        std::uint32_t dash_seed = 0;
        std::uint32_t line_seed = 0;
    };

    using dashes = std::vector<std::pair<double, double>>;

    // dropouts: on scan lines picked by a noise along y,
    // dashes of random length separated by random gaps run from the left edge; returns this row's dashes in x
    static dashes dropout_row(const dropout_spec& s, double y)
    {
        dashes out;
        if (s.amount == 0.0 || y < s.y_lo || y > s.y_hi || s.scale <= 0.0)
        {
            return out;
        }
        const double ly = y * s.scale;
        const double line = std::floor(ly);
        if (ly - line >= s.line_rel_height)
        {
            return out;
        }
        if (s.y_freq > 0.0 &&
            core::noise::fbm_2d(s.line_seed, 0.0, line / s.scale * s.y_freq, std::max(1, s.y_octaves)) <= s.y_threshold)
        {
            return out;
        }
        constexpr double MIN_STEP = 0.002;
        constexpr int MAX_DASHES = 2048;
        const std::uint32_t h = core::noise::combine(s.dash_seed, core::noise::cell(line));
        double pos = -1.0 - core::noise::unit(h) * s.gap_max;
        for (int i = 0; i < MAX_DASHES && pos < 1.0; ++i)
        {
            const std::uint32_t hi = core::noise::combine(h, static_cast<std::uint32_t>(i));
            const double gap = s.gap_min + (s.gap_max - s.gap_min) * core::noise::unit(hi);
            const double len = s.len_min + (s.len_max - s.len_min) * core::noise::unit(core::noise::hash(hi));
            const double start = pos + std::max(gap, 0.0);
            const double end = start + std::max(len, 0.0);
            if (end > start)
            {
                out.emplace_back(start, end);
            }
            pos = std::max(end, pos + MIN_STEP);
        }
        return out;
    }

    static double in_dash(const dashes& d, double x)
    {
        return std::ranges::any_of(d, [x](const auto& r) { return x >= r.first && x < r.second; }) ? 1.0 : 0.0;
    }

    struct ghost
    {
        double offset_px = 0;
        float amp = 0;
    };

    // ghosts: Num Ghosts echoes Spacing apart (xy units,
    // jittered by Vary Position), placed right of the image for Shift 1 and left for -1, fading with distance, each
    // one negative with chance Negative Ghosts
    static std::vector<ghost> ghost_kernel(const core::param_values& v, double pos_amp, const core::frame& f)
    {
        std::vector<ghost> out;
        const int n = std::max(0, v.whole(p::num_ghosts));
        const std::uint32_t seed = core::noise::seed_bits(v.scalar(p::seed) + 1.0);
        const double spacing = v.scalar(p::spacing);
        for (int k = 1; k <= n; ++k)
        {
            const std::uint32_t h = core::noise::combine(seed, static_cast<std::uint32_t>(k));
            const double pos = spacing * (k + (v.scalar(p::shift) - 1.0) * (n + 1) * 0.5 +
                                          v.scalar(p::vary_position) * 0.5 * core::noise::signed_unit(h));
            const bool negative = core::noise::unit(core::noise::hash(h)) < v.scalar(p::negative_ghosts);
            const double fade = 1.0 - static_cast<double>(k - 1) / n;
            out.push_back(
                {f.to_px(pos / core::XY_PER_FRAME_UNIT), static_cast<float>((negative ? -0.5 : 1.0) * pos_amp * fade)});
        }
        return out;
    }

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const core::frame& f = args.geometry;
        const core::image& src = args.src;
        const bool mono = v.choice(p::style) == 1;
        const double t = args.time;
        const auto frame_number = static_cast<double>(args.frame_number());
        const double seed = v.scalar(p::seed);
        const double reception = v.scalar(p::reception_master);
        const double aspect = src.bounds.height() / f.width;
        const double tv_pixels = v.scalar(p::tv_pixels);
        const double pixel_frequency = tv_pixels * 50.0 / 720.0;
        const std::uint32_t frame_seed = core::noise::seed_bits(seed + frame_number);
        const auto to_xx = [&](double px) { return f.to_nx(px) * core::XY_PER_FRAME_UNIT; };
        const auto to_yy = [&](double py) { return f.to_ny(py) * core::XY_PER_FRAME_UNIT; };

        // d0: the source (monochrome for Mono) minus the orthicon glow, a ring around the hot spots
        core::buffer d0 = core::edges::straight_rgb(src);
        if (const double b = 4.0 * v.scalar(p::orthicon); b != 0.0)
        {
            const core::buffer hot = core::buffer::copy_of(d0.view());
            const double th = v.scalar(p::threshold);
            core::glow::hotspots(hot.view(), {th, th, th});
            const core::buffer wide = core::buffer::copy_of(hot.view());
            const double darks = core::blur::amount_px(v.scalar(p::darks_width), f);
            core::blur::gaussian(wide.view(), darks, darks);
            constexpr double PROTECT_WIDTH = 0.1;
            const double protect = core::blur::amount_px(PROTECT_WIDTH, f);
            core::blur::gaussian(hot.view(), protect, protect);
            const core::image& o = d0.view();
            for (int y = o.bounds.y0; y < o.bounds.y1; ++y)
            {
                for (int x = o.bounds.x0; x < o.bounds.x1; ++x)
                {
                    float* d = o.at(x, y);
                    const float l = core::color::luma(d[0], d[1], d[2]);
                    for (int c = 0; c < 3; ++c)
                    {
                        const auto glow =
                            static_cast<float>(std::max(0.0, b * wide.view().at(x, y)[c] - b * hot.view().at(x, y)[c]));
                        d[c] = (mono ? l : d[c]) - glow;
                    }
                }
            }
        }
        else if (mono)
        {
            const core::image& o = d0.view();
            for (int y = o.bounds.y0; y < o.bounds.y1; ++y)
            {
                for (int x = o.bounds.x0; x < o.bounds.x1; ++x)
                {
                    float* d = o.at(x, y);
                    std::fill_n(d, 3, core::color::luma(d[0], d[1], d[2]));
                }
            }
        }

        // d1: ghosts, horizontal echoes (blurred by Blur) added and the sum renormalized
        const double ghosting = v.scalar(p::ghost_amp) * reception;
        const std::vector<ghost> ghosts = ghost_kernel(v, ghosting, f);
        core::buffer d1(src.bounds, 3);
        {
            core::buffer echoes(src.bounds, 3);
            const core::image& e = echoes.view();
            const core::image& s = d0.view();
            core::parallel::for_pixels(src.bounds, [&](int x, int y) {
                float* d = e.at(x, y);
                for (const ghost& g : ghosts)
                {
                    const core::sample::pixel q =
                        core::sample::bilinear(s, x + 0.5 - g.offset_px, y + 0.5, core::sample::wrap::none);
                    for (int c = 0; c < 3; ++c)
                    {
                        d[c] += g.amp * q[c];
                    }
                }
            });
            const double blur = core::blur::amount_px(v.scalar(p::blur) * 0.1, f);
            core::blur::gaussian(e, blur, 0.0);
            double total = 1.0;
            std::ranges::for_each(ghosts, [&](const ghost& g) { total += g.amp; });
            const auto norm = static_cast<float>(1.0 / std::max(0.05, total));
            const core::image& o = d1.view();
            for (int y = o.bounds.y0; y < o.bounds.y1; ++y)
            {
                for (int x = o.bounds.x0; x < o.bounds.x1; ++x)
                {
                    for (int c = 0; c < 3; ++c)
                    {
                        o.at(x, y)[c] = (s.at(x, y)[c] + e.at(x, y)[c]) * norm;
                    }
                }
            }
        }

        // horizontal hold: each row slides by a noise along y; its strength wanders over time
        const double h_hold = std::max(0.0, v.scalar(p::horizontal_hold) * reception - 0.1);
        const double h_vary = v.scalar(p::h_time_vary);
        const double h_amount = std::max(
            0.0, h_hold * (1.0 - h_vary) +
                     2.0 * h_hold * h_vary * core::noise::fbm_2d(core::noise::seed_bits(seed + 5.1), t * 10.0, 0.0, 1));
        const double h_freq = v.scalar(p::h_frequency);
        const int h_octaves = v.whole(p::h_octaves);
        const std::uint32_t h_seed = core::noise::seed_bits(seed + 5.0);
        const double h_roll = 0.2 * t;

        // vertical hold: the picture rolls by whole frames part of the time
        const double v_hold = v.scalar(p::vertical_hold) * reception;
        const double v_wrap = (1.0 + v.scalar(p::border_height)) * 2.0 * aspect;
        double hold_y_shift = 0.0;
        if (v_hold > 0.0)
        {
            const double freq = v.scalar(p::v_frequency) / 4.0;
            hold_y_shift = v.scalar(p::v_speed) * 2.0 * aspect *
                           core::damage::sawtooth_plus_noise(seed + 6.789, t, 6, 0.0, 1.0, v.scalar(p::v_random), 1.0,
                                                             1.2 / (v_hold + 0.2) - 1.0, freq, 2.0 * freq)
                               .value;
        }
        const double ff_amount = v.scalar(p::fast_forward_amount);
        const double band_frequency = v.scalar(p::band_frequency);
        const double band_height_rel = v.scalar(p::band_height) / band_frequency * 1.5;
        const double h_wrap = 2.0 + v.scalar(p::border_width);
        const bool warped = h_amount + hold_y_shift + ff_amount != 0.0;

        // dropouts: data dashes in the rolling frame border, dashes in the fast forward bands, tape dropouts
        const double dropout_scale = tv_pixels * 480.0 / 720.0;
        const double v_seed = seed + 6.789;
        // placed in rolled coordinates, two lines inside the frame border the roll brings into view
        const double border_top = aspect + 2.0 / dropout_scale;
        const dropout_spec border{.amount = hold_y_shift != 0.0 ? v.scalar(p::border_data) : 0.0,
                                  .scale = dropout_scale,
                                  .line_rel_height = 0.9,
                                  .gap_min = 0.05,
                                  .gap_max = 0.5,
                                  .len_min = 0.0,
                                  .len_max = 0.05,
                                  .y_lo = border_top,
                                  .y_hi = border_top + 2.0 * aspect * v.scalar(p::border_height) - 4.0 / dropout_scale,
                                  .dash_seed = core::noise::seed_bits(v_seed + t),
                                  .line_seed = core::noise::seed_bits(v_seed)};
        const double ff_seed = seed + 4.0 + t;
        const dropout_spec ff_dropouts{.amount = 1.0,
                                       .scale = dropout_scale,
                                       .line_rel_height = 0.7,
                                       .gap_min = 0.1,
                                       .gap_max = 0.2,
                                       .len_min = 0.0,
                                       .len_max = 0.15,
                                       .dash_seed = core::noise::seed_bits(ff_seed + t),
                                       .line_seed = core::noise::seed_bits(ff_seed)};
        const double dropout_seed = seed + frame_number + 4.0;
        // Mono compares with Dropouts Always itself where Color uses 1 - Dropouts Always
        const double always = v.scalar(p::dropouts_always);
        const bool dropouts_this_frame = core::noise::fbm_2d(core::noise::seed_bits(dropout_seed), 0.1 * 5.0, t * 5.0,
                                                             3) > (mono ? always : 1.0 - always);
        const dropout_spec tape{.amount = dropouts_this_frame ? v.scalar(p::tape_dropout_brightness) : 0.0,
                                .scale = dropout_scale,
                                .line_rel_height = 0.9,
                                .gap_min = v.scalar(p::dropout_gap_length),
                                .gap_max = 1.0,
                                .len_min = 0.0,
                                .len_max = v.scalar(p::dropout_length),
                                .y_freq = v.scalar(p::dropout_y_freq),
                                .y_threshold = v.scalar(p::dropout_y_threshold),
                                .y_octaves = 4,
                                .dash_seed = core::noise::seed_bits(dropout_seed + t),
                                .line_seed = core::noise::seed_bits(dropout_seed)};

        // interference: a fine dot grid carrying a carrier at Frequency cycles per dot (rows skewed by half a dot),
        // scrolling at Dots Speed
        const double interference = v.scalar(p::interference_amp) * reception;
        const double ndots_x = 5.0 * pixel_frequency * 0.8;
        const double ndots_y = 5.0 * pixel_frequency;
        const double dots_shift_x = v.scalar(p::dots_speed_x) * t;
        const double dots_shift_y = v.scalar(p::dots_speed_y) * t;
        // the phase multiplies the carrier by hundreds of dots, so it is snapped to a grid finer than the UI step;
        // otherwise After Effects' float storage of the value would shift the pattern between hosts
        constexpr double CARRIER_GRID = 1e5;
        const double carrier = std::round(v.scalar(p::frequency) * CARRIER_GRID) / CARRIER_GRID;
        constexpr double JITTER_DOTS = 0.05;
        const double jitter = v.scalar(p::jitter_amount) * JITTER_DOTS;
        const std::uint32_t int_seed =
            core::noise::combine(core::noise::seed_bits(seed + 2.0), static_cast<std::uint32_t>(frame_number));

        // static: thresholded noise at 4 x the pixel frequency, new every frame; our value noise spreads about three
        // times wider than a gradient noise, so it is narrowed
        constexpr double STATIC_SPREAD = 0.35;
        const double static_amp = v.scalar(p::static_amplitude) * reception;
        const double density = v.scalar(p::static_density);
        const double static_threshold = 1.0 - std::sqrt(density);
        const double static_freq = pixel_frequency * 4.0;
        const double static_seed = seed + t + 3.0;

        const double stripes = v.scalar(p::color_stripes_amplitude) * reception;
        const double color_turns = -v.scalar(p::color_angle) / 360.0;
        const double color_sin = std::sin(TAU * color_turns);
        const double color_cos = std::cos(TAU * color_turns);
        const double color_frequency = v.scalar(p::color_frequency);
        const double roll = v.scalar(p::roll_speed) * t;

        const auto hue = v.scalar_f(p::hue_shift);
        const auto saturation = v.scalar_f(p::saturation);
        std::array<float, 3> lights{};
        std::array<float, 3> darks{};
        for (int c = 0; c < 3; ++c)
        {
            lights[c] = static_cast<float>(v.vec(p::tint_lights)[c] * v.scalar(p::scale_lights));
            darks[c] = static_cast<float>(v.vec(p::tint_darks)[c] + v.scalar(p::offset_darks));
        }

        // bars: two clipped sine bands rolling up the frame, their product added
        const double bars = v.scalar(p::bars_brightness) * reception;
        const double edge1 = 1.0 / (v.scalar(p::bar_sharpness) * 2.0);
        const double edge2 = 1.0 / (v.scalar(p::bar2_sharpness) * 2.0);
        const double center1 = (1.0 - v.scalar(p::bar1_width) * 2.0) * (1.0 + edge1 * 1.5);
        const double bar_frequency = v.scalar(p::bar_frequency);
        const double bar2_frequency = bar_frequency * v.scalar(p::bar2_rel_frequency);
        const double bar_offset = v.scalar(p::bar_roll_speed) * t + 0.2;

        const double scanlines = v.scalar(p::scanlines);
        const double scan_freq = v.scalar(p::scanlines_rel_freq) * pixel_frequency;
        const double vignette = v.scalar(p::vignette_darkness);

        // turn off: the picture becomes a white rectangle collapsing to the centre, with a flare, fading out
        const double turn_off = v.scalar(p::turn_off);
        constexpr double WARP_ZDIST = 30.0;
        const double ws1 = std::clamp(turn_off / 0.2, 0.0, 1.0);
        const double ws2 = std::clamp((turn_off - 0.2) / 0.2, 0.0, 1.0);
        const double zoom_from = 1.0 + (WARP_ZDIST - 1.0) * ws2 * ws2;
        const double zoom_to = 1.0 + (WARP_ZDIST - 1.0) * ws1 * ws1;
        const double flare_scale = std::clamp((turn_off - 0.1) / 0.2, 0.0, 1.0) * v.scalar(p::flare_width);
        const double flare_brightness = v.scalar(p::flare_brightness);
        const double fade_time = v.scalar(p::fade_out_time);
        const auto fade =
            static_cast<float>(fade_time > 0.0 ? std::clamp(1.0 - (turn_off - (1.0 - fade_time)) / fade_time, 0.0, 1.0)
                                               : (turn_off == 1.0 ? 0.0 : 1.0));

        core::buffer d12(src.bounds, 3);
        const core::image& out = d12.view();
        const core::image& d1v = d1.view();
        core::parallel::for_range(src.bounds.y0, src.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                const double yy = to_yy(y + 0.5);
                const double row_shift =
                    h_amount * (core::noise::fbm_2d(h_seed, t * h_freq * 0.2, (yy + h_roll) * h_freq, h_octaves) - 0.5);
                const double ymod = floored_mod(yy + v.scalar(p::band_shift), 2.0 * aspect / band_frequency);
                const double yband = ymod > band_height_rel ? 0.0 : ymod / band_height_rel;
                constexpr double CURVE = 0.3;
                const double ff_mask = ff_amount * (yband + (yband * yband - yband) * CURVE);
                const double v_wrapped = floored_mod(aspect + yy + hold_y_shift, v_wrap) - aspect;
                const dashes border_dashes = dropout_row(border, v_wrapped);
                const dashes ff_dashes = ff_mask != 0.0 ? dropout_row(ff_dropouts, yy) : dashes{};
                const dashes tape_dashes = dropout_row(tape, yy);

                const double bar_y = yy - bar_offset;
                const double s1 = std::clamp(std::sin(TAU * bar_frequency * bar_y), center1 - edge1, center1 + edge1);
                const double s2 = std::clamp(std::sin(TAU * bar2_frequency * bar_y), -edge2, edge2);
                const double bar =
                    (s1 - (center1 - edge1)) / (2.0 * edge1) * ((s2 + edge2) / (2.0 * edge2)) * (bars * 4.0 / 3.0) -
                    bars / 3.0;
                const double lines = core::color::nsin(yy * scan_freq) * scanlines - (scanlines - 1.0) * 0.5;
                const std::int64_t row = std::llround(std::floor(yy * ndots_y + dots_shift_y));

                for (int x = src.bounds.x0; x < src.bounds.x1; ++x)
                {
                    const double xx = to_xx(x + 0.5);
                    std::array<double, 3> c{};
                    if (warped)
                    {
                        const double u = floored_mod(xx + row_shift + 0.2 * ff_mask + 1.0, h_wrap) - 1.0;
                        const core::sample::pixel q = core::sample::bilinear(
                            d1v, f.to_px_x(u / core::XY_PER_FRAME_UNIT), f.to_px_y(v_wrapped / core::XY_PER_FRAME_UNIT),
                            core::sample::wrap::none);
                        std::copy_n(q.begin(), 3, c.begin());
                    }
                    else
                    {
                        std::copy_n(d1v.at(x, y), 3, c.begin());
                    }
                    double add = border.amount * in_dash(border_dashes, xx) + ff_mask * in_dash(ff_dashes, xx) +
                                 tape.amount * in_dash(tape_dashes, xx);

                    if (interference != 0.0)
                    {
                        const double jit =
                            jitter *
                            core::noise::signed_unit(core::noise::combine(int_seed, static_cast<std::uint32_t>(row)));
                        const double uu = xx * ndots_x + dots_shift_x + jit;
                        const double vv = yy * ndots_y + dots_shift_y;
                        const double dot = core::color::nsin(uu - std::floor(uu) - 0.25) *
                                           core::color::nsin(vv - std::floor(vv) - 0.25);
                        const double phase = carrier * (std::floor(uu) + 0.5 * static_cast<double>(row));
                        add += interference * std::sin(TAU * (phase - std::floor(phase))) * dot;
                    }
                    for (int ch = 0; ch < 3; ++ch)
                    {
                        c[ch] += add;
                    }
                    if (static_amp != 0.0)
                    {
                        for (int ch = 0; ch < 3; ++ch)
                        {
                            const double n =
                                2.0 * STATIC_SPREAD *
                                (core::noise::value_2d(core::noise::seed_bits(static_seed + (mono ? 0 : 0.01 * ch)),
                                                       xx * static_freq, yy * static_freq) -
                                 0.5);
                            const double s0 = n < static_threshold && n > -static_threshold ? 0.0 : n;
                            c[ch] += std::clamp(s0 * static_amp / density, -1.0, 1.0);
                        }
                    }

                    // color stripes, scan lines: dissolve(darks, lights, lines) on 2x the color
                    const double ry = xx * color_sin + yy * color_cos;
                    core::color::rgb rgb{};
                    for (int ch = 0; ch < 3; ++ch)
                    {
                        const double shift = roll + (mono ? 0.0 : ch / 3.0);
                        const double cl =
                            core::color::nsin(ry * color_frequency - shift) * stripes - (stripes - 1.0) * 0.5;
                        const double i2 = c[ch] * 2.0;
                        rgb[ch] = core::color::dissolve(static_cast<float>(std::max(0.0, i2 - 1.0)),
                                                        static_cast<float>(std::min(1.0, i2)), static_cast<float>(cl));
                    }
                    if (hue != 0.0f)
                    {
                        rgb = core::color::hue_shift_preserve_luma(rgb, hue);
                    }
                    rgb = core::color::sat_scale(rgb, saturation);
                    const double keep =
                        vignette != 0.0
                            ? 1.0 + (core::damage::vignette_keep(xx, yy, v.scalar(p::vignette_radius),
                                                                 v.scalar(p::vignette_rel_height),
                                                                 v.scalar(p::vignette_edge_softness),
                                                                 core::damage::pixel_static(frame_seed, f, x, y)) -
                                     1.0) *
                                        vignette
                            : 1.0;
                    for (int ch = 0; ch < 3; ++ch)
                    {
                        const double i2 =
                            2.0 * (core::color::scale_lights_offset_darks(rgb[ch], lights[ch], darks[ch]) + bar);
                        c[ch] =
                            core::color::dissolve(static_cast<float>(std::max(0.0, i2 - 1.0)),
                                                  static_cast<float>(std::min(1.0, i2)), static_cast<float>(lines)) *
                            keep;
                    }

                    if (turn_off > 0.0)
                    {
                        // blur warp of a white frame from xy * zoom_from to xy * zoom_to: the share of that
                        // path still inside the frame
                        const double kmax =
                            std::min(xx != 0.0 ? 1.0 / std::abs(xx) : 1e30, yy != 0.0 ? aspect / std::abs(yy) : 1e30);
                        const double white = zoom_to > zoom_from
                                                 ? std::clamp((kmax - zoom_from) / (zoom_to - zoom_from), 0.0, 1.0)
                                                 : (kmax >= zoom_from ? 1.0 : 0.0);
                        // turn off: a soft ellipse with a horizontal streak
                        double flare = 0.0;
                        if (flare_scale > 0.0)
                        {
                            const double fx = xx / flare_scale;
                            const double fy = yy / (0.4 * flare_scale);
                            constexpr double STREAK_THICKNESS = 0.05 * 2.0;
                            flare =
                                flare_brightness * (std::exp(-4.0 * (fx * fx + fy * fy)) +
                                                    0.5 * std::exp(-(fy * fy) / (STREAK_THICKNESS * STREAK_THICKNESS)) *
                                                        std::max(0.0, 1.0 - std::abs(fx)));
                        }
                        const auto w = static_cast<float>(white);
                        const auto fl = static_cast<float>(flare);
                        c.fill(fade * core::color::screen_super(w, fl));
                    }
                    float* d = out.at(x, y);
                    std::ranges::transform(c, d, [](double value) { return static_cast<float>(value); });
                }
            }
        });

        const double fish_eye = v.scalar(p::fish_eye);
        const core::image& dst = args.dst;
        core::parallel::for_pixels(dst.bounds, [&](int x, int y) {
            float* d = dst.at(x, y);
            if (!src.bounds.contains(x, y))
            {
                std::fill_n(d, 4, 0.0f);
                return;
            }
            const float a = src.at(x, y)[3];
            std::array<float, 3> c{};
            if (fish_eye != 0.0)
            {
                const double xx = to_xx(x + 0.5);
                const double yy = to_yy(y + 0.5);
                const double m = xx * xx + yy * yy;
                const double k = (1.0 + m * fish_eye) / (1.0 + std::max(0.0, fish_eye));
                const core::sample::pixel q =
                    core::sample::bilinear(out, f.to_px_x(xx * k / core::XY_PER_FRAME_UNIT),
                                           f.to_px_y(yy * k / core::XY_PER_FRAME_UNIT), core::sample::wrap::none);
                std::copy_n(q.begin(), 3, c.begin());
            }
            else
            {
                std::copy_n(out.at(x, y), 3, c.begin());
            }
            std::ranges::transform(c, d, [a](float value) { return value * a; });
            d[3] = a;
        });

        core::mask::apply(args, core::mask::luma_or_alpha(v.choice(p::mask_use)), v.scalar(p::blur_mask),
                          v.toggle(p::invert_mask));
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_tvdamage",
            .name = "f_tvdamage",
            .category = "Functional Stylize",
            .description =
                "Simulates a bad analog TV picture: ghosts, rolling and tearing holds, interference, static, "
                "dropouts, bars, color stripes, scan lines and the tube turning off.",
            .params = PARAMS,
            .mask_input = "Mask",
            .background_input = "",
            .time_varying = true,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::tvdamage
