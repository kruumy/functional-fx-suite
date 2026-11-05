#include "effects/ascii.hpp"
#include "core/color.hpp"
#include "core/noise.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <vector>

namespace functional_fx_suite::effects::ascii
{
    enum class p : std::uint8_t
    {
        scale,
        character_spacing,
        font_size,
        font_color_mode,
        font_color,
        background_color,
        swap_colors,
        invert_brightness,
        dithering,
        dither_intensity,
        count
    };

    using core::param;

    // order and defaults follow the shader's UI; Scale is ours
    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "scale", "Scale").value(1).range(0.1, 100).slider(0.5, 10),
        param::scalar(2, "character_spacing", "Character Spacing").value(1).range(0, 5).digits(0),
        param::choice(3, "font_size", "Font Size", "Smaller 3x5 font|Normal 5x5 font").value(1),
        param::choice(4, "font_color_mode", "Font Color Mode",
                      "Foreground On Background|Colorized Grayscale|Full Color")
            .value(1),
        param::color(5, "font_color", "Font Color").value({1, 1, 1}),
        param::color(6, "background_color", "Background Color").value({0, 0, 0}),
        param::toggle(7, "swap_colors", "Swap Colors"),
        param::toggle(8, "invert_brightness", "Invert Brightness"),
        param::toggle(9, "dithering", "Dithering").value(1),
        param::scalar(10, "dither_intensity", "Dither Intensity").value(2).range(0, 4),
    };

    // row-major bitmaps, bit (width * row + column) set means lit; darkest first, space is handled separately
    static constexpr std::array<std::uint32_t, 13> FONT_3X5{
        4096, 1040, 5136, 5200, 2728, 11088, 14478, 11114, 23213, 15211, 23533, 31599, 31727, // .:;s*oSOXDH08
    };
    static constexpr std::array<std::uint32_t, 16> FONT_5X5{
        4194304,  131200,   324,      330,      283712,   12650880, 4532768,  13191552, // .:^"~cvo
        10648704, 11195936, 15218734, 15255086, 15252014, 32294446, 15324974, 11512810, // *wSO8Q0#
    };

    // one font pixel is one screen pixel at this frame width
    static constexpr double BASE_WIDTH = 1920.0;

    struct cell
    {
        core::color::rgb color{}; // straight average
        float gray = 0;
        std::uint32_t glyph = 0; // 0 is a space
    };

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const core::frame& f = args.geometry;
        const bool small = v.choice(p::font_size) == 0;
        const std::span<const std::uint32_t> font =
            small ? std::span<const std::uint32_t>(FONT_3X5) : std::span<const std::uint32_t>(FONT_5X5);
        const int font_w = small ? 3 : 5;
        const int font_h = 5;
        const int spacing = v.whole(p::character_spacing);
        const double quant = 1.0 / static_cast<double>(font.size()); // levels including space, minus one
        const double font_px = v.scalar(p::scale) * f.width / BASE_WIDTH;
        const int block_w = font_w + spacing;
        const int block_h = font_h + spacing;
        const bool invert = v.toggle(p::invert_brightness);
        const double dither = v.toggle(p::dithering) ? quant * v.scalar(p::dither_intensity) : 0.0;

        // the grid is anchored at the frame centre so it stays put at any resolution
        const core::rect& sb = args.src.bounds;
        // the nudge keeps pixel centres that land exactly on a font pixel edge on the same side in every host
        const auto font_pixel = [&](double offset) { return static_cast<int>(std::floor((offset / font_px) + 1e-6)); };
        const auto floor_div = [](int a, int b) { return (a >= 0 ? a : a - b + 1) / b; };
        const auto cell_x = [&](double px) { return floor_div(font_pixel(px - f.cx), block_w); };
        const auto cell_y = [&](double py) { return floor_div(font_pixel(py - f.cy), block_h); };
        const int gx0 = cell_x(sb.x0 + 0.5);
        const int gy0 = cell_y(sb.y0 + 0.5);
        const int cols = cell_x(sb.x1 - 0.5) - gx0 + 1;
        const int rows = cell_y(sb.y1 - 0.5) - gy0 + 1;
        std::vector<cell> cells(static_cast<std::size_t>(cols) * static_cast<std::size_t>(rows));
        const auto at = [&](int r, int c) -> cell& {
            return cells[(static_cast<std::size_t>(r) * static_cast<std::size_t>(cols)) + static_cast<std::size_t>(c)];
        };

        // each pixel belongs to the cell under its centre, using the same lookup as the drawing pass so every host
        // groups pixels alike; a cell smaller than a pixel takes the next pixel
        std::vector<int> col_of(static_cast<std::size_t>(sb.width()));
        std::vector<int> row_of(static_cast<std::size_t>(sb.height()));
        for (std::size_t i = 0; i < col_of.size(); ++i)
        {
            col_of[i] = cell_x(sb.x0 + static_cast<double>(i) + 0.5) - gx0;
        }
        for (std::size_t i = 0; i < row_of.size(); ++i)
        {
            row_of[i] = cell_y(sb.y0 + static_cast<double>(i) + 0.5) - gy0;
        }
        const auto pixels = [](const std::vector<int>& of, int index, int origin) {
            const auto [first, last] = std::ranges::equal_range(of, index);
            const auto begin = std::min(static_cast<int>(first - of.begin()), static_cast<int>(of.size()) - 1);
            return std::array{origin + begin, origin + std::max(static_cast<int>(last - of.begin()), begin + 1)};
        };
        core::parallel::for_range(0, rows, [&](int r_begin, int r_end) {
            for (int r = r_begin; r < r_end; ++r)
            {
                const auto [y0, y1] = pixels(row_of, r, sb.y0);
                for (int c = 0; c < cols; ++c)
                {
                    const auto [x0, x1] = pixels(col_of, c, sb.x0);
                    std::array<double, 4> sum{};
                    for (int y = y0; y < y1; ++y)
                    {
                        for (int x = x0; x < x1; ++x)
                        {
                            const float* s = args.src.at(x, y);
                            for (int i = 0; i < 4; ++i)
                            {
                                sum[i] += s[i];
                            }
                        }
                    }
                    cell& out = at(r, c);
                    const double inv = sum[3] > 0.0 ? 1.0 / sum[3] : 0.0;
                    out.color = {static_cast<float>(sum[0] * inv), static_cast<float>(sum[1] * inv),
                                 static_cast<float>(sum[2] * inv)};
                    double gray = (0.2126 * out.color[0]) + (0.7152 * out.color[1]) + (0.0722 * out.color[2]);
                    if (invert)
                    {
                        gray = 1.0 - gray;
                    }
                    if (dither != 0.0)
                    {
                        const std::uint32_t h =
                            core::noise::combine(core::noise::hash(static_cast<std::uint32_t>(gx0 + c)),
                                                 static_cast<std::uint32_t>(gy0 + r));
                        gray += dither * (core::noise::unit(h) - 0.5);
                    }
                    out.gray = static_cast<float>(gray);
                    // gray picks the ramp level; the darkest level is a space
                    const double level = std::floor(gray / quant);
                    out.glyph = gray <= quant ? 0
                                              : font[static_cast<std::size_t>(
                                                    std::clamp(level, 1.0, static_cast<double>(font.size())) - 1.0)];
                }
            }
        });

        const auto& font_color = v.vec(p::font_color);
        const auto& bg_color = v.vec(p::background_color);
        const int mode = v.choice(p::font_color_mode);
        const bool swap = v.toggle(p::swap_colors);
        const core::image& dst = args.dst;
        core::parallel::for_pixels(dst.bounds, [&](int x, int y) {
            float* d = dst.at(x, y);
            if (!sb.contains(x, y))
            {
                std::fill_n(d, 4, 0.0f);
                return;
            }
            const int fx = font_pixel(x + 0.5 - f.cx);
            const int fy = font_pixel(y + 0.5 - f.cy);
            const int cx = floor_div(fx, block_w);
            const int cy = floor_div(fy, block_h);
            const cell& c = at(cy - gy0, cx - gx0);
            // font pixel inside the cell; beyond the glyph is spacing
            const int px = fx - (cx * block_w);
            const int py = fy - (cy * block_h);
            const bool lit = px < font_w && py < font_h && ((c.glyph >> ((font_w * py) + px)) & 1U) != 0;

            const float a = args.src.at(x, y)[3];
            for (int i = 0; i < 3; ++i)
            {
                const auto on = static_cast<float>(swap ? bg_color[i] : font_color[i]);
                const auto off = static_cast<float>(swap ? font_color[i] : bg_color[i]);
                float value = off;
                if (lit)
                {
                    value = mode == 2 ? c.color[i] : mode == 1 ? on * c.gray : on;
                }
                d[i] = std::clamp(value, 0.0f, 1.0f) * a;
            }
            d[3] = a;
        });
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_ascii",
            .name = "f_ascii",
            .category = "Functional Stylize",
            .description = "Redraws the clip as a grid of text characters picked by brightness.",
            .params = PARAMS,
            .mask_input = "",
            .background_input = "",
            .time_varying = false,
            .reads_outside = true,
            .render = render,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::ascii
