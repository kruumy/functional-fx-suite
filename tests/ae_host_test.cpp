#include "AEConfig.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_EffectCBSuites.h"
#include "AE_EffectSuites.h"
#include "SPBasic.h"
#include "core/buffer.hpp"
#include "core/param_values.hpp"
#include "effects/catalog.hpp"
#include "test_support.hpp"

#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <functional>
#include <iostream>
#include <map>
#include <string_view>
#include <vector>

// minimal in-process After Effects host: param setup, UI updates and one SmartFX render
namespace functional_fx_suite::tests::ae_host_test
{
    using entry_fn = PF_Err (*)(PF_Cmd, PF_InData*, PF_OutData*, PF_ParamDef*[], PF_LayerDef*, void*);

    struct host_state
    {
        std::vector<PF_ParamDef> params;
        std::vector<int> ui_updates; // param indices passed to PF_UpdateParamUI
        std::vector<int> disabled;   // their disabled state
        PF_PixelFormat format = PF_PixelFormat_ARGB128;
        PF_EffectWorld* output = nullptr;
        std::map<A_long, A_long> input_times;            // checkout id -> layer time, from pre-render
        std::function<PF_EffectWorld*(A_long)> input_at; // the input layer at a time
        std::vector<A_long> held;                        // other frames checked out and not yet checked in
    };

    static host_state state;

    static PF_Err add_param(PF_ProgPtr, PF_ParamIndex, PF_ParamDefPtr def)
    {
        state.params.push_back(*def);
        return PF_Err_NONE;
    }
    static PF_Err checkout_param(PF_ProgPtr, PF_ParamIndex index, A_long, A_long, A_u_long, PF_ParamDef* def)
    {
        if (index < 0 || index >= static_cast<PF_ParamIndex>(state.params.size()))
        {
            return PF_Err_BAD_CALLBACK_PARAM;
        }
        *def = state.params[index];
        if (def->param_type == PF_Param_POINT && state.output != nullptr)
        {
            // AE turns the percentage default into layer pixels when the effect is applied
            def->u.td.x_value =
                static_cast<PF_Fixed>(static_cast<double>(def->u.td.x_dephault) * state.output->width / 100.0);
            def->u.td.y_value =
                static_cast<PF_Fixed>(static_cast<double>(def->u.td.y_dephault) * state.output->height / 100.0);
        }
        return PF_Err_NONE;
    }
    static PF_Err checkin_param(PF_ProgPtr, PF_ParamDef*)
    {
        return PF_Err_NONE;
    }

    static SPAPI PF_Err float_color(PF_ProgPtr, const PF_ParamDef* def, PF_PixelFloat* out)
    {
        const PF_Pixel& c = def->u.cd.value;
        *out = PF_PixelFloat{c.alpha / 255.0f, c.red / 255.0f, c.green / 255.0f, c.blue / 255.0f};
        return PF_Err_NONE;
    }
    static PF_Err pixel_format(const PF_EffectWorld*, PF_PixelFormat* out)
    {
        *out = state.format;
        return PF_Err_NONE;
    }
    static SPAPI PF_Err update_param_ui(PF_ProgPtr, PF_ParamIndex index, const PF_ParamDef* def)
    {
        state.ui_updates.push_back(index);
        state.disabled.push_back((def->ui_flags & PF_PUI_DISABLED) != 0 ? 1 : 0);
        state.params[index].ui_flags = def->ui_flags;
        return PF_Err_NONE;
    }

    static SPAPI SPErr acquire_suite(const char* name, int32, const void** suite)
    {
        static PF_ColorParamSuite1 colors{};
        static PF_WorldSuite2 worlds{};
        static PF_ParamUtilsSuite3 utils{};
        colors.PF_GetFloatingPointColorFromColorDef = float_color;
        worlds.PF_GetPixelFormat = pixel_format;
        utils.PF_UpdateParamUI = update_param_ui;
        const std::string_view n(name);
        *suite = n == kPFColorParamSuite   ? static_cast<const void*>(&colors)
                 : n == kPFWorldSuite      ? static_cast<const void*>(&worlds)
                 : n == kPFParamUtilsSuite ? static_cast<const void*>(&utils)
                                           : nullptr;
        return *suite != nullptr ? kSPNoError : kSPSuiteNotFoundError;
    }
    static SPAPI SPErr release_suite(const char*, int32)
    {
        return kSPNoError;
    }

