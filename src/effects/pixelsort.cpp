#include "effects/pixelsort.hpp"
#include "core/blur.hpp"
#include "core/color.hpp"
#include "core/mask.hpp"
#include "core/noise.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <numeric>
#include <vector>

namespace functional_fx_suite::effects::pixelsort
{
    enum class p : std::uint8_t
    {
        style,
        sort_angle,
        center,
        radial_start_angle,
        radial_degrees_sorted,
        radial_inner_radius,
        ray_length,
        vary_radius,
        start_angle,
        degrees_sorted,
        circle_center,
        vary_start,
        inner_radius,
        thickness,
        threshold,
        sort_direction,
        reverse_sort_direction,
        sort_type,
        randomly_restart_sort,
        soften_threshold_mask,
        seed,
        mix_with_source,
        show,
        matte_use,
        blur_matte,
        invert_matte,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;
    static constexpr int STYLE = static_cast<int>(p::style);
    static constexpr std::uint32_t LINEAR = 0b001;
    static constexpr std::uint32_t RADIAL = 0b010;
    static constexpr std::uint32_t CIRCULAR = 0b100;

    // the three styles (popup order Linear, Radial, Circular) share every parameter after the geometry;
    // angles are in degrees, counter-clockwise from the +x axis; radii are in xy units (half frame widths)
    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::choice(1, "style", "Style", "Linear|Radial|Circular"),
        param::scalar(2, "sort_angle", "Sort Angle")
            .value(0)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(-180, 180)
            .enabled_when(STYLE, LINEAR),
        param::point(3, "center", "Center").value({0, 0, 0}).enabled_when(STYLE, RADIAL),
        param::scalar(4, "radial_start_angle", "Start Angle")
            .value(0)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(-180, 180)
            .enabled_when(STYLE, RADIAL),
        param::scalar(5, "radial_degrees_sorted", "Degrees Sorted")
            .value(360)
            .range(0, 360)
            .enabled_when(STYLE, RADIAL),
        param::scalar(6, "radial_inner_radius", "Inner Radius")
            .value(0.1)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 2)
            .enabled_when(STYLE, RADIAL),
        param::scalar(7, "ray_length", "Ray Length")
            .value(0.8)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 2)
            .enabled_when(STYLE, RADIAL),
        param::scalar(8, "vary_radius", "Vary Radius").value(0.1).range(0, 1).enabled_when(STYLE, RADIAL),
        param::scalar(9, "start_angle", "Start Angle")
            .value(0)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(-180, 180)
            .enabled_when(STYLE, CIRCULAR),
        param::scalar(10, "degrees_sorted", "Degrees Sorted").value(270).range(0, 360).enabled_when(STYLE, CIRCULAR),
        param::point(11, "circle_center", "Circle Center").value({0, 0, 0}).enabled_when(STYLE, CIRCULAR),
        param::scalar(12, "vary_start", "Vary Start").value(0.15).range(0, 1).enabled_when(STYLE, CIRCULAR),
        param::scalar(13, "inner_radius", "Inner Radius")
            .value(0)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 2)
            .enabled_when(STYLE, CIRCULAR),
        param::scalar(14, "thickness", "Thickness")
            .value(1.1)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(0, 2.2)
            .enabled_when(STYLE, CIRCULAR),
        param::scalar(15, "threshold", "Threshold").value(0.3).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::choice(16, "sort_direction", "Sort Direction", "Sort Below Threshold|Sort Above Threshold").value(1),
        param::toggle(17, "reverse_sort_direction", "Reverse Sort Direction"),
        param::choice(18, "sort_type", "Sort Type",
                      "Monochrome|Average|Minimum|Maximum|Red|Green|Blue|Hue|Saturation|Brightness"),
        param::scalar(19, "randomly_restart_sort", "Randomly Restart Sort").value(100).range(0, 1000),
        param::scalar(20, "soften_threshold_mask", "Soften Threshold Mask").value(0.1).range(0, UNBOUNDED).slider(0, 1),
        param::scalar(21, "seed", "Seed").value(0.273).range(-UNBOUNDED, UNBOUNDED).slider(0, 1),
        param::scalar(22, "mix_with_source", "Mix With Source").value(0).range(-UNBOUNDED, 1).slider(0, 1),
        param::choice(23, "show", "Show", "Result|Raw Sort Values|Threshold Mask|Random Restart Noise|Combined Mask"),
        param::choice(24, "matte_use", "Matte Use", "Luma|Alpha"),
        param::scalar(25, "blur_matte", "Blur Matte").value(0.05).range(0, 1000).slider(0, 2),
        param::toggle(26, "invert_matte", "Invert Matte"),
    };

    enum class style : std::uint8_t
    {
        linear,
        radial,
        circular
    };

    enum class show : std::uint8_t
    {
        result,
        sort_values,
        threshold_mask,
        restart_noise,
        combined_mask
    };

    static float sort_value(const float* c, int type)
    {
        const float hi = std::max({c[0], c[1], c[2]});
        const float lo = std::min({c[0], c[1], c[2]});
        switch (type)
        {
        case 1:
            return (c[0] + c[1] + c[2]) / 3.0f;
        case 2:
            return lo;
        case 3:
        case 9:
            return hi;
        case 4:
        case 5:
        case 6:
            return c[type - 4];
        case 7: {
            const float range = hi - lo;
            if (range <= 0.0f)
            {
                return 0.0f;
            }
            const float h = hi == c[0]   ? (c[1] - c[2]) / range
                            : hi == c[1] ? 2.0f + (c[2] - c[0]) / range
                                         : 4.0f + (c[0] - c[1]) / range;
            return (h < 0.0f ? h + 6.0f : h) / 6.0f;
        }
        case 8:
            return hi > 0.0f ? (hi - lo) / hi : 0.0f;
        default:
            return core::color::luma(c[0], c[1], c[2]);
        }
    }

    // where each pixel sits in the sort: which line it belongs to, its position along it, and whether the
    // style's geometry lets it be sorted at all
    struct placement
    {
        std::vector<int> line;
        std::vector<float> along;
        int lines = 0;
    };

    static placement place(const core::render_args& args, const core::param_values& v)
    {
        const core::rect& b = args.src.bounds;
        const core::frame& f = args.geometry;
        const int w = b.width();
        const int h = b.height();
        placement out;
        out.line.assign(static_cast<std::size_t>(w) * h, -1);
        out.along.assign(out.line.size(), 0.0f);
        const auto mode = static_cast<style>(std::clamp(v.choice(p::style), 0, 2));
        const std::uint32_t seed = core::noise::seed_bits(v.scalar(p::seed));

        if (mode == style::linear)
        {
            // pixel lines along the sort direction that cover every pixel exactly once (y down in pixels)
            const double angle = v.scalar(p::sort_angle) * std::numbers::pi / 180.0;
            const double dx = std::cos(angle);
            const double dy = -std::sin(angle);
            const bool shallow = std::abs(dx) >= std::abs(dy);
            const double slope = shallow ? dy / dx : dx / dy;
            const double sign = shallow ? (dx >= 0.0 ? 1.0 : -1.0) : (dy >= 0.0 ? 1.0 : -1.0);
            const int major = shallow ? w : h;
            const int minor = shallow ? h : w;
            const int reach = static_cast<int>(std::lround(std::abs(slope) * major)) + 1;
            out.lines = minor + 2 * reach;
            for (int y = 0; y < h; ++y)
            {
                for (int x = 0; x < w; ++x)
                {
                    const int a = shallow ? x : y;
                    const int m = shallow ? y : x;
                    const std::size_t i = static_cast<std::size_t>(y) * w + x;
                    out.line[i] = m - static_cast<int>(std::lround(a * slope)) + reach;
                    out.along[i] = static_cast<float>(a * sign);
                }
            }
            return out;
        }

        const bool radial = mode == style::radial;
        const auto& c = radial ? v.vec(p::center) : v.vec(p::circle_center);
        const double cx = f.to_px_x(c[0]);
        const double cy = f.to_px_y(c[1]);
        // radii are in xy units
        const double px_per_xy = f.to_px(1.0 / core::XY_PER_FRAME_UNIT);
        const double inner = (radial ? v.scalar(p::radial_inner_radius) : v.scalar(p::inner_radius)) * px_per_xy;
        const double span = (radial ? v.scalar(p::ray_length) : v.scalar(p::thickness)) * px_per_xy;
        const double start = (radial ? v.scalar(p::radial_start_angle) : v.scalar(p::start_angle)) / 360.0;
        const double degrees = (radial ? v.scalar(p::radial_degrees_sorted) : v.scalar(p::degrees_sorted)) / 360.0;
        const double far = std::hypot(std::max(cx - b.x0, b.x1 - cx), std::max(cy - b.y0, b.y1 - cy));
        // rays: one per pixel of circumference at the farthest corner; rings: one per pixel of radius
        const int rays = std::max(8, static_cast<int>(std::ceil(2.0 * std::numbers::pi * far)));
        out.lines = radial ? rays : static_cast<int>(std::ceil(far)) + 1;
        const double vary = radial ? v.scalar(p::vary_radius) : v.scalar(p::vary_start);
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                const double ox = b.x0 + x + 0.5 - cx;
                const double oy = cy - (b.y0 + y + 0.5);
                const double r = std::hypot(ox, oy);
                double turn = std::atan2(oy, ox) / (2.0 * std::numbers::pi) - start;
                turn -= std::floor(turn);
                const std::size_t i = static_cast<std::size_t>(y) * w + x;
                if (radial)
                {
                    const int ray = std::min(rays - 1, static_cast<int>(turn * rays));
                    // each ray ends at its own random fraction of the full length
                    const double u = core::noise::unit(core::noise::combine(seed, static_cast<std::uint32_t>(ray)));
                    const double end = inner + span * (1.0 - vary * u);
                    if (turn <= degrees && r >= inner && r <= end)
                    {
                        out.line[i] = ray;
                        out.along[i] = static_cast<float>(r);
                    }
                    continue;
                }
                const int ring = static_cast<int>(r);
                // each ring starts its arc at its own random offset
                const double u = core::noise::unit(core::noise::combine(seed, static_cast<std::uint32_t>(ring)));
                double t = turn - vary * u;
                t -= std::floor(t);
                if (t <= degrees && r >= inner && r <= inner + span)
                {
                    out.line[i] = ring;
                    out.along[i] = static_cast<float>(t);
                }
            }
        }
        return out;
    }

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const core::image& src = args.src;
        const core::rect& b = src.bounds;
        const core::frame& f = args.geometry;
        const int w = b.width();
        const int type = v.choice(p::sort_type);
        const float threshold = v.scalar_f(p::threshold);
        const bool above = v.choice(p::sort_direction) == 1;
        const std::uint32_t seed = core::noise::seed_bits(v.scalar(p::seed));

        // sort values and the soft threshold mask, both from the premultiplied pixels
        core::buffer values(b, 1);
        core::buffer soft(b, 1);
        core::buffer noise(b, 1);
        const double freq = v.scalar(p::randomly_restart_sort);
        core::parallel::for_range(b.y0, b.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                const double ry = core::XY_PER_FRAME_UNIT * f.to_ny(y + 0.5) * freq;
                for (int x = b.x0; x < b.x1; ++x)
                {
                    const float s = sort_value(src.at(x, y), type);
                    *values.view().at(x, y) = s;
                    *soft.view().at(x, y) = (s >= threshold) == above ? 1.0f : 0.0f;
                    const double rx = core::XY_PER_FRAME_UNIT * f.to_nx(x + 0.5) * freq;
                    *noise.view().at(x, y) =
                        freq > 0.0 ? static_cast<float>(core::noise::fbm_3d(seed, rx, ry, 0.0, 2, 0.5)) : 1.0f;
                }
            }
        });
        const double soften = core::blur::amount_px(v.scalar(p::soften_threshold_mask), f);
        core::blur::gaussian(soft.view(), soften, soften, core::blur::edge::reflect);

        // restart dots: single pixels at the noise lattice points whose value is high, each one breaks a run
        core::buffer dots(b, 1);
        if (freq > 0.0)
        {
            constexpr double DOT_LEVEL = 0.8;
            const double scale = core::XY_PER_FRAME_UNIT * freq;
            const auto x0 = static_cast<std::int64_t>(std::floor(f.to_nx(b.x0) * scale));
            const auto x1 = static_cast<std::int64_t>(std::ceil(f.to_nx(b.x1) * scale));
            const auto y0 = static_cast<std::int64_t>(std::floor(f.to_ny(b.y1) * scale));
            const auto y1 = static_cast<std::int64_t>(std::ceil(f.to_ny(b.y0) * scale));
            for (std::int64_t iy = y0; iy <= y1; ++iy)
            {
                for (std::int64_t ix = x0; ix <= x1; ++ix)
                {
                    const auto fx = static_cast<double>(ix);
                    const auto fy = static_cast<double>(iy);
                    if (core::noise::value_2d(seed + 1, fx, fy) <= DOT_LEVEL)
                    {
                        continue;
                    }
                    const auto px = static_cast<int>(std::floor(f.to_px_x(fx / scale)));
                    const auto py = static_cast<int>(std::floor(f.to_px_y(fy / scale)));
                    if (b.contains(px, py))
                    {
                        *dots.view().at(px, py) = 1.0f;
                    }
                }
            }
        }

        const auto mode = v.choice(p::matte_use) == 1 ? core::mask::source::alpha : core::mask::source::luma;
        const core::buffer matte = core::mask::build(args, mode, v.scalar(p::blur_matte), v.toggle(p::invert_matte));
        core::buffer combined(b, 1);
        core::parallel::for_range(b.y0, b.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                for (int x = b.x0; x < b.x1; ++x)
                {
                    const float m = matte.view().data != nullptr ? *matte.view().at(x, y) : 1.0f;
                    *combined.view().at(x, y) = m * *soft.view().at(x, y) * (1.0f - *dots.view().at(x, y));
                }
            }
        });

        // sort each run of included pixels along its line by value; the soft mask then blends it in
        const placement pl = place(args, v);
        std::vector<int> start(static_cast<std::size_t>(pl.lines) + 1, 0);
        for (const int l : pl.line)
        {
            if (l >= 0)
            {
                ++start[static_cast<std::size_t>(l) + 1];
            }
        }
        std::partial_sum(start.begin(), start.end(), start.begin());
        std::vector<int> order(static_cast<std::size_t>(start.back()));
        {
            std::vector<int> fill(start.begin(), start.end() - 1);
            for (std::size_t i = 0; i < pl.line.size(); ++i)
            {
                if (const int l = pl.line[i]; l >= 0)
                {
                    order[static_cast<std::size_t>(fill[static_cast<std::size_t>(l)]++)] = static_cast<int>(i);
                }
            }
        }
        const bool reverse = v.toggle(p::reverse_sort_direction);
        core::buffer sorted(b, 4);
        const core::image& out = sorted.view();
        core::parallel::for_range(b.y0, b.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                std::copy_n(src.row(y), static_cast<std::size_t>(w) * 4, out.row(y));
            }
        });
        const auto pixel = [&](int i) { return std::array{b.x0 + i % w, b.y0 + i / w}; };
        // low-noise pixels take the extreme sort value, which splits the sorted runs into chunks
        const auto key = [&](int i) {
            constexpr float CHUNK_LEVEL = 0.1f;
            const auto [x, y] = pixel(i);
            return *noise.view().at(x, y) < CHUNK_LEVEL ? (above ? 0.0f : 1.0f) : *values.view().at(x, y);
        };
        core::parallel::for_range(0, pl.lines, [&](int l_begin, int l_end) {
            std::vector<int> run;
            std::vector<int> by_value;
            for (int l = l_begin; l < l_end; ++l)
            {
                const auto first = order.begin() + start[static_cast<std::size_t>(l)];
                const auto last = order.begin() + start[static_cast<std::size_t>(l) + 1];
                std::sort(first, last, [&](int a, int c) {
                    return pl.along[static_cast<std::size_t>(a)] < pl.along[static_cast<std::size_t>(c)];
                });
                for (auto it = first; it != last;)
                {
                    run.clear();
                    for (; it != last; ++it)
                    {
                        const auto [x, y] = pixel(*it);
                        if (*combined.view().at(x, y) < 0.5f)
                        {
                            ++it;
                            break;
                        }
                        run.push_back(*it);
                    }
                    if (run.size() < 2)
                    {
                        continue;
                    }
                    by_value = run;
                    std::ranges::stable_sort(by_value,
                                             [&](int a, int c) { return reverse ? key(a) > key(c) : key(a) < key(c); });
                    for (std::size_t k = 0; k < run.size(); ++k)
                    {
                        const auto [dx, dy] = pixel(run[k]);
                        const auto [sx, sy] = pixel(by_value[k]);
                        std::copy_n(src.at(sx, sy), 4, out.at(dx, dy));
                    }
                }
            }
        });

        const auto what = static_cast<show>(std::clamp(v.choice(p::show), 0, 4));
        const float mix = v.scalar_f(p::mix_with_source);
        const core::image& dst = args.dst;
        core::parallel::for_range(dst.bounds.y0, dst.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                for (int x = dst.bounds.x0; x < dst.bounds.x1; ++x)
                {
                    float* d = dst.at(x, y);
                    if (!b.contains(x, y))
                    {
                        std::fill_n(d, 4, 0.0f);
                        continue;
                    }
                    const float* s = src.at(x, y);
                    std::array<float, 4> result{};
                    const float k = std::clamp(*combined.view().at(x, y), 0.0f, 1.0f);
                    const std::array<const core::buffer*, 4> views{&values, &soft, &dots, &combined};
                    if (what == show::result)
                    {
                        const float* r = out.at(x, y);
                        for (int c = 0; c < 4; ++c)
                        {
                            result[c] = core::color::lerp(s[c], r[c], k);
                        }
                    }
                    else
                    {
                        const float g = *views[static_cast<std::size_t>(what) - 1]->view().at(x, y);
                        result = {g, g, g, 1.0f};
                    }
                    for (int c = 0; c < 4; ++c)
                    {
                        d[c] = core::color::lerp(result[c], s[c], mix);
                    }
                }
            }
        });
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_pixelsort",
            .name = "f_pixelsort",
            .category = "Functional Stylize",
            .description = "Sorts runs of pixels by brightness or color along lines, rays or rings.",
            .params = PARAMS,
            .mask_input = "Matte",
            .background_input = "",
            .time_varying = false,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::pixelsort
