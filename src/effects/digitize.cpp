#include "effects/digitize.hpp"
#include "core/blur.hpp"
#include "core/color.hpp"
#include "core/mask.hpp"
#include "core/parallel.hpp"
#include "core/sample.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace functional_fx_suite::effects::digitize
{
    enum class p : std::uint8_t
    {
        view,
        pixelation,
        pixel_strength,
        pixel_frame_shift,
        block_size,
        block_rel_height,
        pixelation_end,
        wavy,
        wavy_strength,
        wavy_frame_shift,
        wavy_frequency,
        wavy_sharpness,
        wavy_angle,
        wavy_smooth,
        wavy_color,
        wavy_displace_x,
        wavy_displace_y,
        wavy_displace_smooth,
        wavy_end,
        horizontal,
        horizontal_strength,
        horizontal_frame_shift,
        horizontal_frequency,
        horizontal_sharpness,
        horizontal_angle,
        horizontal_smooth,
        horizontal_brightness,
        horizontal_offset,
        horizontal_end,
        matte_use,
        blur_matte,
        invert_matte,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;
    static constexpr double MAX_SHIFT = 100;

    // three layers built from the clip `Frame Shift` frames away: pixel blocks laid over it, then wavy and horizontal
    // lines color-dodged on top. Sizes and displacements are in frame widths, frequencies in lines per frame width,
    // Smooth like other blur widths; Angle 0 draws horizontal lines
    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::choice(21, "view", "View", "Result|Pixelation|Wavy Lines|Horizontal Lines"),
        param::group(22, "pixelation", "Pixelation"),
        param::scalar(23, "pixel_strength", "Strength").value(0.5).range(0, 1),
        param::scalar(24, "pixel_frame_shift", "Frame Shift")
            .value(1)
            .range(-MAX_SHIFT, MAX_SHIFT)
            .slider(-10, 10)
            .digits(0),
        param::scalar(1, "block_size", "Block Size").value(0.0222).range(0.0005, UNBOUNDED).slider(0.002, 0.1),
        param::scalar(2, "block_rel_height", "Block Rel Height").value(0.41).range(0.01, UNBOUNDED).slider(0.1, 10),
        param::group_end(25, "pixelation_end"),
        param::group(26, "wavy", "Wavy Lines"),
        param::scalar(27, "wavy_strength", "Strength").value(0.5).range(0, 1),
        param::scalar(28, "wavy_frame_shift", "Frame Shift")
            .value(1)
            .range(-MAX_SHIFT, MAX_SHIFT)
            .slider(-10, 10)
            .digits(0),
        param::scalar(29, "wavy_frequency", "Frequency").value(242).range(0, UNBOUNDED).slider(0, 1024),
        param::scalar(30, "wavy_sharpness", "Sharpness").value(1.14).range(0.001, 2),
        param::scalar(31, "wavy_angle", "Angle").value(90).range(-UNBOUNDED, UNBOUNDED).slider(-180, 180),
        param::scalar(32, "wavy_smooth", "Smooth Source").value(0.06).range(0, 1000).slider(0, 1),
        param::color(14, "wavy_color", "Color").value({1.0, 0.25, 0.4}),
        param::scalar(33, "wavy_displace_x", "Horizontal Displacement")
            .value(0.032)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(-0.2, 0.2),
        param::scalar(34, "wavy_displace_y", "Vertical Displacement")
            .value(0.057)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(-0.2, 0.2),
        param::scalar(35, "wavy_displace_smooth", "Displacement Smooth").value(0.1).range(0, 1000).slider(0, 1),
        param::group_end(36, "wavy_end"),
        param::group(37, "horizontal", "Horizontal Lines"),
        param::scalar(38, "horizontal_strength", "Strength").value(0.5).range(0, 1),
        param::scalar(39, "horizontal_frame_shift", "Frame Shift")
            .value(1)
            .range(-MAX_SHIFT, MAX_SHIFT)
            .slider(-10, 10)
            .digits(0),
        param::scalar(40, "horizontal_frequency", "Frequency").value(300).range(0, UNBOUNDED).slider(0, 1024),
        param::scalar(41, "horizontal_sharpness", "Sharpness").value(1.4).range(0.001, 2),
        param::scalar(42, "horizontal_angle", "Angle").value(0).range(-UNBOUNDED, UNBOUNDED).slider(-180, 180),
        param::scalar(43, "horizontal_smooth", "Smooth Source").value(0.083).range(0, 1000).slider(0, 1),
        param::scalar(16, "horizontal_brightness", "Brightness").value(1.7).range(0, UNBOUNDED).slider(0, 10),
        param::scalar(44, "horizontal_offset", "Offset").value(-0.2).range(-1, 1),
        param::group_end(45, "horizontal_end"),
        param::choice(18, "matte_use", "Matte Use", "Luma|Alpha"),
        param::scalar(19, "blur_matte", "Blur Matte").value(0).range(0, 1000).slider(0, 2),
        param::toggle(20, "invert_matte", "Invert Matte"),
    };

    enum class view : std::uint8_t
    {
        result,
        pixelation,
        wavy,
        horizontal
    };

    static constexpr std::array LAYERS{
        std::array{p::pixel_strength, p::pixel_frame_shift},
        std::array{p::wavy_strength, p::wavy_frame_shift},
        std::array{p::horizontal_strength, p::horizontal_frame_shift},
    };

    // a layer's frame shift, or 0 when the layer is off
    static int shift_of(const core::param_values& v, std::size_t layer)
    {
        return v.scalar(LAYERS[layer][0]) > 0.0 ? v.whole(LAYERS[layer][1]) : 0;
    }

    static std::vector<int> frame_offsets(const core::param_values& v, int /*frame*/)
    {
        std::vector<int> offsets;
        for (std::size_t i = 0; i < LAYERS.size(); ++i)
        {
            const int n = shift_of(v, i);
            if (n != 0 && std::ranges::find(offsets, n) == offsets.end())
            {
                offsets.push_back(n);
            }
        }
        return offsets;
    }

    // mean premultiplied color of each block of a grid centred on the frame
    struct blocks
    {
        double width = 1; // pixels
        double height = 1;
        double ox = 0;
        double oy = 0;
        int first_row = 0;
        int first_col = 0;
        int rows = 0;
        int cols = 0;
        std::vector<core::sample::pixel> means; // row-major

        [[nodiscard]] int row_of(int y) const
        {
            return static_cast<int>(std::floor((y + 0.5 - oy) / height)) - first_row;
        }
        [[nodiscard]] int col_of(int x) const
        {
            return static_cast<int>(std::floor((x + 0.5 - ox) / width)) - first_col;
        }
        [[nodiscard]] core::sample::pixel at(int x, int y) const
        {
            const int r = row_of(y);
            const int c = col_of(x);
            return r >= 0 && r < rows && c >= 0 && c < cols ? means[static_cast<std::size_t>(r) * cols + c]
                                                            : core::sample::pixel{};
        }
    };

    static blocks measure(const core::image& img, const core::frame& f, double size, double rel_height)
    {
        const core::rect& b = img.bounds;
        blocks out;
        out.width = std::max(1.0, f.to_px(size));
        out.height = std::max(1.0, out.width * rel_height);
        out.ox = f.cx;
        out.oy = f.cy;
        if (b.width() <= 0 || b.height() <= 0)
        {
            return out;
        }
        out.first_row = out.row_of(b.y0);
        out.first_col = out.col_of(b.x0);
        out.rows = out.row_of(b.y1 - 1) + 1;
        out.cols = out.col_of(b.x1 - 1) + 1;
        // the first pixel row of each block row, rows being monotonic in y
        std::vector<int> starts(static_cast<std::size_t>(out.rows) + 1, b.y1);
        for (int y = b.y1 - 1; y >= b.y0; --y)
        {
            starts[static_cast<std::size_t>(out.row_of(y))] = y;
        }
        for (int r = out.rows - 1; r >= 0; --r)
        {
            const auto i = static_cast<std::size_t>(r);
            starts[i] = std::min(starts[i], starts[i + 1]);
        }
        std::vector<int> col(static_cast<std::size_t>(b.width()));
        for (int x = b.x0; x < b.x1; ++x)
        {
            col[static_cast<std::size_t>(x - b.x0)] = out.col_of(x);
        }
        out.means.resize(static_cast<std::size_t>(out.rows) * out.cols);
        core::parallel::for_range(0, out.rows, [&](int begin, int end) {
            std::vector<std::array<double, 5>> sums(static_cast<std::size_t>(out.cols));
            for (int r = begin; r < end; ++r)
            {
                std::ranges::fill(sums, std::array<double, 5>{});
                for (int y = starts[static_cast<std::size_t>(r)]; y < starts[static_cast<std::size_t>(r) + 1]; ++y)
                {
                    for (int x = b.x0; x < b.x1; ++x)
                    {
                        const float* px = img.at(x, y);
                        auto& sum = sums[static_cast<std::size_t>(col[static_cast<std::size_t>(x - b.x0)])];
                        for (std::size_t c = 0; c < 4; ++c)
                        {
                            sum[c] += px[c];
                        }
                        sum[4] += 1.0;
                    }
                }
                for (std::size_t c = 0; c < sums.size(); ++c)
                {
                    auto& mean = out.means[(static_cast<std::size_t>(r) * out.cols) + c];
                    for (std::size_t k = 0; k < 4; ++k)
                    {
                        mean[k] = sums[c][4] > 0.0 ? static_cast<float>(sums[c][k] / sums[c][4]) : 0.0f;
                    }
                }
            }
        });
        return out;
    }

    // a line pattern in 0..1 across the frame; Sharpness below 1 squares the waves off, above 1 lifts them toward white
    struct lines
    {
        double dx = 0; // cycles per pixel along each axis
        double dy = 0;
        double cx = 0;
        double cy = 0;
        double sharpness = 1;

        lines(const core::frame& f, double frequency, double angle_deg, double sharp)
            : cx(f.cx), cy(f.cy), sharpness(sharp)
        {
            // angle 0 is horizontal lines, so the wave runs along y; pixel y is down
            const double a = angle_deg * std::numbers::pi / 180.0;
            dx = std::sin(a) * frequency / f.width;
            dy = -std::cos(a) * frequency / f.width;
        }

        [[nodiscard]] float at(double x, double y) const
        {
            const double c = std::cos(2.0 * std::numbers::pi * ((x - cx) * dx + (y - cy) * dy));
            const double shaped = sharpness < 1.0 ? std::copysign(std::pow(std::abs(c), sharpness), c)
                                                  : c + (1.0 - c) * (sharpness - 1.0);
            return static_cast<float>(0.5 + 0.5 * shaped);
        }
    };

    // color dodge on straight color; never darkens, saturates at 1 unless already brighter
    static float dodge(float base, float blend)
    {
        const float b = std::max(base, 0.0f);
        return blend >= 1.0f ? std::max(b, 1.0f) : std::max(b, std::min(b / (1.0f - blend), 1.0f));
    }

    static core::buffer smoothed(const core::image& img, double amount)
    {
        core::buffer out = core::buffer::copy_of(img);
        core::blur::gaussian(out.view(), amount, amount, core::blur::edge::reflect);
        return out;
    }

    static core::sample::pixel straight(const core::sample::pixel& s)
    {
        return s[3] > 0.0f ? core::sample::pixel{s[0] / s[3], s[1] / s[3], s[2] / s[3], s[3]} : core::sample::pixel{};
    }

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const core::frame& f = args.geometry;
        const auto shown = static_cast<view>(v.choice(p::view));

        // each layer's frame, fetched once per distinct shift; the current one when the host has none
        std::array<core::buffer, 3> fetched;
        std::array<core::image, 3> frames;
        for (std::size_t i = 0; i < LAYERS.size(); ++i)
        {
            const int n = shift_of(v, i);
            std::size_t first = 0;
            while (shift_of(v, first) != n)
            {
                ++first;
            }
            if (n != 0 && first == i)
            {
                fetched[i] = args.frame_at(n);
            }
            const core::image* img = fetched[first].view_if_set();
            frames[i] = img != nullptr ? *img : args.src;
        }
        const std::array<float, 3> strength{v.scalar_f(p::pixel_strength), v.scalar_f(p::wavy_strength),
                                            v.scalar_f(p::horizontal_strength)};
        const auto amount = [&](p width) { return core::blur::amount_px(v.scalar(width), f); };

        const blocks pix = strength[0] > 0.0f
                               ? measure(frames[0], f, v.scalar(p::block_size), v.scalar(p::block_rel_height))
                               : blocks{};

        core::buffer wavy;
        core::buffer displace;
        if (strength[1] > 0.0f)
        {
            wavy = smoothed(frames[1], amount(p::wavy_smooth));
            displace = core::buffer(frames[1].bounds, 1);
            const core::image& d = displace.view();
            core::parallel::for_pixels(d.bounds, [&](int x, int y) {
                const float* s = frames[1].at(x, y);
                *d.at(x, y) = core::color::luma(s[0], s[1], s[2]);
            });
            core::blur::gaussian(d, amount(p::wavy_displace_smooth), amount(p::wavy_displace_smooth),
                                 core::blur::edge::reflect);
        }
        const lines wavy_lines(f, v.scalar(p::wavy_frequency), v.scalar(p::wavy_angle), v.scalar(p::wavy_sharpness));
        const double shift_x = f.to_px(v.scalar(p::wavy_displace_x));
        const double shift_y = f.to_px(v.scalar(p::wavy_displace_y));
        const auto& tint = v.vec(p::wavy_color);
        const core::color::rgb color{static_cast<float>(tint[0]), static_cast<float>(tint[1]),
                                     static_cast<float>(tint[2])};
        const float color_luma = core::color::luma(color[0], color[1], color[2]);

        const core::buffer horizontal =
            strength[2] > 0.0f ? smoothed(frames[2], amount(p::horizontal_smooth)) : core::buffer{};
        const lines horizontal_lines(f, v.scalar(p::horizontal_frequency), v.scalar(p::horizontal_angle),
                                     v.scalar(p::horizontal_sharpness));
        const float brightness = v.scalar_f(p::horizontal_brightness);
        const float offset = v.scalar_f(p::horizontal_offset);

        const core::image& dst = args.dst;
        core::parallel::for_pixels(dst.bounds, [&](int x, int y) {
            float* d = dst.at(x, y);
            const double cx = x + 0.5;
            const double cy = y + 0.5;
            // each layer as premultiplied color, already scaled by its strength
            core::sample::pixel p_layer = pix.at(x, y);
            for (float& c : p_layer)
            {
                c *= strength[0];
            }
            core::sample::pixel w_layer{};
            if (const core::image* w = wavy.view_if_set(); w != nullptr)
            {
                // the lines and the smoothed frame both bent by the frame's smoothed luma
                const float* lum = core::sample::nearest(displace.view(), cx, cy);
                const double n = lum != nullptr ? *lum - 0.5 : 0.0;
                const double qx = cx + n * shift_x;
                const double qy = cy - n * shift_y;
                const core::sample::pixel s = straight(core::sample::bilinear(*w, qx, qy, core::sample::wrap::reflect));
                // the frame's brightness in lines, recolored to Color at the same luma
                const float l = core::color::luma(s[0], s[1], s[2]) * wavy_lines.at(qx, qy);
                const float a = s[3] * strength[1];
                for (std::size_t k = 0; k < 3; ++k)
                {
                    w_layer[k] = (color_luma > 0.0f ? l * color[k] / color_luma : 0.0f) * a;
                }
                w_layer[3] = a;
            }
            core::sample::pixel h_layer{};
            if (const core::image* h = horizontal.view_if_set(); h != nullptr)
            {
                const float* hs = core::sample::nearest(*h, cx, cy);
                const core::sample::pixel s =
                    hs != nullptr ? straight({hs[0], hs[1], hs[2], hs[3]}) : core::sample::pixel{};
                const float line = std::clamp(((1.0f - horizontal_lines.at(cx, cy)) * brightness) + offset, 0.0f, 1.0f);
                const float a = s[3] * strength[2];
                for (std::size_t k = 0; k < 3; ++k)
                {
                    h_layer[k] = s[k] * line * a;
                }
                h_layer[3] = a;
            }

            const auto put = [d](const core::sample::pixel& c) { std::copy_n(c.begin(), 4, d); };
            if (shown != view::result)
            {
                put(shown == view::pixelation ? p_layer : shown == view::wavy ? w_layer : h_layer);
                return;
            }

            const float* s = core::sample::nearest(args.src, cx, cy);
            core::sample::pixel c = s != nullptr ? core::sample::pixel{s[0], s[1], s[2], s[3]} : core::sample::pixel{};
            for (std::size_t k = 0; k < 4; ++k)
            {
                c[k] = p_layer[k] + (c[k] * (1.0f - p_layer[3]));
            }
            // the lines dodge the color under them by their alpha, keeping its alpha
            for (const core::sample::pixel* layer : {&w_layer, &h_layer})
            {
                const float la = (*layer)[3];
                if (la <= 0.0f || c[3] <= 0.0f)
                {
                    continue;
                }
                for (std::size_t k = 0; k < 3; ++k)
                {
                    const float dodged = dodge(c[k] / c[3], (*layer)[k] / la) * c[3];
                    c[k] = core::color::lerp(c[k], dodged, la);
                }
            }
            put(c);
        });

        core::mask::apply(args, core::mask::luma_or_alpha(v.choice(p::matte_use)), v.scalar(p::blur_matte),
                          v.toggle(p::invert_matte));
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_digitize",
            .name = "f_digitize",
            .category = "Functional Stylize",
            .description =
                "Breaks the clip into a digital ghost of a nearby frame: pixel blocks with tinted wavy lines "
                "and fine horizontal lines dodged over them.",
            .params = PARAMS,
            .mask_input = "Matte",
            .background_input = "",
            .time_varying = false,
            .reads_outside = true,
            .render = render,
            .frame_offsets = frame_offsets,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::digitize