    // only the input layer is connected
    static PF_Err checkout_layer(PF_ProgPtr, PF_ParamIndex index, A_long id, const PF_RenderRequest*, A_long time,
                                 A_long, A_u_long, PF_CheckoutResult* result)
    {
        if (index == 0)
        {
            state.input_times[id] = time;
        }
        *result = PF_CheckoutResult{};
        result->result_rect = PF_LRect{0, 0, state.output->width, state.output->height};
        result->max_result_rect = result->result_rect;
        return PF_Err_NONE;
    }
    static PF_Err checkout_layer_pixels(PF_ProgPtr, A_long id, PF_EffectWorld** pixels)
    {
        const auto it = state.input_times.find(id);
        *pixels = it != state.input_times.end() ? state.input_at(it->second) : nullptr;
        if (id >= 3 && *pixels != nullptr)
        {
            state.held.push_back(id);
        }
        return PF_Err_NONE;
    }

    static PF_Err checkin_layer_pixels(PF_ProgPtr, A_long id)
    {
        std::erase(state.held, id);
        return PF_Err_NONE;
    }
    static PF_Err checkout_output(PF_ProgPtr, PF_EffectWorld** output)
    {
        *output = state.output;
        return PF_Err_NONE;
    }

    template <class Pixel> struct world
    {
        std::vector<Pixel> pixels;
        PF_EffectWorld w{};

        world(int width, int height) : pixels(static_cast<std::size_t>(width) * height)
        {
            w.data = reinterpret_cast<PF_PixelPtr>(pixels.data());
            w.width = width;
            w.height = height;
            w.rowbytes = static_cast<A_long>(width * sizeof(Pixel));
        }
    };

