#include "effects/lensdirt.hpp"
#include "core/blur.hpp"
#include "core/color.hpp"
#include "core/glow.hpp"
#include "core/mip.hpp"
#include "core/noise.hpp"
#include "core/parallel.hpp"
#include "core/sample.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace functional_fx_suite::effects::lensdirt
{
    enum class p : std::uint8_t
    {
        threshold,
        combine,
        flare,
        flare_color,
        flare_intensity,
        pivot_angle,
        flare_blur,
        haze,
        flare_end,
        dirt,
        intensity,
        spread,
        smudges,
        smudge_size,
        specks,
        speck_size,
        seed,
        dirt_end,
        show,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    // sizes are in frame widths
    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "threshold", "Threshold").value(0.6).range(0, UNBOUNDED).slider(0, 1),
        param::choice(4, "combine", "Combine", "Screen|Add|Dirt Only"),
        param::group(13, "flare", "Flare"),
        param::color(20, "flare_color", "Color").value({1, 1, 1}),
        param::scalar(14, "flare_intensity", "Intensity").value(1).range(0, UNBOUNDED).slider(0, 4),
        param::scalar(15, "pivot_angle", "Pivot Angle").value(90).range(0, 180),
        param::scalar(16, "flare_blur", "Blur").value(0.1).range(0, 1000).slider(0, 2),
        param::scalar(17, "haze", "Haze").value(0.5).range(0, 1),
        param::group_end(18, "flare_end"),
        param::group(5, "dirt", "Built-in Dirt"),
        param::scalar(2, "intensity", "Intensity").value(1.5).range(0, UNBOUNDED).slider(0, 4),
        param::scalar(3, "spread", "Spread").value(0.6).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(6, "smudges", "Smudges").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(7, "smudge_size", "Smudge Size").value(0.12).range(0.001, UNBOUNDED).slider(0.01, 0.5),
        param::scalar(8, "specks", "Specks").value(1).range(0, UNBOUNDED).slider(0, 2),
        param::scalar(9, "speck_size", "Speck Size").value(0.006).range(0.0005, UNBOUNDED).slider(0.001, 0.03),
        param::scalar(10, "seed", "Seed").value(0.2).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::group_end(11, "dirt_end"),
        param::choice(12, "show", "Show", "Result|Light|Dirt"),
    };

    // specks sit one per cell at most, in cells this many speck sizes wide
    static constexpr double SPECK_CELLS = 6.0;

    struct built_in
    {
        std::uint32_t seed = 0;
        double smudges = 0;
        double smudge_size = 1;
        double specks = 0;
        double speck_size = 1;

        // dirt at (nx, ny) in frame units, 0..1
        [[nodiscard]] float at(double nx, double ny) const
        {
            constexpr int OCTAVES = 4;
            const double n = core::noise::fbm_2d(seed, nx / smudge_size, ny / smudge_size, OCTAVES);
            // only the noise's peaks become smudges, faded in over a soft edge
            constexpr double LOW = 0.5;
            constexpr double HIGH = 0.75;
            const double smudge = std::clamp((n - LOW) / (HIGH - LOW), 0.0, 1.0);
            double speck = 0.0;
            const double cell = speck_size * SPECK_CELLS;
            const double cx = std::floor(nx / cell);
            const double cy = std::floor(ny / cell);
            for (int j = -1; j <= 1; ++j)
            {
                for (int i = -1; i <= 1; ++i)
                {
                    const std::uint32_t h = core::noise::combine(
                        core::noise::combine(seed + 1, core::noise::cell(cy + j)), core::noise::cell(cx + i));
                    constexpr double PRESENT = 0.5;
                    if (core::noise::unit(h) > PRESENT)
                    {
                        continue;
                    }
                    const double px = (cx + i + core::noise::unit(core::noise::hash(h + 1))) * cell;
                    const double py = (cy + j + core::noise::unit(core::noise::hash(h + 2))) * cell;
                    const double r = speck_size * (0.3 + 0.7 * core::noise::unit(core::noise::hash(h + 3)));
                    const double edge = std::clamp((r - std::hypot(nx - px, ny - py)) / (0.5 * r), 0.0, 1.0);
                    speck = std::max(speck, edge * (0.3 + 0.7 * core::noise::unit(core::noise::hash(h + 4))));
                }
            }
            return static_cast<float>(std::min(1.0, smudges * smudge + specks * speck));
        }
    };

    // the flare at 1/scale resolution over the light's bounds, rgb in 4 channels
    struct flare_map
    {
        core::buffer low;
        core::rect bounds;
        int scale = 1;

        [[nodiscard]] core::sample::pixel at(int x, int y) const
        {
            return core::sample::bilinear(low.view(), (x + 0.5 - bounds.x0) / scale, (y + 0.5 - bounds.y0) / scale,
                                          core::sample::wrap::reflect);
        }
    };

    // works at about 480 pixels across: the flare is too soft to need more
    static constexpr double FLARE_WIDTH = 480.0;
    // angle steps of the polar resampling the spin is a box blur in
    static constexpr int ANGLES = 720;

    static flare_map flare_of(const core::image& light, const core::frame& f, double pivot_deg, double blur_px)
    {
        flare_map out;
        out.bounds = light.bounds;
        out.scale = std::max(1, static_cast<int>(std::lround(f.width / FLARE_WIDTH)));
        const int k = out.scale;
        const core::rect& b = light.bounds;
        const int lw = (b.width() + k - 1) / k;
        const int lh = (b.height() + k - 1) / k;
        if (lw <= 0 || lh <= 0)
        {
            return out;
        }
        const core::buffer down = core::box_mean(light, k);
        const core::image& dn = down.view();

        // the spin is a box blur along each ring of a polar resampling about the frame centre
        const double cx = (f.cx - b.x0) / k;
        const double cy = (f.cy - b.y0) / k;
        const double rmax = std::max(std::hypot(std::max(cx, lw - cx), std::max(cy, lh - cy)), 1.0);
        const int rings = static_cast<int>(std::ceil(rmax)) + 2;
        const auto ring_size = static_cast<std::size_t>(ANGLES);
        std::vector<std::array<float, 3>> polar(static_cast<std::size_t>(rings) * ring_size);
        const int half = std::min(ANGLES / 2 - 1, static_cast<int>(std::lround(pivot_deg / 360.0 * ANGLES)));
        core::parallel::for_range(0, rings, [&](int begin, int end) {
            std::vector<std::array<float, 3>> ring(ring_size);
            for (int r = begin; r < end; ++r)
            {
                for (std::size_t a = 0; a < ring_size; ++a)
                {
                    const double t = 2.0 * std::numbers::pi * static_cast<double>(a) / ANGLES;
                    const core::sample::pixel px = core::sample::bilinear(
                        dn, cx + r * std::cos(t), cy + r * std::sin(t), core::sample::wrap::none);
                    ring[a] = {px[0], px[1], px[2]};
                }
                // a running sum over the 2 * half + 1 angles around each one, wrapping round the ring
                std::array<double, 3> run{};
                for (int a = -half; a <= half; ++a)
                {
                    const auto& v = ring[static_cast<std::size_t>((a + ANGLES) % ANGLES)];
                    for (std::size_t c = 0; c < 3; ++c)
                    {
                        run[c] += v[c];
                    }
                }
                const double inv = 1.0 / (2 * half + 1);
                for (int a = 0; a < ANGLES; ++a)
                {
                    auto& o = polar[(static_cast<std::size_t>(r) * ring_size) + static_cast<std::size_t>(a)];
                    for (std::size_t c = 0; c < 3; ++c)
                    {
                        o[c] = static_cast<float>(run[c] * inv);
                    }
                    const auto& in = ring[static_cast<std::size_t>((a + half + 1) % ANGLES)];
                    const auto& gone = ring[static_cast<std::size_t>((a - half + ANGLES) % ANGLES)];
                    for (std::size_t c = 0; c < 3; ++c)
                    {
                        run[c] += in[c] - gone[c];
                    }
                }
            }
        });

        // back to pixels, half a turn round: the mirror through the centre
        out.low = core::buffer(core::rect{0, 0, lw, lh}, 4);
        const core::image& lo = out.low.view();
        core::parallel::for_pixels(lo.bounds, [&](int i, int j) {
            const double dx = i + 0.5 - cx;
            const double dy = j + 0.5 - cy;
            const double r = std::min(std::hypot(dx, dy), rings - 1.001);
            double a = (std::atan2(dy, dx) + std::numbers::pi) / (2.0 * std::numbers::pi) * ANGLES;
            a -= std::floor(a / ANGLES) * ANGLES;
            const int r0 = static_cast<int>(r);
            const int a0 = static_cast<int>(a) % ANGLES;
            const int a1 = (a0 + 1) % ANGLES;
            const auto tr = static_cast<float>(r - r0);
            const auto ta = static_cast<float>(a - std::floor(a));
            const auto pv = [&](int rr, int aa) -> const std::array<float, 3>& {
                return polar[(static_cast<std::size_t>(rr) * ring_size) + static_cast<std::size_t>(aa)];
            };
            float* o = lo.at(i, j);
            for (std::size_t c = 0; c < 3; ++c)
            {
                const float near = pv(r0, a0)[c] + (pv(r0, a1)[c] - pv(r0, a0)[c]) * ta;
                const float far = pv(r0 + 1, a0)[c] + (pv(r0 + 1, a1)[c] - pv(r0 + 1, a0)[c]) * ta;
                o[c] = near + (far - near) * tr;
            }
            o[3] = 1.0f;
        });
        const double soft = blur_px / k;
        core::blur::gaussian(lo, soft, soft, core::blur::edge::reflect);
        return out;
    }

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const core::frame& f = args.geometry;
        const core::rect& sb = args.src.bounds;

        // the light layer stands in for the source as the light
        core::render_args lit = args;
        if (args.mask != nullptr)
        {
            lit.src = *args.mask;
        }
        const core::buffer light = core::glow::source(lit, nullptr, 0.0);
        const core::image& l = light.view();
        const double t = v.scalar(p::threshold);
        core::glow::threshold(l, {t, t, t});
        const float flare_intensity = v.scalar_f(p::flare_intensity);
        const auto& flare_tint = v.vec(p::flare_color);
        const std::array<float, 3> flare_color{static_cast<float>(flare_tint[0]) * flare_intensity,
                                               static_cast<float>(flare_tint[1]) * flare_intensity,
                                               static_cast<float>(flare_tint[2]) * flare_intensity};
        const flare_map flare = flare_intensity > 0.0f ? flare_of(l, f, v.scalar(p::pivot_angle),
                                                                  core::blur::amount_px(v.scalar(p::flare_blur), f))
                                                       : flare_map{};
        const float haze = v.scalar_f(p::haze);
        const double spread = core::blur::amount_px(v.scalar(p::spread), f);
        core::blur::gaussian(l, spread, spread, core::blur::edge::reflect);

        const built_in dirt{
            .seed = core::noise::seed_bits(v.scalar(p::seed)),
            .smudges = v.scalar(p::smudges),
            .smudge_size = v.scalar(p::smudge_size),
            .specks = v.scalar(p::specks),
            .speck_size = v.scalar(p::speck_size),
        };
        const float intensity = v.scalar_f(p::intensity);
        const int combine = v.choice(p::combine);
        const int show = v.choice(p::show);
        const core::image& dst = args.dst;
        core::parallel::for_pixels(dst.bounds, [&](int x, int y) {
            std::array<float, 3> d{};
            if (args.background != nullptr)
            {
                if (args.background->bounds.contains(x, y))
                {
                    const float* b = args.background->at(x, y);
                    d = {std::max(b[0], 0.0f), std::max(b[1], 0.0f), std::max(b[2], 0.0f)};
                }
            }
            else
            {
                d.fill(dirt.at(f.to_nx(x + 0.5), f.to_ny(y + 0.5)));
            }
            const float* li = l.bounds.contains(x, y) ? l.at(x, y) : nullptr;
            const core::sample::pixel fl = flare.low.view_if_set() != nullptr ? flare.at(x, y) : core::sample::pixel{};
            std::array<float, 4> fr{};
            for (std::size_t c = 0; c < 3; ++c)
            {
                const float lc = li != nullptr ? li[c] : 0.0f;
                const float fc = std::max(fl[c], 0.0f) * flare_color[c];
                const float lit = (d[c] * lc * intensity) + (fc * (haze + ((1.0f - haze) * d[c])));
                fr[c] = show == 1 ? (lc * intensity) + fc : show == 2 ? d[c] : lit;
            }
            fr[3] = show != 0 ? 1.0f : std::min(1.0f, std::max({fr[0], fr[1], fr[2]}));
            std::array<float, 4> bg{};
            if (sb.contains(x, y))
            {
                std::copy_n(args.src.at(x, y), 4, bg.begin());
            }
            float* o = dst.at(x, y);
            for (std::size_t c = 0; c < 4; ++c)
            {
                o[c] = show != 0 || combine == 2 ? fr[c]
                       : combine == 1            ? bg[c] + fr[c]
                                                 : core::color::screen_super(bg[c], fr[c]);
            }
        });
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_lensdirt",
            .name = "f_lensdirt",
            .category = "Functional Lighting",
            .description = "Lights up dust and smudges on the lens wherever bright light falls near them.",
            .params = PARAMS,
            .mask_input = "Light Layer",
            .background_input = "Dirt Layer",
            .time_varying = false,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::lensdirt
