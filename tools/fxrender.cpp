#include "core/buffer.hpp"
#include "core/param_values.hpp"
#include "effects/catalog.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace functional_fx_suite::tools::fxrender
{
    struct options
    {
        std::string effect;
        std::string in;
        std::string out;
        int width = 960;
        int height = 540;
        double time = 0.0;
        double fps = 24.0;
        bool list_params = false;
        std::vector<std::pair<std::string, std::string>> assignments;
    };

    static constexpr std::string_view USAGE = R"(usage: fxrender <effect> [options] [name=value ...]
  --in <file.ppm>    input image (binary P6); default is a synthetic test card. A run of '#'
                     (clip_####.ppm) numbers a sequence: frame = round(time * fps), and effects
                     that read other frames load their numbers; missing files are transparent
  --out <file.ppm>   output image; default <effect>.ppm
  --size <W>x<H>     test card size (default 960x540)
  --time <seconds>   render time (default 0)
  --fps <rate>       frame rate (default 24)
  --params           print the parameter table as markdown and exit
Vectors take comma-separated values (center=0.1,0.2); choices take their index.
Effects: f_shake f_tint f_rays f_glow f_ultraglow f_glowaura f_glowdark f_glowrings f_glint f_hotspots
         f_edgerays f_vignette f_badcolors f_duotone f_tritone f_edgedetect f_edgecolorize f_flicker
         f_mosaic f_scanlines f_halftone f_halftonecolor f_jpegdamage f_pixelsort f_digitaldamage
         f_tvdamage f_filmdamage f_distort f_distortblur f_distortchroma f_distortrgb f_warpchroma
         f_warpfisheye f_warppuddle f_zblur f_blurmocurves f_pseudocolor f_psykoblobs f_psykostripes
         f_ascii f_rollingbar f_zdefocus f_lensflare f_timewarprgb f_timedisplace f_motiondetect
         f_freezeframe f_jitterframes f_reverseedits
)";

    static double parse_number(std::string_view s)
    {
        double v = 0.0;
        const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
        if (ec != std::errc{} || end != s.data() + s.size())
        {
            throw std::invalid_argument(std::format("not a number: '{}'", s));
        }
        return v;
    }

    static options parse(int argc, char** argv)
    {
        const std::vector<std::string_view> args(argv + 1, argv + argc);
        if (args.empty())
        {
            throw std::invalid_argument("missing effect name");
        }
        options o;
        o.effect = args[0];
        for (std::size_t i = 1; i < args.size(); ++i)
        {
            const std::string_view a = args[i];
            const auto next = [&]() -> std::string_view {
                if (i + 1 >= args.size())
                {
                    throw std::invalid_argument(std::format("{} needs a value", a));
                }
                return args[++i];
            };
            if (a == "--in")
            {
                o.in = next();
            }
            else if (a == "--out")
            {
                o.out = next();
            }
            else if (a == "--time")
            {
                o.time = parse_number(next());
            }
            else if (a == "--fps")
            {
                o.fps = parse_number(next());
            }
            else if (a == "--params")
            {
                o.list_params = true;
            }
            else if (a == "--size")
            {
                const std::string_view s = next();
                const auto x = s.find('x');
                if (x == std::string_view::npos)
                {
                    throw std::invalid_argument("--size expects WxH");
                }
                o.width = static_cast<int>(parse_number(s.substr(0, x)));
                o.height = static_cast<int>(parse_number(s.substr(x + 1)));
            }
            else if (const auto eq = a.find('='); eq != std::string_view::npos)
            {
                o.assignments.emplace_back(a.substr(0, eq), a.substr(eq + 1));
            }
            else
            {
                throw std::invalid_argument(std::format("unknown argument '{}'", a));
            }
        }
        if (o.width <= 0 || o.height <= 0 || o.fps <= 0.0)
        {
            throw std::invalid_argument("size and fps must be positive");
        }
        if (o.out.empty())
        {
            o.out = o.effect + ".ppm";
        }
        return o;
    }

    static void print_params(const core::effect& fx)
    {
        static constexpr std::array KIND_NAMES{"scalar", "toggle", "choice", "color", "point", "group", "group end"};
        std::cout << std::format("## {}\n\n| Label | Name | Kind | Default | Range | Slider |\n", fx.name)
                  << "|---|---|---|---|---|---|\n";
        for (const core::param& p : fx.params)
        {
            if (p.kind == core::param_kind::group_end)
            {
                continue;
            }
            const bool vector = p.kind == core::param_kind::color || p.kind == core::param_kind::point;
            const std::string def =
                vector ? std::format("{}, {}, {}", p.def[0], p.def[1], p.def[2]) : std::format("{}", p.def[0]);
            std::string range = std::format("{} .. {}", p.min, p.max);
            if (p.kind == core::param_kind::choice)
            {
                // '|' would split the markdown cell
                range = p.choices;
                std::ranges::replace(range, '|', '/');
            }
            std::cout << std::format("| {} | `{}` | {} | {} | {} | {} .. {} |\n", p.label, p.name,
                                     KIND_NAMES[static_cast<int>(p.kind)], def, range, p.slider_min, p.slider_max);
        }
    }

    static void apply_assignments(const core::effect& fx, const options& o, core::param_values& values)
    {
        for (const auto& [name, text] : o.assignments)
        {
            const auto it = std::ranges::find(fx.params, name, &core::param::name);
            if (it == fx.params.end())
            {
                throw std::invalid_argument(std::format("{} has no parameter '{}'", fx.name, name));
            }
            core::param_values::value v{};
            std::string_view rest = text;
            for (std::size_t c = 0; c < v.size() && !rest.empty(); ++c)
            {
                const auto comma = rest.find(',');
                v[c] = parse_number(rest.substr(0, comma));
                rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
            }
            if (it->kind != core::param_kind::color && it->kind != core::param_kind::point)
            {
                v = {v[0], v[0], v[0]};
            }
            values.set(static_cast<std::size_t>(it - fx.params.begin()), v);
        }
    }

    // grey ramp with bright spots and a checker, enough to judge every effect; the spots drift right `frame` frames
    // from now
    static core::buffer test_card(int width, int height, int frame)
    {
        core::buffer card(core::rect{0, 0, width, height}, 4);
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const double u = (x + 0.5) / width;
                const double v = (y + 0.5) / width; // square units so spots are round
                const bool checker = ((x / 40) + (y / 40)) % 2 == 0;
                const double drift = 0.02 * frame;
                const double spot = std::exp(-std::pow(std::hypot(u - 0.35 - drift, v - 0.22) / 0.06, 2.0));
                const double spot2 = std::exp(-std::pow(std::hypot(u - 0.7 - drift, v - 0.34) / 0.03, 2.0));
                float* p = card.view().at(x, y);
                const double base = 0.15 + 0.25 * u + (checker ? 0.08 : 0.0);
                p[0] = static_cast<float>(base + spot + 0.3 * spot2);
                p[1] = static_cast<float>(base * 0.9 + 0.9 * spot + spot2);
                p[2] = static_cast<float>(base * 1.1 + 0.6 * spot + 0.5 * spot2);
                p[3] = 1.0f;
            }
        }
        return card;
    }

    static core::buffer read_ppm(const std::string& path)
    {
        std::ifstream f(path, std::ios::binary);
        std::string magic;
        int w = 0;
        int h = 0;
        int maxval = 0;
        f >> magic >> w >> h >> maxval;
        f.get();
        if (!f || magic != "P6" || w <= 0 || h <= 0 || maxval != 255)
        {
            throw std::runtime_error(std::format("{}: expected an 8-bit binary P6 PPM", path));
        }
        std::vector<unsigned char> bytes(static_cast<std::size_t>(w) * h * 3);
        f.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!f)
        {
            throw std::runtime_error(std::format("{}: truncated pixel data", path));
        }
        core::buffer img(core::rect{0, 0, w, h}, 4);
        for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h; ++i)
        {
            float* p = img.view().data + i * 4;
            std::ranges::transform(bytes.begin() + static_cast<std::ptrdiff_t>(i * 3),
                                   bytes.begin() + static_cast<std::ptrdiff_t>(i * 3 + 3), p,
                                   [](unsigned char b) { return b / 255.0f; });
            p[3] = 1.0f;
        }
        return img;
    }

    // the first run of '#' in `pattern` replaced by the zero-padded frame number
    static std::string frame_path(std::string pattern, int frame)
    {
        const auto first = pattern.find('#');
        if (first == std::string::npos)
        {
            return pattern;
        }
        const auto last = pattern.find_first_not_of('#', first);
        const std::size_t width = (last == std::string::npos ? pattern.size() : last) - first;
        return pattern.replace(first, width, std::format("{:0{}}", frame, width));
    }

    // premultiplied rgb is written as-is, i.e. composited over black
    static void write_ppm(const std::string& path, const core::image& img)
    {
        std::ofstream f(path, std::ios::binary);
        f << std::format("P6\n{} {}\n255\n", img.bounds.width(), img.bounds.height());
        std::vector<unsigned char> row(static_cast<std::size_t>(img.bounds.width()) * 3);
        for (int y = img.bounds.y0; y < img.bounds.y1; ++y)
        {
            for (int x = img.bounds.x0; x < img.bounds.x1; ++x)
            {
                const float* p = img.at(x, y);
                for (int c = 0; c < 3; ++c)
                {
                    row[static_cast<std::size_t>(x - img.bounds.x0) * 3 + c] =
                        static_cast<unsigned char>(std::lround(std::clamp(p[c], 0.0f, 1.0f) * 255.0f));
                }
            }
            f.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
        }
        if (!f)
        {
            throw std::runtime_error(std::format("{}: write failed", path));
        }
    }

    static int run(int argc, char** argv)
    {
        const options o = parse(argc, argv);
        const core::effect* fx = effects::catalog::find(o.effect);
        if (fx == nullptr)
        {
            throw std::invalid_argument(std::format("unknown effect '{}'", o.effect));
        }
        if (o.list_params)
        {
            print_params(*fx);
            return 0;
        }
        core::param_values values(fx->params);
        apply_assignments(*fx, o, values);

        core::render_args args;
        args.time = o.time;
        args.frame_duration = 1.0 / o.fps;
        const int frame = args.frame_number();
        const core::buffer src = o.in.empty() ? test_card(o.width, o.height, 0) : read_ppm(frame_path(o.in, frame));
        const std::vector<int> offsets =
            fx->frame_offsets != nullptr ? fx->frame_offsets(values, frame) : std::vector<int>{};
        args.frame_at = [&](int offset) {
            if (std::ranges::find(offsets, offset) == offsets.end())
            {
                return core::buffer{};
            }
            const std::string path = frame_path(o.in, frame + offset);
            return o.in.empty()                    ? test_card(o.width, o.height, offset)
                   : std::filesystem::exists(path) ? read_ppm(path)
                                                   : core::buffer{};
        };
        const core::rect bounds = src.view().bounds;
        core::buffer dst(bounds, 4);
        args.src = src.view();
        args.dst = dst.view();
        args.params = &values;
        args.geometry = core::frame{bounds.width() * 0.5, bounds.height() * 0.5, static_cast<double>(bounds.width())};
        fx->render(args);
        write_ppm(o.out, dst.view());
        return 0;
    }
} // namespace functional_fx_suite::tools::fxrender

int main(int argc, char** argv)
{
    try
    {
        return functional_fx_suite::tools::fxrender::run(argc, argv);
    }
    catch (const std::exception& e)
    {
        std::cerr << "fxrender: " << e.what() << "\n\n" << functional_fx_suite::tools::fxrender::USAGE;
        return 1;
    }
}