    template <class Pixel>
    static void run_depth(entry_fn entry, PF_InData& in, PF_OutData& out, const core::effect& fx, PF_PixelFormat format,
                          float max_value, double tolerance)
    {
        constexpr int W = 96;
        constexpr int H = 64;
        // the input `frame` frames from now, as the host's world and as the core's float buffer
        struct input_frame
        {
            world<Pixel> host{W, H};
            core::buffer floats{core::rect{0, 0, W, H}, 4};
        };
        std::map<int, input_frame> inputs;
        const auto input_at = [&](int frame) -> input_frame& {
            const auto [it, added] = inputs.try_emplace(frame);
            for (int y = 0; added && y < H; ++y)
            {
                for (int x = 0; x < W; ++x)
                {
                    float* s = it->second.floats.view().at(x, y);
                    Pixel& p = it->second.host.pixels[static_cast<std::size_t>(y) * W + x];
                    using channel = decltype(p.red);
                    const auto q = [&](int c) {
                        // clamped so 8-bit worlds can hold the spot
                        const auto v = static_cast<float>(std::min(spot_value(x, y, c, W, H, frame), 1.0));
                        return static_cast<channel>(max_value == 1.0f ? v : std::round(v * max_value));
                    };
                    p.alpha = static_cast<channel>(max_value);
                    p.red = q(0);
                    p.green = q(1);
                    p.blue = q(2);
                    // as the plugin reads it, so values on a threshold land on the same side
                    const float scale = 1.0f / max_value;
                    const float alpha = static_cast<float>(p.alpha) * scale;
                    s[0] = static_cast<float>(p.red) * scale * alpha;
                    s[1] = static_cast<float>(p.green) * scale * alpha;
                    s[2] = static_cast<float>(p.blue) * scale * alpha;
                    s[3] = alpha;
                }
            }
            return it->second;
        };
        world<Pixel> output(W, H);
        state.format = format;
        state.output = &output.w;
        state.input_times.clear();
        state.held.clear();
        state.input_at = [&](A_long time) { return &input_at((time - in.current_time) / in.time_step).host.w; };
        in.width = W;
        in.height = H;

        PF_PreRenderInput pre_in{};
        pre_in.output_request.rect = PF_LRect{0, 0, W, H};
        PF_PreRenderOutput pre_out{};
        PF_PreRenderCallbacks pre_cb{};
        pre_cb.checkout_layer = checkout_layer;
        PF_PreRenderExtra pre{&pre_in, &pre_out, &pre_cb};
        check(entry(PF_Cmd_SMART_PRE_RENDER, &in, &out, nullptr, nullptr, &pre) == PF_Err_NONE,
              std::format("{}: pre-render", fx.name));
        check(pre_out.result_rect.right == W && pre_out.result_rect.bottom == H,
              std::format("{}: result rect", fx.name));

        PF_SmartRenderInput render_in{};
        PF_SmartRenderCallbacks render_cb{};
        render_cb.checkout_layer_pixels = checkout_layer_pixels;
        render_cb.checkin_layer_pixels = checkin_layer_pixels;
        render_cb.checkout_output = checkout_output;
        PF_SmartRenderExtra render{&render_in, &render_cb};
        check(entry(PF_Cmd_SMART_RENDER, &in, &out, nullptr, nullptr, &render) == PF_Err_NONE,
              std::format("{}: smart render", fx.name));
        check(state.held.empty(), std::format("{}: smart render checks its other frames back in", fx.name));

        // the same frame through the core with default params; AE stores color defaults as 8-bit
        core::param_values values(fx.params);
        for (std::size_t i = 0; i < fx.params.size(); ++i)
        {
            if (fx.params[i].kind == core::param_kind::color)
            {
                core::param_values::value c = fx.params[i].def;
                std::ranges::for_each(c, [](double& v) { v = std::round(std::clamp(v, 0.0, 1.0) * 255.0) / 255.0; });
                values.set(i, c);
            }
        }
        const core::buffer& src = input_at(0).floats;
        core::buffer expected(src.view().bounds, 4);
        core::render_args ra;
        ra.time = static_cast<double>(in.current_time) / in.time_scale;
        ra.frame_duration = static_cast<double>(in.time_step) / in.time_scale;
        // the plugin checks out the input at each offset under the ids after the fixed inputs (0..2)
        const std::vector<int> offsets = fx.offsets(values, ra.frame_number());
        for (std::size_t i = 0; i < offsets.size(); ++i)
        {
            const auto it = state.input_times.find(static_cast<A_long>(3 + i));
            check(it != state.input_times.end() && it->second == in.current_time + offsets[i] * in.time_step,
                  std::format("{}: pre-render checks out the input {} frames from now", fx.name, offsets[i]));
        }
        std::vector<int> fetched;
        bool repeated = false;
        ra.frame_at = [&](int offset) {
            core::buffer b;
            repeated = repeated || std::ranges::find(fetched, offset) != fetched.end();
            fetched.push_back(offset);
            if (std::ranges::find(offsets, offset) != offsets.end())
            {
                b = core::buffer::copy_of(input_at(offset).floats.view());
            }
            return b;
        };
        ra.src = src.view();
        ra.dst = expected.view();
        ra.params = &values;
        ra.geometry = centered_frame(W, H);
        fx.render(ra);
        check(!repeated, std::format("{}: render fetches each frame offset at most once", fx.name));

        double max_diff = 0.0;
        for (int y = 0; y < H; ++y)
        {
            for (int x = 0; x < W; ++x)
            {
                const float* e = expected.view().at(x, y);
                const Pixel& p = output.pixels[static_cast<std::size_t>(y) * W + x];
                const float a = std::clamp(e[3], 0.0f, max_value == 1.0f ? e[3] : 1.0f);
                const float inv = a > 0.0f ? 1.0f / a : 0.0f;
                const auto expect = [&](float v) { return max_value == 1.0f ? v : std::clamp(v, 0.0f, 1.0f); };
                const auto diff = [&](double got, float want) {
                    return std::abs(got / max_value - static_cast<double>(expect(want)));
                };
                max_diff = std::max({max_diff, diff(p.alpha, a), diff(p.red, e[0] * inv), diff(p.green, e[1] * inv),
                                     diff(p.blue, e[2] * inv)});
            }
        }
        check(max_diff <= tolerance, std::format("{} ({} bit): AE render matches core (max diff {})", fx.name,
                                                 max_value == 1.0f ? 32 : 8, max_diff));
        std::cout << std::format("{} {}-bit: max diff vs core {:.2e}\n", fx.name, max_value == 1.0f ? 32 : 8, max_diff);
    }

