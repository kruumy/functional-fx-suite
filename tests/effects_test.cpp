#include "core/blur.hpp"
#include "core/bokeh.hpp"
#include "core/buffer.hpp"
#include "core/color.hpp"
#include "core/fft.hpp"
#include "core/flare.hpp"
#include "core/param_values.hpp"
#include "core/warp.hpp"
#include "effects/catalog.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <format>
#include <iostream>
#include <numbers>
#include <numeric>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace functional_fx_suite::tests::effects_test
{
    static int failures = 0;

    static void check(bool ok, std::string_view what)
    {
        if (!ok)
        {
            ++failures;
            std::cerr << "FAIL: " << what << '\n';
        }
    }

    static core::buffer card(int w, int h)
    {
        core::buffer b(core::rect{0, 0, w, h}, 4);
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                float* p = b.view().at(x, y);
                p[0] = static_cast<float>(x) / w;
                p[1] = static_cast<float>(y) / h;
                p[2] = (x / 8 + y / 8) % 2 == 0 ? 0.9f : 0.1f;
                p[3] = 1.0f;
            }
        }
        return b;
    }

    static core::buffer solid(core::rect r, float value)
    {
        core::buffer b(r, 4);
        std::fill_n(b.view().data, static_cast<std::size_t>(r.width()) * r.height() * 4, value);
        return b;
    }

    struct run_result
    {
        core::buffer out;
        double max_diff = 0; // against the source
        double sum_diff = 0; // signed, rgb only
    };

    // the source at another frame, as a test hands it to run()
    struct offset_frame
    {
        int offset = 0;
        core::image src;
    };

    // the offsets the last run() fetched, in order
    static std::vector<int> fetched;

    template <class Setup>
    static run_result run(std::string_view name, const core::image& src, Setup&& setup, double time = 0.5,
                          const core::image* mask = nullptr, std::span<const offset_frame> frames = {})
    {
        const core::effect& fx = *effects::catalog::find(name);
        core::param_values values(fx.params);
        setup(fx, values);
        run_result r{core::buffer(src.bounds, 4)};
        core::render_args args;
        args.src = src;
        args.dst = r.out.view();
        args.mask = mask;
        fetched.clear();
        args.frame_at = [frames](int offset) {
            fetched.push_back(offset);
            core::buffer b;
            const auto it = std::ranges::find(frames, offset, &offset_frame::offset);
            if (it != frames.end())
            {
                const core::image& f = it->src;
                b.reset(f.bounds, 4);
                std::copy_n(f.data, static_cast<std::size_t>(f.bounds.width()) * f.bounds.height() * 4, b.view().data);
            }
            return b;
        };
        args.params = &values;
        args.geometry =
            core::frame{src.bounds.width() * 0.5, src.bounds.height() * 0.5, static_cast<double>(src.bounds.width())};
        args.time = time;
        fx.render(args);
        for (int y = src.bounds.y0; y < src.bounds.y1; ++y)
        {
            for (int x = src.bounds.x0; x < src.bounds.x1; ++x)
            {
                for (int c = 0; c < 4; ++c)
                {
                    const double d = r.out.view().at(x, y)[c] - src.at(x, y)[c];
                    r.max_diff = std::max(r.max_diff, std::abs(d));
                    r.sum_diff += c < 3 ? d : 0.0;
                }
            }
        }
        return r;
    }

    static void set(const core::effect& fx, core::param_values& v, std::string_view name, double value)
    {
        const auto it = std::ranges::find(fx.params, name, &core::param::name);
        v.set(static_cast<std::size_t>(it - fx.params.begin()), {value, value, value});
    }

    static void param_tables()
    {
        for (const core::effect* fx : effects::catalog::all())
        {
            std::set<int> ids;
            std::set<std::string_view> names;
            for (const core::param& p : fx->params)
            {
                check(ids.insert(p.id).second, std::format("{}: duplicate id {}", fx->name, p.id));
                check(names.insert(p.name).second, std::format("{}: duplicate name {}", fx->name, p.name));
                if (p.kind == core::param_kind::scalar)
                {
                    check(p.min <= p.def[0] && p.def[0] <= p.max,
                          std::format("{}: {} default out of range", fx->name, p.name));
                    check(p.min <= p.slider_min && p.slider_max <= p.max,
                          std::format("{}: {} slider outside range", fx->name, p.name));
                }
                if (p.enable_param >= 0)
                {
                    check(fx->params[p.enable_param].kind == core::param_kind::choice,
                          std::format("{}: {} enabled by a non-choice", fx->name, p.name));
                }
            }
        }
    }

    static void identities()
    {
        const core::buffer src = card(64, 48);
        const auto none = [](const core::effect&, core::param_values&) {};

        check(run("f_tint", src.view(), none).max_diff < 1e-5, "f_tint defaults are identity");
        check(run("f_shake", src.view(),
                  [](const core::effect& fx, core::param_values& v) {
                      set(fx, v, "amplitude", 0.0);
                  }).max_diff < 1e-5,
              "f_shake with zero amplitude is identity");
        check(run("f_rays", src.view(),
                  [](const core::effect& fx, core::param_values& v) {
                      set(fx, v, "threshold", 10.0);
                  }).max_diff < 1e-5,
              "f_rays above every value is identity");
        for (const std::string_view name : {"f_glow", "f_ultraglow", "f_glowaura", "f_glowrings", "f_glint"})
        {
            check(run(name, src.view(),
                      [](const core::effect& fx, core::param_values& v) {
                          set(fx, v, "threshold", 10.0);
                      }).max_diff < 1e-5,
                  std::format("{} above every value is identity", name));
        }
        check(run("f_glowdark", src.view(),
                  [](const core::effect& fx, core::param_values& v) {
                      set(fx, v, "threshold", 0.0);
                  }).max_diff < 1e-5,
              "f_glowdark with zero threshold is identity");
    }

    static void glows()
    {
        const core::buffer src = card(64, 48);
        const auto none = [](const core::effect&, core::param_values&) {};
        for (const std::string_view name : {"f_glow", "f_ultraglow", "f_glowrings", "f_glint"})
        {
            const double a = run(name, src.view(), none).sum_diff;
            check(a > 1.0, std::format("{} brightens by default", name));
            check(a == run(name, src.view(), none).sum_diff, std::format("{} is repeatable", name));
        }
        for (const std::string_view name : {"f_glow", "f_ultraglow", "f_glowaura", "f_glowdark", "f_glowrings"})
        {
            const auto show = [](const core::effect& fx, core::param_values& v) { set(fx, v, "show", 1.0); };
            check(std::abs(run(name, src.view(), show).out.view().at(5, 5)[3] - 1.0f) < 1e-6,
                  std::format("{} Show Threshold is opaque", name));
        }
        check(run("f_glowaura", src.view(), none).max_diff > 0.05, "f_glowaura draws an aura by default");
        check(run("f_glowaura", src.view(), none, 0.0).sum_diff != run("f_glowaura", src.view(), none, 0.5).sum_diff,
              "f_glowaura phase moves over time");
        check(run("f_glowdark", src.view(), none).sum_diff < -1.0, "f_glowdark darkens by default");
        check(run("f_ultraglow", src.view(),
                  [](const core::effect& fx, core::param_values& v) {
                      set(fx, v, "edge_detect", 1.0);
                      set(fx, v, "atmosphere", 1.0);
                      set(fx, v, "glow_bias", 2.0);
                  }).sum_diff > 1.0,
              "f_ultraglow edges, atmosphere and bias render");
        check(run("f_ultraglow", src.view(),
                  [](const core::effect& fx, core::param_values& v) {
                      set(fx, v, "atmosphere", 1.0);
                      set(fx, v, "apply_pre_glow", 1.0);
                      set(fx, v, "glow_bias", -2.0);
                  }).max_diff > 0.0,
              "f_ultraglow pre-glow atmosphere and negative bias render");
        check(run("f_glint", src.view(),
                  [](const core::effect& fx, core::param_values& v) {
                      set(fx, v, "hue_shift", 0.3);
                      set(fx, v, "blur_glint", 0.05);
                  }).sum_diff > 1.0,
              "f_glint hue shift and blur render");
        for (int mode = 0; mode < 5; ++mode)
        {
            check(run("f_glow", src.view(),
                      [mode](const core::effect& fx, core::param_values& v) {
                          set(fx, v, "combine", mode);
                          set(fx, v, "light_background", 0.5);
                          set(fx, v, "glow_under_source", 0.5);
                      }).max_diff > 0.0,
                  std::format("f_glow combine {} renders", mode));
        }

        // reflected edges keep a flat image flat; transparent edges fade it
        const core::buffer flat = solid(core::rect{0, 0, 32, 32}, 0.5f);
        core::blur::gaussian(flat.view(), 6.0, 6.0, core::blur::edge::reflect);
        check(std::abs(flat.view().at(0, 0)[0] - 0.5f) < 1e-5, "reflected blur keeps edges");
        core::blur::gaussian(flat.view(), 6.0, 6.0);
        check(flat.view().at(0, 0)[0] < 0.4f, "transparent blur fades edges");

        const core::color::rgb c{0.8f, 0.3f, 0.1f};
        const core::color::rgb turned = core::color::hue_shift_preserve_luma(c, 1.0f);
        check(std::abs(turned[0] - c[0]) + std::abs(turned[1] - c[1]) + std::abs(turned[2] - c[2]) < 1e-4,
              "a full hue turn is identity");
        const core::color::rgb shifted = core::color::hue_shift_preserve_luma(c, 0.4f);
        check(std::abs(core::color::sat_luma(shifted) - core::color::sat_luma(c)) < 1e-4 && shifted[2] > c[2],
              "hue shift moves the hue and keeps luma");
    }

    static void tones_and_edges()
    {
        const core::buffer src = card(64, 48);
        const auto none = [](const core::effect&, core::param_values&) {};
        const auto mix = [](const core::effect& fx, core::param_values& v) { set(fx, v, "mix_with_source", 1.0); };

        check(run("f_hotspots", src.view(),
                  [](const core::effect& fx, core::param_values& v) {
                      set(fx, v, "threshold", 0.0);
                  }).max_diff < 1e-5,
              "f_hotspots with zero threshold is identity");
        check(run("f_hotspots", src.view(), none).sum_diff < -1.0, "f_hotspots keeps only the bright areas");
        check(run("f_duotone", src.view(), mix).max_diff < 1e-5, "f_duotone fully mixed with the source is identity");
        check(run("f_tritone", src.view(), mix).max_diff < 1e-5, "f_tritone fully mixed with the source is identity");
        check(run("f_badcolors", src.view(), none).max_diff < 1e-6, "f_badcolors passes legal colors through");
        check(run("f_badcolors", src.view(),
                  [](const core::effect& fx, core::param_values& v) {
                      set(fx, v, "max", 0.5);
                  }).max_diff > 0.5,
              "f_badcolors flags colors above Max");
        check(run("f_vignette", src.view(),
                  [](const core::effect& fx, core::param_values& v) {
                      set(fx, v, "opacity", 0.0);
                  }).max_diff < 1e-5,
              "f_vignette with zero opacity is identity");
        const run_result vignette = run("f_vignette", src.view(), none);
        check(vignette.sum_diff < -1.0 && std::abs(vignette.out.view().at(32, 24)[0] - src.view().at(32, 24)[0]) < 1e-5,
              "f_vignette darkens the border and keeps the centre");
        check(run("f_vignette", src.view(),
                  [](const core::effect& fx, core::param_values& v) {
                      set(fx, v, "blur_amount", 1.0);
                      set(fx, v, "opacity", 0.0);
                  }).max_diff > 0.05,
              "f_vignette blurs the border");

        // the default ramps map an opaque gray to its own level
        core::buffer gray(core::rect{0, 0, 16, 16}, 4);
        for (int i = 0; i < 16 * 16 * 4; ++i)
        {
            gray.view().data[i] = i % 4 == 3 ? 1.0f : 0.3f;
        }
        const auto black_to_white = [](const core::effect& fx, core::param_values& v) {
            set(fx, v, "color0", 0.0);
            set(fx, v, "color1", 1.0);
        };
        check(std::abs(run("f_duotone", gray.view(), black_to_white).out.view().at(3, 3)[2] - 0.3f) < 1e-5,
              "f_duotone black to white ramp keeps gray levels");
        check(std::abs(run("f_tritone", gray.view(), none).out.view().at(3, 3)[2] - 0.3f) < 1e-5,
              "f_tritone default ramp keeps gray levels");

        // a step from black to white: the edge strength is (right - left) x scale x 0.05 x width
        core::buffer step(core::rect{0, 0, 64, 16}, 4);
        for (int y = 0; y < 16; ++y)
        {
            for (int x = 0; x < 64; ++x)
            {
                float* q = step.view().at(x, y);
                std::fill_n(q, 3, x >= 32 ? 1.0f : 0.0f);
                q[3] = 1.0f;
            }
        }
        const auto sharp = [](int style) {
            return [style](const core::effect& fx, core::param_values& v) {
                set(fx, v, "edge_smooth", 0.0);
                set(fx, v, "style", style);
            };
        };
        for (int style = 0; style < 3; ++style)
        {
            const run_result e = run("f_edgedetect", step.view(), sharp(style));
            const float expected = style == 1 ? 0.0f : 0.1f * 0.05f * 64.0f;
            check(std::abs(e.out.view().at(31, 8)[0] - expected) < 1e-4 && std::abs(e.out.view().at(10, 8)[0]) < 1e-6,
                  std::format("f_edgedetect style {} edge strength", style));
        }
        check(run("f_edgedetect", src.view(), none).max_diff > 0.1, "f_edgedetect finds edges in the card");
        const run_result colorized =
            run("f_edgecolorize", step.view(),
                [](const core::effect& fx, core::param_values& v) { set(fx, v, "edge_smooth", 0.0); });
        check(std::abs(colorized.out.view().at(31, 8)[0] - 0.5f * 0.32f) < 1e-4 &&
                  std::abs(colorized.out.view().at(10, 8)[0]) < 1e-6,
              "f_edgecolorize colors a left-facing edge with Left");

        const double rays = run("f_edgerays", src.view(), none).sum_diff;
        check(rays > 1.0, "f_edgerays brightens by default");
        check(rays == run("f_edgerays", src.view(), none).sum_diff, "f_edgerays is repeatable");
        for (int show = 1; show < 3; ++show)
        {
            const auto shown = [show](const core::effect& fx, core::param_values& v) { set(fx, v, "show", show); };
            check(run("f_edgerays", src.view(), shown).out.view().at(5, 5)[3] == 1.0f,
                  std::format("f_edgerays show {} is opaque", show));
        }
        check(run("f_edgerays", src.view(),
                  [](const core::effect& fx, core::param_values& v) {
                      set(fx, v, "enable_dark_rays", 1.0);
                      set(fx, v, "rays_color", 0.0);
                  }).sum_diff < -1.0,
              "f_edgerays dark rays darken");
    }

    static void stylize()
    {
        const core::buffer src = card(64, 48);
        const auto none = [](const core::effect&, core::param_values&) {};
        const auto mix = [](const core::effect& fx, core::param_values& v) { set(fx, v, "mix_with_source", 1.0); };

        check(run("f_mosaic", src.view(), mix).max_diff < 1e-6, "f_mosaic fully mixed with the source is identity");
        const run_result mosaic = run("f_mosaic", src.view(), [](const core::effect& fx, core::param_values& v) {
            set(fx, v, "pixel_frequency", 8.0);
        });
        check(mosaic.max_diff > 0.05 && mosaic.out.view().at(1, 1)[0] == mosaic.out.view().at(6, 6)[0] &&
                  mosaic.out.view().at(1, 1)[0] != mosaic.out.view().at(9, 1)[0],
              "f_mosaic fills each cell with one color");

        // with flat lines the dark and light halves average back to the source
        check(run("f_scanlines", src.view(),
                  [](const core::effect& fx, core::param_values& v) {
                      set(fx, v, "lines_sharpness", 0.0);
                  }).max_diff < 1e-5,
              "f_scanlines with zero sharpness is identity");
        check(run("f_scanlines", src.view(), none).max_diff > 0.1, "f_scanlines draws lines by default");

        const core::buffer white = solid(core::rect{0, 0, 32, 24}, 1.0f);
        core::buffer black = solid(core::rect{0, 0, 32, 24}, 0.0f);
        for (int i = 3; i < 32 * 24 * 4; i += 4)
        {
            black.view().data[i] = 1.0f;
        }
        for (const std::string_view name : {"f_halftone", "f_halftonecolor"})
        {
            check(run(name, white.view(), none).max_diff < 1e-6, std::format("{} keeps white white", name));
            check(run(name, black.view(), none).max_diff < 1e-6, std::format("{} keeps black black", name));
            check(run(name, src.view(), none).max_diff > 0.5, std::format("{} draws dots", name));
        }

        check(run("f_flicker", src.view(),
                  [](const core::effect& fx, core::param_values& v) {
                      set(fx, v, "amplitude", 0.0);
                  }).max_diff < 1e-6,
              "f_flicker with zero amplitude is identity");
        check(run("f_flicker", src.view(), none, 0.5).sum_diff != run("f_flicker", src.view(), none, 0.9).sum_diff,
              "f_flicker changes over time");
        for (const double time : {0.1, 0.3, 0.5, 0.7, 0.9})
        {
            const auto direction = [](double d) {
                return [d](const core::effect& fx, core::param_values& v) { set(fx, v, "direction", d); };
            };
            check(run("f_flicker", src.view(), direction(1.0), time).sum_diff >= 0.0,
                  "f_flicker Brighter never darkens");
            check(run("f_flicker", src.view(), direction(2.0), time).sum_diff <= 0.0,
                  "f_flicker Darker never brightens");
        }

        check(run("f_jpegdamage", src.view(),
                  [](const core::effect& fx, core::param_values& v) {
                      set(fx, v, "quality", 1.0);
                  }).max_diff < 0.02,
              "f_jpegdamage at full quality is near identity");
        check(run("f_jpegdamage", src.view(), none).max_diff > 0.05, "f_jpegdamage compresses by default");
        const auto errors = [](const core::effect& fx, core::param_values& v) { set(fx, v, "error_rate", 1.0); };
        const double damaged = run("f_jpegdamage", src.view(), errors).sum_diff;
        check(damaged == run("f_jpegdamage", src.view(), errors).sum_diff &&
                  damaged != run("f_jpegdamage", src.view(), errors, 2.0).sum_diff,
              "f_jpegdamage errors are repeatable and jitter over frames");

        check(run("f_pixelsort", src.view(),
                  [](const core::effect& fx, core::param_values& v) {
                      set(fx, v, "threshold", 2.0);
                  }).max_diff < 1e-6,
              "f_pixelsort with nothing above the threshold is identity");
        // hard mask and no restarts: every row is only permuted
        for (int style = 0; style < 3; ++style)
        {
            const run_result sorted =
                run("f_pixelsort", src.view(), [style](const core::effect& fx, core::param_values& v) {
                    set(fx, v, "style", style);
                    set(fx, v, "soften_threshold_mask", 0.0);
                    set(fx, v, "randomly_restart_sort", 0.0);
                });
            check(sorted.max_diff > 0.1 && std::abs(sorted.sum_diff) < 1e-2,
                  std::format("f_pixelsort style {} only reorders pixels", style));
        }
    }

    static void damage()
    {
        const core::buffer src = card(64, 48);
        const auto none = [](const core::effect&, core::param_values&) {};
        const auto changed_over_time = [&](std::string_view name) {
            double total = 0.0;
            for (double t = 0.0; t < 2.0; t += 0.125)
            {
                total += run(name, src.view(), none, t).max_diff;
            }
            return total > 0.1;
        };

        // with Time Intensity 0 no stage ever blinks on
        check(run("f_digitaldamage", src.view(),
                  [](const core::effect& fx, core::param_values& v) {
                      set(fx, v, "time_intensity", 0.0);
                  }).max_diff < 1e-6,
              "f_digitaldamage with Time Intensity 0 is identity");
        check(changed_over_time("f_digitaldamage"), "f_digitaldamage damages some frames by default");
        check(run("f_digitaldamage", src.view(), none, 1.25).sum_diff ==
                  run("f_digitaldamage", src.view(), none, 1.25).sum_diff,
              "f_digitaldamage is repeatable");

        // no reception and flat scan lines leave only the dissolve(darks, lights, 0.5) identity
        for (int style = 0; style < 2; ++style)
        {
            const core::buffer gray = solid(src.view().bounds, 0.4f);
            check(run("f_tvdamage", (style == 0 ? src : gray).view(),
                      [style](const core::effect& fx, core::param_values& v) {
                          set(fx, v, "style", style);
                          set(fx, v, "reception_master", 0.0);
                          set(fx, v, "scanlines", 0.0);
                      }).max_diff < 1e-5,
                  std::format("f_tvdamage style {} without reception is identity", style));
        }
        check(changed_over_time("f_tvdamage"), "f_tvdamage damages the picture by default");
        check(run("f_tvdamage", src.view(), none, 1.25).sum_diff == run("f_tvdamage", src.view(), none, 1.25).sum_diff,
              "f_tvdamage is repeatable");
        const run_result off = run("f_tvdamage", src.view(),
                                   [](const core::effect& fx, core::param_values& v) { set(fx, v, "turn_off", 1.0); });
        check(off.out.view().at(32, 24)[0] == 0.0f && off.out.view().at(3, 3)[1] == 0.0f,
              "f_tvdamage fully turned off is black");

        check(run("f_filmdamage", src.view(),
                  [](const core::effect& fx, core::param_values& v) {
                      for (const std::string_view name : {"grain_amp", "stain_density", "dust_density", "hairs",
                                                          "scratches", "vignette_darkness", "flicker"})
                      {
                          set(fx, v, name, 0.0);
                      }
                  }).max_diff < 1e-6,
              "f_filmdamage with every layer off is identity");
        check(changed_over_time("f_filmdamage"), "f_filmdamage damages the film by default");
        check(run("f_filmdamage", src.view(), none, 1.25).sum_diff ==
                  run("f_filmdamage", src.view(), none, 1.25).sum_diff,
              "f_filmdamage is repeatable");
        check(run("f_filmdamage", src.view(),
                  [](const core::effect& fx, core::param_values& v) {
                      set(fx, v, "shake_amplitude", 1.0);
                      set(fx, v, "shake_always", 1.0);
                      set(fx, v, "defocus", 1.0);
                      set(fx, v, "grain_blur", 0.1);
                      set(fx, v, "grain_hold", 3.0);
                  }).max_diff > 0.05,
              "f_filmdamage shake, defocus and blurred grain render");
    }

    static void behaviour()
    {
        const core::buffer src = card(64, 48);
        const auto none = [](const core::effect&, core::param_values&) {};

        check(run("f_shake", src.view(), none).max_diff > 0.05, "f_shake moves the image by default");
        // normal-style motion is continuous: nearby times give nearly the same frame
        for (double t = 0.0; t < 3.0; t += 0.0625)
        {
            const double near = std::abs(run("f_shake", src.view(), none, t).sum_diff -
                                         run("f_shake", src.view(), none, t + 1e-4).sum_diff);
            check(near < 1.0, std::format("f_shake is continuous at t={}", t));
        }
        const double a = run("f_shake", src.view(), none, 1.25).sum_diff;
        check(a == run("f_shake", src.view(), none, 1.25).sum_diff, "f_shake is repeatable");
        check(a != run(
                       "f_shake", src.view(),
                       [](const core::effect& fx, core::param_values& v) { set(fx, v, "seed", 3.0); }, 1.25)
                       .sum_diff,
              "f_shake seed changes the motion");
        check(run("f_shake", src.view(),
                  [](const core::effect& fx, core::param_values& v) {
                      set(fx, v, "red_phase", 0.05);
                      set(fx, v, "motion_blur", 1.0);
                  }).max_diff > 0.0,
              "f_shake per-channel path with motion blur renders");
        for (int s = 1; s <= 2; ++s)
        {
            check(run("f_shake", src.view(),
                      [s](const core::effect& fx, core::param_values& v) {
                          set(fx, v, "style", s);
                          set(fx, v, "stillness", 0.0);
                      }).max_diff > 0.0,
                  std::format("f_shake style {} renders", s));
        }

        check(run("f_rays", src.view(), none).sum_diff > 1.0, "f_rays brightens by default");
        check(run("f_rays", src.view(),
                  [](const core::effect& fx, core::param_values& v) {
                      set(fx, v, "style", 1.0);
                  }).sum_diff < 0.0,
              "f_rays dark style darkens");
        check(run("f_rays", src.view(),
                  [](const core::effect& fx, core::param_values& v) {
                      set(fx, v, "rays_res", 2.0);
                      set(fx, v, "blur_rays", 0.1);
                      set(fx, v, "atmosphere_amp", 0.5);
                      set(fx, v, "length_red", 2.0);
                  }).sum_diff > 0.0,
              "f_rays quarter res, blur, atmosphere and channel lengths render");

        check(run("f_tint", src.view(),
                  [](const core::effect& fx, core::param_values& v) {
                      set(fx, v, "source_saturation", 0.0);
                  }).max_diff > 0.05,
              "f_tint desaturates");
    }

    static void masks()
    {
        const core::buffer src = card(64, 48);
        const core::buffer white = solid(src.view().bounds, 1.0f);
        const core::buffer black = solid(src.view().bounds, 0.0f);
        const auto tint_red = [](const core::effect& fx, core::param_values& v) {
            v.set(0, {1.0, 0.2, 0.2});
            set(fx, v, "blur_mask", 0.0);
        };
        const double unmasked = run("f_tint", src.view(), tint_red).sum_diff;
        check(std::abs(run("f_tint", src.view(), tint_red, 0.5, &white.view()).sum_diff - unmasked) < 1e-3,
              "white mask keeps the full effect");
        check(run("f_tint", src.view(), tint_red, 0.5, &black.view()).max_diff < 1e-6, "black mask removes the effect");

        for (int matte_type = 0; matte_type < 3; ++matte_type)
        {
            const auto matte = [matte_type](const core::effect& fx, core::param_values& v) {
                set(fx, v, "matte_type", matte_type);
            };
            check(run("f_rays", src.view(), matte, 0.5, &black.view()).max_diff < 1e-6,
                  std::format("f_rays black matte (type {}) blocks the rays", matte_type));
            check(std::abs(run("f_rays", src.view(), matte, 0.5, &white.view()).sum_diff -
                           run("f_rays", src.view(), matte).sum_diff) < 1e-3,
                  std::format("f_rays white matte (type {}) keeps the rays", matte_type));
        }
    }

    static void warps()
    {
        const core::buffer src = card(64, 48);
        const core::buffer black = solid(src.view().bounds, 0.0f);
        const auto none = [](const core::effect&, core::param_values&) {};
        const auto zero = [](std::string_view param) {
            return [param](const core::effect& fx, core::param_values& v) { set(fx, v, param, 0.0); };
        };

        for (const auto& [name, param] :
             {std::pair{"f_distort", "amount"}, std::pair{"f_distortrgb", "amount"},
              std::pair{"f_distortchroma", "amount"}, std::pair{"f_warpfisheye", "amount"},
              std::pair{"f_warppuddle", "amplitude"}, std::pair{"f_warpchroma", "warp_amount"}})
        {
            check(run(name, src.view(), zero(param)).max_diff < 1e-5,
                  std::format("{} with zero {} is identity", name, param));
            check(run(name, src.view(), none).max_diff > 0.05, std::format("{} warps by default", name));
        }
        check(run("f_distortblur", src.view(), zero("blur_amount")).max_diff < 1e-5,
              "f_distortblur with no blur or warp is identity");
        check(run("f_distortblur", src.view(), none).max_diff > 0.05, "f_distortblur blurs by default");
        check(run("f_distort", src.view(), none, 0.5, &black.view()).max_diff < 1e-5,
              "f_distort black matte removes the warp");
        check(run("f_warppuddle", src.view(), none, 0.0).sum_diff !=
                  run("f_warppuddle", src.view(), none, 0.25).sum_diff,
              "f_warppuddle waves move over time");
        const run_result rgb = run("f_distortrgb", src.view(), none);
        check(rgb.out.view().at(20, 20)[3] == src.view().at(20, 20)[3], "f_distortrgb leaves alpha alone");

        const std::array<std::array<double, 3>, 3> primaries{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
        const auto weights = core::warp::spectrum(8, primaries, false, 1.0);
        for (int c = 0; c < 3; ++c)
        {
            double sum = 0.0;
            for (const auto& w : weights)
            {
                sum += w[c];
            }
            check(std::abs(sum - 1.0) < 1e-5, std::format("default spectrum keeps channel {} at full weight", c));
        }

        // f_blurmocurves blurs along the motion of its own params between shutter open and close
        check(run("f_blurmocurves", src.view(), none).max_diff < 1e-5, "f_blurmocurves without motion is identity");
        const core::effect& curves = *effects::catalog::find("f_blurmocurves");
        core::param_values values(curves.params);
        core::buffer out(src.view().bounds, 4);
        core::render_args args;
        args.src = src.view();
        args.dst = out.view();
        args.params = &values;
        args.geometry = core::frame{32, 24, 64};
        args.time = 1.0;
        args.params_at = [&](double t) {
            core::param_values moved(curves.params);
            set(curves, moved, "rotate", (t - 1.0) * 240.0);
            return moved;
        };
        curves.render(args);
        double diff = 0.0;
        for (int x = 0; x < 64; ++x)
        {
            diff = std::max(diff, static_cast<double>(std::abs(out.view().at(x, 2)[2] - src.view().at(x, 2)[2])));
        }
        check(diff > 0.05, "f_blurmocurves blurs along animated rotation");
        // Shift 0.5 is a quarter of the frame width in xy units: 16 px here
        const auto shifted = [](bool blur_only) {
            return [blur_only](const core::effect& fx, core::param_values& v) {
                set(fx, v, "shift_x", 0.5);
                set(fx, v, "blur_only", blur_only ? 1.0 : 0.0);
            };
        };
        const run_result moved = run("f_blurmocurves", src.view(), shifted(false));
        check(std::abs(moved.out.view().at(40, 20)[0] - src.view().at(24, 20)[0]) < 1e-4f &&
                  std::abs(moved.out.view().at(40, 20)[2] - src.view().at(24, 20)[2]) < 1e-4f,
              "f_blurmocurves moves the clip by Shift");
        check(run("f_blurmocurves", src.view(), shifted(true)).max_diff < 1e-5,
              "f_blurmocurves Blur Only without motion leaves the clip in place");

        for (int mode = 0; mode < 2; ++mode)
        {
            check(run("f_zblur", src.view(),
                      [mode](const core::effect& fx, core::param_values& v) {
                          set(fx, v, "layer_mode", mode);
                          set(fx, v, "focal_depth", 1.0);
                      }).max_diff < 1e-5,
                  std::format("f_zblur (mode {}) keeps the focal depth sharp", mode));
            check(run("f_zblur", src.view(),
                      [mode](const core::effect& fx, core::param_values& v) {
                          set(fx, v, "layer_mode", mode);
                      }).max_diff > 0.05,
                  std::format("f_zblur (mode {}) blurs out of focus", mode));
        }
        const run_result zone =
            run("f_zblur", src.view(), [](const core::effect& fx, core::param_values& v) { set(fx, v, "show", 1.0); });
        check(std::abs(zone.out.view().at(5, 5)[0] - 0.6f * src.view().at(5, 5)[0]) < 1e-5,
              "f_zblur In Focus Zone darkens what is out of focus");

        // the FFT against a direct DFT on a length with every radix
        const core::fft::plan plan(60);
        std::vector<core::fft::complex> signal(60);
        std::vector<core::fft::complex> spectrum(60);
        for (int i = 0; i < 60; ++i)
        {
            signal[static_cast<std::size_t>(i)] = {std::sin(static_cast<float>(i) * 0.37f),
                                                   std::cos(static_cast<float>(i) * 1.1f)};
        }
        plan.run(signal, spectrum, false);
        double dft_err = 0.0;
        for (int k = 0; k < 60; ++k)
        {
            std::complex<double> sum{};
            for (int i = 0; i < 60; ++i)
            {
                sum += std::complex<double>(signal[static_cast<std::size_t>(i)]) *
                       std::polar(1.0, -2.0 * std::numbers::pi * i * k / 60.0);
            }
            dft_err = std::max(dft_err, std::abs(sum - std::complex<double>(spectrum[static_cast<std::size_t>(k)])));
        }
        check(dft_err < 1e-3, "fft matches a direct DFT");
        check(core::fft::good_size(61) == 64 && core::fft::good_size(1001) == 1024, "fft good sizes");

        // a centred one-pixel kernel leaves the image alone
        core::buffer delta(core::rect{-1, -1, 2, 2}, 1);
        *delta.view().at(0, 0) = 1.0f;
        const core::buffer convolved = card(64, 48);
        core::fft::convolve(convolved.view(), delta.view(), core::fft::edge::transparent);
        double conv_err = 0.0;
        for (int y = 0; y < 48; ++y)
        {
            for (int x = 0; x < 64; ++x)
            {
                for (int c = 0; c < 4; ++c)
                {
                    conv_err = std::max(
                        conv_err, static_cast<double>(std::abs(convolved.view().at(x, y)[c] - src.view().at(x, y)[c])));
                }
            }
        }
        check(conv_err < 1e-5, "fft convolution with a delta is identity");

        for (int sides : {0, 3, 6})
        {
            const core::buffer k = core::bokeh::kernel({.radius_x = 6.5,
                                                        .radius_y = 4.0,
                                                        .sides = sides,
                                                        .pointiness = 1.0,
                                                        .rotate = 10.0,
                                                        .bokeh = 0.5,
                                                        .noise_amp = 0.3});
            const core::image& kv = k.view();
            const float total = std::accumulate(kv.data, kv.data + (kv.bounds.width() * kv.bounds.height()), 0.0f);
            check(std::abs(total - 1.0f) < 1e-4 && kv.bounds.width() % 2 == 1 && kv.bounds.height() % 2 == 1,
                  std::format("bokeh kernel ({} sides) is odd sized and sums to 1", sides));
        }

        for (int mode = 0; mode < 2; ++mode)
        {
            check(run("f_zdefocus", src.view(),
                      [mode](const core::effect& fx, core::param_values& v) {
                          set(fx, v, "layer_mode", mode);
                          set(fx, v, "focal_depth", 1.0);
                      }).max_diff < 1e-5,
                  std::format("f_zdefocus (mode {}) keeps the focal depth sharp", mode));
            check(run("f_zdefocus", src.view(),
                      [mode](const core::effect& fx, core::param_values& v) {
                          set(fx, v, "layer_mode", mode);
                          set(fx, v, "shape", 4.0);
                      }).max_diff > 0.05,
                  std::format("f_zdefocus (mode {}) defocuses out of focus", mode));
        }
        const core::buffer gray = solid(core::rect{0, 0, 64, 48}, 0.4f);
        for (int edge = 0; edge < 3; ++edge)
        {
            const run_result flat =
                run("f_zdefocus", gray.view(),
                    [edge](const core::effect& fx, core::param_values& v) { set(fx, v, "edge_mode", edge); });
            const float* corner = flat.out.view().at(0, 0);
            const float* middle = flat.out.view().at(32, 24);
            check(std::abs(middle[0] - 0.4f) < 1e-4 &&
                      (edge == 0 ? corner[0] < 0.35f : std::abs(corner[0] - 0.4f) < 1e-4),
                  std::format("f_zdefocus (edge {}) keeps a flat image flat inside the frame", edge));
        }
        const run_result plain = run("f_zdefocus", src.view(), [](const core::effect&, core::param_values&) {});
        const run_result boosted = run("f_zdefocus", src.view(), [](const core::effect& fx, core::param_values& v) {
            set(fx, v, "boost_highlights", 1.0);
            set(fx, v, "highlight_threshold", 0.5);
        });
        check(boosted.sum_diff > plain.sum_diff + 1.0, "f_zdefocus Boost Highlights brightens");
        const run_result shape = run("f_zdefocus", src.view(),
                                     [](const core::effect& fx, core::param_values& v) { set(fx, v, "show", 2.0); });
        check(std::abs(shape.out.view().at(32, 24)[0] - 1.0f) < 1e-5 && shape.out.view().at(0, 0)[0] == 0.0f,
              "f_zdefocus Show Shape draws the lens at the frame centre");
    }

    static void background()
    {
        const core::buffer src = card(64, 48);
        // a half-transparent source so the background shows through
        for (int y = 0; y < 48; ++y)
        {
            for (int x = 0; x < 64; ++x)
            {
                float* p = src.view().at(x, y);
                std::for_each(p, p + 4, [](float& c) { c *= 0.5f; });
            }
        }
        const core::buffer bg = solid(src.view().bounds, 1.0f);
        const core::effect& fx = *effects::catalog::find("f_rays");
        core::param_values values(fx.params);
        set(fx, values, "threshold", 10.0); // no rays: output is just source over background
        core::buffer out(src.view().bounds, 4);
        core::render_args args;
        args.src = src.view();
        args.dst = out.view();
        args.background = &bg.view();
        args.params = &values;
        args.geometry = core::frame{32, 24, 64};
        fx.render(args);
        const float* s = src.view().at(10, 10);
        const float* o = out.view().at(10, 10);
        check(std::abs(o[0] - (s[0] + 0.5f)) < 1e-5 && std::abs(o[3] - 1.0f) < 1e-5,
              "f_rays composites the source over the background");
    }

    static void generators()
    {
        const core::buffer src = card(64, 48);
        const auto none = [](const core::effect&, core::param_values&) {};
        const auto mixed = [](const core::effect& fx, core::param_values& v) { set(fx, v, "mix_with_source", 1.0); };

        for (const std::string_view name : {"f_pseudocolor", "f_psykoblobs", "f_psykostripes"})
        {
            check(run(name, src.view(), mixed).max_diff < 1e-5, std::format("{} fully mixed is identity", name));
            check(run(name, src.view(), none).max_diff > 0.05, std::format("{} changes the image by default", name));
        }

        const core::color::rgb red = core::color::hsv_to_rgb(0.0, 1.0, 1.0);
        const core::color::rgb green = core::color::hsv_to_rgb(1.0 / 3.0, 1.0, 1.0);
        const core::color::rgb gray = core::color::hsv_to_rgb(0.7, 0.0, 0.5);
        check(red == core::color::rgb{1, 0, 0} && std::abs(green[1] - 1.0f) < 1e-6 && green[0] < 1e-6 &&
                  gray == core::color::rgb{0.5f, 0.5f, 0.5f},
              "hsv_to_rgb maps hue turns to the primaries");

        check(run("f_psykoblobs", src.view(), none, 0.0).sum_diff !=
                  run("f_psykoblobs", src.view(), none, 0.25).sum_diff,
              "f_psykoblobs colors cycle over time");
        const run_result mono = run("f_psykostripes", src.view(), [](const core::effect& fx, core::param_values& v) {
            set(fx, v, "saturation", 0.0);
        });
        const float* m = mono.out.view().at(12, 30);
        check(std::abs(m[0] - m[1]) < 1e-5 && std::abs(m[1] - m[2]) < 1e-5, "f_psykostripes saturation 0 is gray");

        const core::buffer white = solid(core::rect{0, 0, 192, 108}, 1.0f);
        core::buffer black = solid(core::rect{0, 0, 192, 108}, 0.0f);
        for (int i = 3; i < 192 * 108 * 4; i += 4)
        {
            black.view().data[i] = 1.0f;
        }
        // a 192 px frame at Scale 10 has 1 px font pixels: 5x5 glyphs in 6x6 cells
        const auto ascii = [](double swap) {
            return [swap](const core::effect& fx, core::param_values& v) {
                set(fx, v, "scale", 10.0);
                set(fx, v, "dithering", 0.0);
                set(fx, v, "swap_colors", swap);
            };
        };
        check(run("f_ascii", black.view(), ascii(0.0)).sum_diff == 0.0, "f_ascii draws black as spaces");
        check(run("f_ascii", black.view(), ascii(1.0)).sum_diff == 192.0 * 108.0 * 3.0,
              "f_ascii with swapped colors draws spaces in the font color");
        const run_result hash = run("f_ascii", white.view(), ascii(0.0));
        const double lit = 1.0 + (hash.sum_diff / (192.0 * 108.0 * 3.0));
        check(std::abs(lit - (16.0 / 36.0)) < 0.01, std::format("f_ascii draws white as # ({} lit)", lit));
        check(run("f_ascii", card(64, 48).view(), none).max_diff > 0.5, "f_ascii changes the image by default");

        const auto rolling = [](double factor) {
            return [factor](const core::effect& fx, core::param_values& v) {
                set(fx, v, "rolling_flicker_factor", factor);
            };
        };
        check(run("f_rollingbar", white.view(), rolling(0.0)).max_diff < 1e-6,
              "f_rollingbar with factor 0 is identity");
        // at time 0 the ramp runs from black at the top to the source at the bottom
        const run_result ramp = run("f_rollingbar", white.view(), rolling(1.0), 0.0);
        check(ramp.out.view().at(5, 0)[0] < 0.01f && ramp.out.view().at(5, 107)[0] > 0.99f &&
                  ramp.out.view().at(5, 0)[3] == 1.0f,
              "f_rollingbar ramps from the top of the frame");
        check(run("f_rollingbar", src.view(), none, 0.2).sum_diff !=
                  run("f_rollingbar", src.view(), none, 0.6).sum_diff,
              "f_rollingbar rolls over time");
    }

    static void flares()
    {
        const core::effect& fx = *effects::catalog::find("f_lensflare");
        const auto lenses = core::flare::lenses();
        const auto& lens_param = *std::ranges::find(fx.params, std::string_view("lens"), &core::param::name);
        std::string labels;
        for (const core::flare::lens& l : lenses)
        {
            labels += (labels.empty() ? "" : "|") + std::string(l.name);
        }
        check(lens_param.choices == labels, "f_lensflare Lens popup lists the built-in lenses");

        core::buffer black = solid(core::rect{0, 0, 160, 90}, 0.0f);
        for (int y = 0; y < 90; ++y)
        {
            for (int x = 0; x < 160; ++x)
            {
                black.view().at(x, y)[3] = 1.0f;
            }
        }
        const auto none = [](const core::effect&, core::param_values&) {};
        check(run("f_lensflare", black.view(),
                  [](const core::effect& f, core::param_values& v) {
                      set(f, v, "brightness", 0.0);
                  }).max_diff < 1e-6,
              "f_lensflare with Brightness 0 leaves the clip alone");
        for (std::size_t i = 0; i < lenses.size(); ++i)
        {
            const run_result r = run("f_lensflare", black.view(), [i](const core::effect& f, core::param_values& v) {
                set(f, v, "lens", static_cast<double>(i));
            });
            // the hotspot default sits at (-0.2222, 0) frame units: pixel (44, 45)
            const float* hot = r.out.view().at(44, 45);
            const float* far = r.out.view().at(150, 5);
            check(r.sum_diff > 1.0 && hot[0] + hot[1] + hot[2] > far[0] + far[1] + far[2],
                  std::format("f_lensflare {} draws a flare brightest at the hotspot", lenses[i].name));
        }

        const run_result full = run("f_lensflare", black.view(), none);
        const run_result priority = run("f_lensflare", black.view(), [](const core::effect& f, core::param_values& v) {
            set(f, v, "performance", 1.0);
        });
        check(priority.sum_diff < full.sum_diff, "f_lensflare Priority Only drops elements");

        // an opaque occlusion mask over the whole frame hides the hotspot; inverted it hides nothing
        const core::buffer cover = solid(core::rect{0, 0, 160, 90}, 1.0f);
        const run_result hidden = run("f_lensflare", black.view(), none, 0.5, &cover.view());
        const run_result shown = run(
            "f_lensflare", black.view(),
            [](const core::effect& f, core::param_values& v) { set(f, v, "invert_occlusion", 1.0); }, 0.5,
            &cover.view());
        check(hidden.sum_diff < 1e-3 && std::abs(shown.sum_diff - full.sum_diff) < 1e-3,
              "f_lensflare occlusion hides the flare behind the mask");

        const auto flickering = [](const core::effect& f, core::param_values& v) { set(f, v, "flicker_amp", 1.0); };
        check(run("f_lensflare", black.view(), flickering, 0.2).sum_diff !=
                  run("f_lensflare", black.view(), flickering, 1.7).sum_diff,
              "f_lensflare flickers over time");
        const run_result only = run("f_lensflare", black.view(),
                                    [](const core::effect& f, core::param_values& v) { set(f, v, "combine", 2.0); });
        const float* hot = only.out.view().at(44, 45);
        check(hot[3] == std::min(1.0f, std::max({hot[0], hot[1], hot[2]})) && hot[3] > 0.0f &&
                  std::abs(only.sum_diff - full.sum_diff) < 1e-2,
              "f_lensflare Flare Only keeps the flare alone, with alpha from its brightest channel");
    }

    static void time_warps()
    {
        const core::effect& fx = *effects::catalog::find("f_timewarprgb");
        const core::buffer src = card(64, 48);
        // opaque 0.2 before, premultiplied 0.7 at half alpha after
        const core::buffer before = solid(src.view().bounds, 0.2f);
        const core::buffer after = solid(src.view().bounds, 0.35f);
        for (int y = 0; y < 48; ++y)
        {
            for (int x = 0; x < 64; ++x)
            {
                before.view().at(x, y)[3] = 1.0f;
                after.view().at(x, y)[3] = 0.5f;
            }
        }
        const std::array<offset_frame, 2> frames{{{-1, before.view()}, {1, after.view()}}};
        const auto none = [](const core::effect&, core::param_values&) {};

        core::param_values defaults(fx.params);
        check(fx.frame_offsets(defaults, 0) == std::vector<int>{1, -1}, "f_timewarprgb defaults need frames +1 and -1");
        core::param_values same(fx.params);
        set(fx, same, "red_shift_frames", 2.0);
        set(fx, same, "green_shift_frames", 2.0);
        set(fx, same, "blue_shift_frames", 2.0);
        check(fx.frame_offsets(same, 0) == std::vector<int>{2}, "f_timewarprgb lists each frame once");

        const run_result warped = run("f_timewarprgb", src.view(), none, 0.5, nullptr, frames);
        const float* w = warped.out.view().at(20, 30);
        const float* s = src.view().at(20, 30);
        check(std::abs(w[0] - 0.7f) < 1e-5f && w[1] == s[1] && std::abs(w[2] - 0.2f) < 1e-5f && w[3] == 1.0f,
              "f_timewarprgb takes red from the next frame (straight), green from now and blue from the last");
        check(run("f_timewarprgb", src.view(),
                  [](const core::effect& fx, core::param_values& v) {
                      set(fx, v, "red_shift_frames", 0.0);
                      set(fx, v, "blue_shift_frames", 0.0);
                  }).max_diff < 1e-5,
              "f_timewarprgb with no shifts is identity");
        const run_result missing = run("f_timewarprgb", src.view(), none);
        check(missing.out.view().at(20, 30)[0] == 0.0f && missing.out.view().at(20, 30)[2] == 0.0f,
              "f_timewarprgb leaves channels from frames the host has none of black");
        const run_result gray = run(
            "f_timewarprgb", src.view(),
            [](const core::effect& fx, core::param_values& v) { set(fx, v, "clamp_chroma", 0.0); }, 0.5, nullptr,
            frames);
        const float* g = gray.out.view().at(20, 30);
        check(std::abs(g[0] - g[1]) < 1e-5f && std::abs(g[1] - g[2]) < 1e-5f, "f_timewarprgb Clamp Chroma 0 is gray");
        const run_result clamped = run(
            "f_timewarprgb", src.view(),
            [](const core::effect& fx, core::param_values& v) { set(fx, v, "clamp_chroma", 0.2); }, 0.5, nullptr,
            frames);
        const float* c = clamped.out.view().at(20, 30);
        check(std::max({c[0], c[1], c[2]}) - std::min({c[0], c[1], c[2]}) < 0.2f + 1e-5f,
              "f_timewarprgb Clamp Chroma limits max - min");
        run("f_timewarprgb", src.view(), [](const core::effect& fx, core::param_values& v) {
            set(fx, v, "red_shift_frames", 1.0);
            set(fx, v, "blue_shift_frames", 1.0);
        });
        check(fetched == std::vector<int>{1}, "f_timewarprgb fetches a frame two channels share once");
    }

    // an opaque gray frame
    static core::buffer gray(core::rect r, float value)
    {
        core::buffer b = solid(r, value);
        for (int y = r.y0; y < r.y1; ++y)
        {
            for (int x = r.x0; x < r.x1; ++x)
            {
                b.view().at(x, y)[3] = 1.0f;
            }
        }
        return b;
    }

    static void time_displaces()
    {
        const core::effect& fx = *effects::catalog::find("f_timedisplace");
        const core::rect r{0, 0, 32, 24};
        const core::buffer earlier = gray(r, 0.2f);
        const core::buffer later = gray(r, 0.6f);
        const core::buffer two_back = gray(r, 0.9f);
        const std::array<offset_frame, 3> frames{{{-1, earlier.view()}, {1, later.view()}, {-2, two_back.view()}}};
        // the source is its own matte: black, mid gray (luma 0.5) and white
        const core::buffer black = gray(r, 0.0f);
        const core::buffer mid = gray(r, 0.5f);
        const core::buffer white = gray(r, 1.0f);
        const auto shifts = [](double b, double w, bool interp) {
            return [=](const core::effect& fx, core::param_values& v) {
                set(fx, v, "black_time_shift", b);
                set(fx, v, "white_time_shift", w);
                set(fx, v, "interp_frames", interp ? 1.0 : 0.0);
                set(fx, v, "blur_matte", 0.0);
            };
        };
        const auto at = [](const run_result& res) { return res.out.view().at(10, 10); };

        core::param_values v(fx.params);
        check(fx.frame_offsets(v, 12).size() == 20, "f_timedisplace defaults need frames -10..10");
        shifts(-0.5, 0.5, true)(fx, v);
        check(fx.frame_offsets(v, 12) == std::vector<int>{-1, 1}, "f_timedisplace Interp Frames needs both neighbours");
        shifts(10.0, 10.0, false)(fx, v);
        set(fx, v, "shift_relative_to", 0.0);
        check(fx.frame_offsets(v, 12) == std::vector<int>{-2}, "f_timedisplace Frame 0 counts from frame 0");

        check(at(run("f_timedisplace", black.view(), shifts(-1, 1, false), 0.5, nullptr, frames))[0] == 0.2f,
              "f_timedisplace black matte shows Black Time Shift");
        check(at(run("f_timedisplace", white.view(), shifts(-1, 1, false), 0.5, nullptr, frames))[0] == 0.6f,
              "f_timedisplace white matte shows White Time Shift");
        const run_result now = run("f_timedisplace", mid.view(), shifts(-1, 1, false), 0.5, nullptr, frames);
        check(at(now)[0] == 0.5f && fetched.empty(), "f_timedisplace fetches only the frames some pixel reads");
        const run_result blend = run("f_timedisplace", mid.view(), shifts(-2, 1, true), 0.5, nullptr, frames);
        check(std::abs(at(blend)[0] - 0.35f) < 1e-5f && at(blend)[3] == 1.0f && fetched == std::vector<int>{-1},
              "f_timedisplace Interp Frames blends the two frames around a fractional shift");
        check(at(run("f_timedisplace", mid.view(), shifts(-2, 1, false), 0.5, nullptr, frames))[0] == 0.2f,
              "f_timedisplace without Interp Frames takes the nearest frame");
        // time 0.5 s at 24 fps is frame 12
        const run_result absolute = run(
            "f_timedisplace", black.view(),
            [&](const core::effect& fx, core::param_values& v) {
                shifts(10, 10, false)(fx, v);
                set(fx, v, "shift_relative_to", 0.0);
            },
            0.5, nullptr, frames);
        check(at(absolute)[0] == 0.9f, "f_timedisplace Frame 0 reads absolute frames");
        check(at(run("f_timedisplace", black.view(), shifts(-3, -3, false), 0.5, nullptr, frames))[3] == 0.0f,
              "f_timedisplace frames the host has none of are transparent");
    }

    static void motion_detects()
    {
        const core::effect& fx = *effects::catalog::find("f_motiondetect");
        const core::buffer src = card(64, 48);
        const core::buffer earlier = gray(src.view().bounds, 0.2f);
        const std::array<offset_frame, 1> frames{{{-1, earlier.view()}}};
        const auto none = [](const core::effect&, core::param_values&) {};
        const float* s = src.view().at(20, 30);

        core::param_values v(fx.params);
        check(fx.frame_offsets(v, 0) == std::vector<int>{-1}, "f_motiondetect defaults need the last frame");
        set(fx, v, "delay_frames", 0.0);
        check(fx.frame_offsets(v, 0).empty(), "f_motiondetect Delay Frames 0 needs no other frame");

        const run_result motion = run("f_motiondetect", src.view(), none, 0.5, nullptr, frames);
        const float* m = motion.out.view().at(20, 30);
        check(std::abs(m[0] - std::abs(s[0] - 0.2f)) < 1e-6f && std::abs(m[2] - std::abs(s[2] - 0.2f)) < 1e-6f &&
                  m[3] == 1.0f,
              "f_motiondetect shows the difference from the last frame, opaque");
        const run_result still = run("f_motiondetect", src.view(), [](const core::effect& fx, core::param_values& v) {
            set(fx, v, "delay_frames", 0.0);
        });
        check(still.out.view().at(20, 30)[0] == 0.0f, "f_motiondetect Delay Frames 0 shows no motion");
        const run_result added = run(
            "f_motiondetect", src.view(),
            [](const core::effect& fx, core::param_values& v) {
                set(fx, v, "motion", 1.0);
                set(fx, v, "combine", 2.0);
            },
            0.5, nullptr, frames);
        check(std::abs(added.out.view().at(20, 30)[1] - (2.0f * s[1] - 0.2f)) < 1e-6f,
              "f_motiondetect Brighter added to the clip");
        core::buffer half = card(64, 48);
        std::for_each(half.view().data, half.view().data + 64 * 48 * 4, [](float& c) { c *= 0.5f; });
        const run_result copied = run(
            "f_motiondetect", half.view(),
            [](const core::effect& fx, core::param_values& v) { set(fx, v, "output_opacity", 1.0); }, 0.5, nullptr,
            frames);
        const float* h = half.view().at(20, 30);
        const float* o = copied.out.view().at(20, 30);
        check(o[3] == 0.5f && std::abs(o[0] - std::abs(h[0] - 0.2f) * 0.5f) < 1e-6f,
              "f_motiondetect Copy From Input keeps the clip's alpha");
        const run_result missing = run("f_motiondetect", src.view(), none);
        check(missing.out.view().at(20, 30)[0] == s[0], "f_motiondetect differences a missing frame against black");
    }

    // the one frame offset a retime effect reads at `frame`, 0 for the current frame
    static int retime_offset(const core::effect& fx, const core::param_values& v, int frame)
    {
        const std::vector<int> offsets = fx.frame_offsets(v, frame);
        return offsets.empty() ? 0 : offsets[0];
    }

    static void retimes()
    {
        const core::buffer src = card(64, 48);
        const core::buffer other = gray(src.view().bounds, 0.3f);
        const auto none = [](const core::effect&, core::param_values&) {};
        const auto at_frame = [](int frame) { return frame / 24.0; };

        const core::effect& freeze = *effects::catalog::find("f_freezeframe");
        core::param_values f(freeze.params);
        check(retime_offset(freeze, f, 12) == -2 && retime_offset(freeze, f, 10) == 0,
              "f_freezeframe holds the first frame of each 5");
        set(freeze, f, "frame_start", 10.0);
        check(retime_offset(freeze, f, 3) == -3 && retime_offset(freeze, f, 5) == 0,
              "f_freezeframe blocks before Frame Start floor");
        const std::array<offset_frame, 1> held{{{-2, other.view()}}};
        check(run("f_freezeframe", src.view(), none, at_frame(12), nullptr, held).out.view().at(20, 30)[0] == 0.3f,
              "f_freezeframe shows the held frame");
        check(run("f_freezeframe", src.view(), none, at_frame(12)).out.view().at(20, 30)[3] == 0.0f,
              "f_freezeframe frames the host has none of are transparent");
        check(run("f_freezeframe", src.view(), none, at_frame(10)).max_diff == 0.0,
              "f_freezeframe shows the clip on the first frame of a block");

        const core::effect& reverse = *effects::catalog::find("f_reverseedits");
        core::param_values r(reverse.params);
        check(retime_offset(reverse, r, 10) == 4 && retime_offset(reverse, r, 12) == 0 &&
                  retime_offset(reverse, r, 14) == -4,
              "f_reverseedits plays each 5 frames backwards");
        set(reverse, r, "edits_start", 2.0);
        check(retime_offset(reverse, r, 1) == -4, "f_reverseedits blocks count from Edits Start");
        const std::array<offset_frame, 1> ahead{{{4, other.view()}}};
        check(run("f_reverseedits", src.view(), none, at_frame(10), nullptr, ahead).out.view().at(20, 30)[0] == 0.3f,
              "f_reverseedits shows the block's last frame first");

        const core::effect& jitter = *effects::catalog::find("f_jitterframes");
        core::param_values j(jitter.params);
        std::set<int> jumps;
        for (int frame = 0; frame < 200; ++frame)
        {
            jumps.insert(retime_offset(jitter, j, frame));
        }
        check(*jumps.begin() >= 0 && *jumps.rbegin() <= 10 && jumps.size() > 5,
              "f_jitterframes jumps 0 .. Jitter Frame Dist frames ahead");
        set(jitter, j, "edit_frame_length", 4.0);
        check(retime_offset(jitter, j, 8) == retime_offset(jitter, j, 11),
              "f_jitterframes keeps one jump through an edit");
        const int jump = retime_offset(jitter, core::param_values(jitter.params), 12);
        const std::array<offset_frame, 1> jumped{{{jump, other.view()}}};
        check(jump == 0 ||
                  run("f_jitterframes", src.view(), none, at_frame(12), nullptr, jumped).out.view().at(20, 30)[0] ==
                      0.3f,
              "f_jitterframes shows the frame it jumped to");
    }
} // namespace functional_fx_suite::tests::effects_test

int main()
{
    functional_fx_suite::tests::effects_test::param_tables();
    functional_fx_suite::tests::effects_test::identities();
    functional_fx_suite::tests::effects_test::behaviour();
    functional_fx_suite::tests::effects_test::glows();
    functional_fx_suite::tests::effects_test::tones_and_edges();
    functional_fx_suite::tests::effects_test::stylize();
    functional_fx_suite::tests::effects_test::damage();
    functional_fx_suite::tests::effects_test::masks();
    functional_fx_suite::tests::effects_test::background();
    functional_fx_suite::tests::effects_test::warps();
    functional_fx_suite::tests::effects_test::generators();
    functional_fx_suite::tests::effects_test::flares();
    functional_fx_suite::tests::effects_test::time_warps();
    functional_fx_suite::tests::effects_test::time_displaces();
    functional_fx_suite::tests::effects_test::motion_detects();
    functional_fx_suite::tests::effects_test::retimes();
    const int failures = functional_fx_suite::tests::effects_test::failures;
    std::cout << (failures == 0 ? "all tests passed\n" : std::format("{} test(s) failed\n", failures));
    return failures == 0 ? 0 : 1;
}