    static void run_plugin(const std::string& path)
    {
        HMODULE lib = LoadLibraryA(path.c_str());
        auto entry = lib != nullptr ? reinterpret_cast<entry_fn>(GetProcAddress(lib, "EffectMain")) : nullptr;
        check(entry != nullptr, std::format("load {}", path));
        if (entry == nullptr)
        {
            return;
        }
        state = host_state{};
        state.params.push_back(PF_ParamDef{});

        SPBasicSuite basic{};
        basic.AcquireSuite = acquire_suite;
        basic.ReleaseSuite = release_suite;
        PF_InData in{};
        in.inter.add_param = add_param;
        in.inter.checkout_param = checkout_param;
        in.inter.checkin_param = checkin_param;
        in.pica_basicP = &basic;
        in.downsample_x = in.downsample_y = PF_RationalScale{1, 1};
        in.time_scale = 2400;
        in.time_step = 100;
        in.current_time = 700;
        PF_OutData out{};

        check(entry(PF_Cmd_GLOBAL_SETUP, &in, &out, nullptr, nullptr, nullptr) == PF_Err_NONE, "global setup");
        check(entry(PF_Cmd_PARAMS_SETUP, &in, &out, nullptr, nullptr, nullptr) == PF_Err_NONE, "params setup");
        check(out.num_params == static_cast<A_long>(state.params.size()), "num_params matches added params");
        check(entry(PF_Cmd_ABOUT, &in, &out, nullptr, nullptr, nullptr) == PF_Err_NONE, "about");
        const std::string_view about(out.return_msg);
        const core::effect* fx = effects::catalog::find(about.substr(0, about.find(' ')));
        check(fx != nullptr, std::format("{}: about names a known effect", path));
        if (fx == nullptr)
        {
            return;
        }

        std::vector<PF_ParamDef*> pointers;
        for (PF_ParamDef& p : state.params)
        {
            pointers.push_back(&p);
        }
        check(entry(PF_Cmd_UPDATE_PARAMS_UI, &in, &out, pointers.data(), nullptr, nullptr) == PF_Err_NONE,
              std::format("{}: update params ui", fx->name));
        const auto gated = std::ranges::count_if(
            fx->params, [](const core::param& p) { return p.enable_param >= 0 && (p.enable_mask & 1U) == 0; });
        check(static_cast<std::ptrdiff_t>(state.ui_updates.size()) == gated &&
                  std::ranges::all_of(state.disabled, [](int d) { return d == 1; }),
              std::format("{}: params unused by the default choice are disabled", fx->name));

        run_depth<PF_PixelFloat>(entry, in, out, *fx, PF_PixelFormat_ARGB128, 1.0f, 1e-5);
        run_depth<PF_Pixel8>(entry, in, out, *fx, PF_PixelFormat_ARGB32, 255.0f, 0.5 / 255.0 + 1e-4);
    }
} // namespace functional_fx_suite::tests::ae_host_test

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        functional_fx_suite::tests::ae_host_test::run_plugin(argv[i]);
    }
    return functional_fx_suite::tests::finish("AE host ");
}
