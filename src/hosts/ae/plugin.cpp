#include "hosts/ae/plugin.hpp"
#include "AEConfig.h" // IWYU pragma: keep
#include "AE_EffectCBSuites.h"
#include "AE_EffectSuites.h"
#include "AE_GeneralPlug.h"
#include "core/param_values.hpp"
#include "entry.h"
#include "hosts/ae/pixels.hpp"
#include "hosts/ae/suite.hpp"
#include "hosts/plugin_effect.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

// FFX_AE_OUTFLAGS, FFX_AE_OUTFLAGS2, FFX_AE_VERSION, FFX_AE_TIME_VARYING and FFX_AE_WIDE_TIME come from
// cmake/ae_plugin.cmake, which writes the same literals into the PiPL; AE refuses mismatches, so check them here
static_assert(FFX_AE_OUTFLAGS == (PF_OutFlag_DEEP_COLOR_AWARE | PF_OutFlag_SEND_UPDATE_PARAMS_UI |
                                  (FFX_AE_TIME_VARYING ? PF_OutFlag_NON_PARAM_VARY : 0) |
                                  (FFX_AE_WIDE_TIME ? PF_OutFlag_WIDE_TIME_INPUT : 0)));
static_assert(FFX_AE_OUTFLAGS2 ==
              (PF_OutFlag2_SUPPORTS_SMART_RENDER | PF_OutFlag2_FLOAT_COLOR_AWARE |
               PF_OutFlag2_SUPPORTS_THREADED_RENDERING | PF_OutFlag2_PARAM_GROUP_START_COLLAPSED_FLAG |
               (FFX_AE_WIDE_TIME ? PF_OutFlag2_AUTOMATIC_WIDE_TIME_INPUT : 0)));
static_assert(FFX_AE_VERSION ==
              PF_VERSION(FFX_VERSION_MAJOR, FFX_VERSION_MINOR, FFX_VERSION_PATCH, PF_Stage_DEVELOP, 1));

namespace functional_fx_suite::hosts::ae::plugin
{
    // stable ids for the optional layer inputs; table params use their own ids below 900
    static constexpr A_long BACKGROUND_ID = 900;
    static constexpr A_long MASK_ID = 901;

    enum class checkout : std::uint8_t
    {
        input,
        mask,
        background,
        frames // the effect's other frames, one id each from here on
    };

    // AE param indices: 0 is the input layer, then optional layers, then the effect's table
    struct layout
    {
        int background = -1;
        int mask = -1;
        int first_param = 1;

        explicit layout(const core::effect& fx)
        {
            if (!fx.background_input.empty())
            {
                background = first_param++;
            }
            if (!fx.mask_input.empty())
            {
                mask = first_param++;
            }
        }
    };

    template <std::size_t N> static void copy_name(A_char (&dst)[N], std::string_view src)
    {
        const std::size_t n = std::min(src.size(), N - 1);
        std::memcpy(dst, src.data(), n);
        dst[n] = '\0';
    }

    static PF_Err check(PF_Err err)
    {
        if (err != PF_Err_NONE)
        {
            throw std::runtime_error("host call failed");
        }
        return err;
    }

    // layer size at the current downsample; point params and worlds use these coordinates
    static core::frame layer_frame(const PF_InData* in)
    {
        const double w = in->width * static_cast<double>(in->downsample_x.num) / in->downsample_x.den;
        const double h = in->height * static_cast<double>(in->downsample_y.num) / in->downsample_y.den;
        return core::frame{w * 0.5, h * 0.5, w};
    }

    // AE hides params only through the AEGP stream suites; unset where they're missing (Premiere), so params gray out
    static std::optional<AEGP_PluginID> aegp_id;

    static PF_Err global_setup(PF_InData* in, PF_OutData* out)
    {
        // the PiPL name comes from the build; refuse to load if it names a different effect
        // and refuse an effect reading other frames without the wide time flags AE needs to invalidate its cache
        const core::effect& fx = plugin_effect::get();
        if (fx.match_name != std::string_view(FFX_AE_MATCH_NAME) || (fx.frame_offsets != nullptr && !FFX_AE_WIDE_TIME))
        {
            return PF_Err_INTERNAL_STRUCT_DAMAGED;
        }
        try
        {
            const suite<AEGP_UtilitySuite6> utility(in, kAEGPUtilitySuite, kAEGPUtilitySuiteVersion6);
            AEGP_PluginID id = 0;
            if (utility->AEGP_RegisterWithAEGP(nullptr, FFX_AE_MATCH_NAME, &id) == A_Err_NONE)
            {
                aegp_id = id;
            }
        }
        catch (const std::runtime_error&)
        {
        }
        out->my_version = FFX_AE_VERSION;
        out->out_flags = FFX_AE_OUTFLAGS;
        out->out_flags2 = FFX_AE_OUTFLAGS2;
        return PF_Err_NONE;
    }

    static PF_Err about(PF_OutData* out)
    {
        const core::effect& fx = plugin_effect::get();
        const auto r =
            std::format_to_n(out->return_msg, sizeof(out->return_msg) - 1, "{} {}.{}.{}\r{}\rFunctional Effects Suite",
                             fx.name, FFX_VERSION_MAJOR, FFX_VERSION_MINOR, FFX_VERSION_PATCH, fx.description);
        *r.out = '\0';
        return PF_Err_NONE;
    }

    static void add_layer(PF_InData* in, std::string_view label, A_long id)
    {
        PF_ParamDef def{};
        def.param_type = PF_Param_LAYER;
        copy_name(def.name, label);
        def.u.ld.dephault = PF_LayerDefault_NONE;
        def.uu.id = id;
        check(PF_ADD_PARAM(in, -1, &def));
    }

    static PF_ParamDef describe(const core::effect& fx, const core::param& p, std::size_t index)
    {
        PF_ParamDef def{};
        def.uu.id = p.id;
        copy_name(def.name, p.label);
        const auto supervised = std::ranges::any_of(
            fx.params, [&](const core::param& other) { return other.enable_param == static_cast<int>(index); });
        def.flags = supervised ? PF_ParamFlag_SUPERVISE : 0;
        switch (p.kind)
        {
        case core::param_kind::scalar:
            def.param_type = PF_Param_FLOAT_SLIDER;
            def.u.fs_d.valid_min = static_cast<PF_FpShort>(p.min);
            def.u.fs_d.valid_max = static_cast<PF_FpShort>(p.max);
            def.u.fs_d.slider_min = static_cast<PF_FpShort>(p.slider_min);
            def.u.fs_d.slider_max = static_cast<PF_FpShort>(p.slider_max);
            def.u.fs_d.value = def.u.fs_d.dephault = p.def[0];
            def.u.fs_d.precision = static_cast<A_short>(p.precision);
            break;
        case core::param_kind::toggle:
            def.param_type = PF_Param_CHECKBOX;
            def.u.bd.value = def.u.bd.dephault = p.def[0] >= 0.5;
            def.u.bd.u.nameptr = "";
            break;
        case core::param_kind::choice:
            def.param_type = PF_Param_POPUP;
            def.u.pd.num_choices = static_cast<A_short>(std::ranges::count(p.choices, '|') + 1);
            def.u.pd.value = def.u.pd.dephault = static_cast<A_short>(std::lround(p.def[0]) + 1);
            // table strings are literals, so the pointer stays valid and NUL terminated
            def.u.pd.u.namesptr = p.choices.data();
            break;
        case core::param_kind::color: {
            const auto to_byte = [](double v) {
                return static_cast<A_u_char>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
            };
            def.param_type = PF_Param_COLOR;
            def.u.cd.value = PF_Pixel{255, to_byte(p.def[0]), to_byte(p.def[1]), to_byte(p.def[2])};
            def.u.cd.dephault = def.u.cd.value;
            break;
        }
        case core::param_kind::point:
            // defaults are percent of the layer, origin top left
            def.param_type = PF_Param_POINT;
            def.u.td.x_value = def.u.td.x_dephault = static_cast<PF_Fixed>((50.0 + p.def[0] * 100.0) * 65536.0);
            def.u.td.y_value = def.u.td.y_dephault = static_cast<PF_Fixed>((50.0 - p.def[1] * 100.0) * 65536.0);
            break;
        case core::param_kind::group_begin:
            def.param_type = PF_Param_GROUP_START;
            def.flags |= PF_ParamFlag_START_COLLAPSED;
            break;
        case core::param_kind::group_end:
            def.param_type = PF_Param_GROUP_END;
            break;
        }
        return def;
    }

    static PF_Err params_setup(PF_InData* in, PF_OutData* out)
    {
        const core::effect& fx = plugin_effect::get();
        const layout l(fx);
        if (l.background >= 0)
        {
            add_layer(in, fx.background_input, BACKGROUND_ID);
        }
        if (l.mask >= 0)
        {
            add_layer(in, fx.mask_input, MASK_ID);
        }
        for (std::size_t i = 0; i < fx.params.size(); ++i)
        {
            PF_ParamDef def = describe(fx, fx.params[i], i);
            check(PF_ADD_PARAM(in, -1, &def));
        }
        out->num_params = l.first_param + static_cast<A_long>(fx.params.size());
        return PF_Err_NONE;
    }

    static void set_hidden(PF_InData* in, const std::vector<std::pair<int, bool>>& hidden)
    {
        const suite<AEGP_PFInterfaceSuite1> pf(in, kAEGPPFInterfaceSuite, kAEGPPFInterfaceSuiteVersion1);
        const suite<AEGP_EffectSuite5> effects(in, kAEGPEffectSuite, kAEGPEffectSuiteVersion5);
        const suite<AEGP_StreamSuite6> streams(in, kAEGPStreamSuite, kAEGPStreamSuiteVersion6);
        const suite<AEGP_DynamicStreamSuite4> dynamic(in, kAEGPDynamicStreamSuite, kAEGPDynamicStreamSuiteVersion4);
        AEGP_EffectRefH effect = nullptr;
        check(pf->AEGP_GetNewEffectForEffect(*aegp_id, in->effect_ref, &effect));
        A_Err err = A_Err_NONE;
        for (const auto& [index, hide] : hidden)
        {
            AEGP_StreamRefH stream = nullptr;
            err = streams->AEGP_GetNewEffectStreamByIndex(*aegp_id, effect, index, &stream);
            if (err != A_Err_NONE)
            {
                break;
            }
            AEGP_DynStreamFlags flags = 0;
            err = dynamic->AEGP_GetDynamicStreamFlags(stream, &flags);
            // setting the flag redraws the panel, so only on a change
            if (err == A_Err_NONE && ((flags & AEGP_DynStreamFlag_HIDDEN) != 0) != hide)
            {
                err = dynamic->AEGP_SetDynamicStreamFlag(stream, AEGP_DynStreamFlag_HIDDEN, FALSE, hide);
            }
            streams->AEGP_DisposeStream(stream);
            if (err != A_Err_NONE)
            {
                break;
            }
        }
        effects->AEGP_DisposeEffect(effect);
        check(err);
    }

    // hides (or, without AEGP, grays out) params whose controlling choice (e.g. Style) doesn't use them
    static PF_Err update_params_ui(PF_InData* in, PF_ParamDef* params[])
    {
        const core::effect& fx = plugin_effect::get();
        const layout l(fx);
        const suite<PF_ParamUtilsSuite3> utils(in, kPFParamUtilsSuite, kPFParamUtilsSuiteVersion3);
        std::vector<std::pair<int, bool>> hidden;
        for (std::size_t i = 0; i < fx.params.size(); ++i)
        {
            const core::param& p = fx.params[i];
            if (p.enable_param < 0)
            {
                continue;
            }
            const int choice = params[l.first_param + p.enable_param]->u.pd.value - 1;
            const bool enabled = choice >= 0 && choice < 32 && ((p.enable_mask >> choice) & 1U) != 0;
            const int index = l.first_param + static_cast<int>(i);
            hidden.emplace_back(index, !enabled);
            PF_ParamDef copy = *params[index];
            const bool disabled = (copy.ui_flags & PF_PUI_DISABLED) != 0;
            if (disabled != enabled)
            {
                continue;
            }
            copy.ui_flags = enabled ? copy.ui_flags & ~PF_PUI_DISABLED : copy.ui_flags | PF_PUI_DISABLED;
            check(utils->PF_UpdateParamUI(in->effect_ref, index, &copy));
        }
        if (aegp_id && !hidden.empty())
        {
            set_hidden(in, hidden);
        }
        return PF_Err_NONE;
    }

    static PF_LRect intersect(const PF_LRect& a, const PF_LRect& b)
    {
        PF_LRect r{std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right),
                   std::min(a.bottom, b.bottom)};
        if (r.right < r.left || r.bottom < r.top)
        {
            r = PF_LRect{};
        }
        return r;
    }

    static core::param_values read_params(PF_InData* in, const core::effect& fx, const layout& l, const core::frame& f,
                                          A_long time);

    // `offset` frames from now in layer time, kept in A_long range for huge shifts
    static A_long frame_time(const PF_InData* in, int offset)
    {
        const long long t = static_cast<long long>(in->current_time) + static_cast<long long>(offset) * in->time_step;
        return static_cast<A_long>(
            std::clamp<long long>(t, std::numeric_limits<A_long>::min(), std::numeric_limits<A_long>::max()));
    }

    // the render's time as the core sees it
    static void set_time(core::render_args& args, const PF_InData* in)
    {
        args.time = static_cast<double>(in->current_time) / in->time_scale;
        args.frame_duration =
            in->time_step != 0 ? std::abs(static_cast<double>(in->time_step)) / in->time_scale : 1.0 / 24.0;
    }

    static PF_Err pre_render(PF_InData* in, PF_PreRenderExtra* extra)
    {
        const core::effect& fx = plugin_effect::get();
        const layout l(fx);
        PF_RenderRequest req = extra->input->output_request;
        if (fx.reads_outside)
        {
            const core::frame f = layer_frame(in);
            req.rect =
                PF_LRect{0, 0, static_cast<A_long>(std::ceil(f.cx * 2.0)), static_cast<A_long>(std::ceil(f.cy * 2.0))};
        }
        const auto checkout_layer = [&](int index, checkout id, PF_CheckoutResult& result) {
            return extra->cb->checkout_layer(in->effect_ref, index, static_cast<A_long>(id), &req, in->current_time,
                                             in->time_step, in->time_scale, &result);
        };
        PF_CheckoutResult input{};
        check(checkout_layer(0, checkout::input, input));
        PF_CheckoutResult unused{};
        if (l.mask >= 0)
        {
            check(checkout_layer(l.mask, checkout::mask, unused));
        }
        if (l.background >= 0)
        {
            check(checkout_layer(l.background, checkout::background, unused));
        }
        if (fx.frame_offsets != nullptr)
        {
            core::render_args timing;
            set_time(timing, in);
            const std::vector<int> offsets =
                fx.frame_offsets(read_params(in, fx, l, layer_frame(in), in->current_time), timing.frame_number());
            for (std::size_t i = 0; i < offsets.size(); ++i)
            {
                check(extra->cb->checkout_layer(in->effect_ref, 0,
                                                static_cast<A_long>(checkout::frames) + static_cast<A_long>(i), &req,
                                                frame_time(in, offsets[i]), in->time_step, in->time_scale, &unused));
            }
        }
        extra->output->result_rect =
            fx.reads_outside ? intersect(extra->input->output_request.rect, input.max_result_rect) : input.result_rect;
        extra->output->max_result_rect = input.max_result_rect;
        return PF_Err_NONE;
    }

    // checks a param out for reading and back in when done
    class checked_param
    {
      public:
        checked_param(PF_InData* in, int index, A_long time) : in_(in)
        {
            check(PF_CHECKOUT_PARAM(in, index, time, in->time_step, in->time_scale, &def_));
        }
        checked_param(const checked_param&) = delete;
        checked_param& operator=(const checked_param&) = delete;
        ~checked_param()
        {
            PF_CHECKIN_PARAM(in_, &def_);
        }
        [[nodiscard]] const PF_ParamDef& def() const
        {
            return def_;
        }

      private:
        PF_InData* in_;
        PF_ParamDef def_{};
    };

    static core::param_values read_params(PF_InData* in, const core::effect& fx, const layout& l, const core::frame& f,
                                          A_long time)
    {
        core::param_values values(fx.params);
        const suite<PF_ColorParamSuite1> colors(in, kPFColorParamSuite, kPFColorParamSuiteVersion1);
        for (std::size_t i = 0; i < fx.params.size(); ++i)
        {
            const core::param_kind kind = fx.params[i].kind;
            if (kind == core::param_kind::group_begin || kind == core::param_kind::group_end)
            {
                continue;
            }
            const checked_param cp(in, l.first_param + static_cast<int>(i), time);
            const PF_ParamDef& def = cp.def();
            switch (kind)
            {
            case core::param_kind::scalar:
                values.set(i, {def.u.fs_d.value, 0, 0});
                break;
            case core::param_kind::toggle:
                values.set(i, {def.u.bd.value != 0 ? 1.0 : 0.0, 0, 0});
                break;
            case core::param_kind::choice:
                values.set(i, {static_cast<double>(def.u.pd.value - 1), 0, 0});
                break;
            case core::param_kind::color: {
                PF_PixelFloat c{};
                check(colors->PF_GetFloatingPointColorFromColorDef(in->effect_ref, &def, &c));
                values.set(i, {c.red, c.green, c.blue});
                break;
            }
            case core::param_kind::point:
                values.set(i, {f.to_nx(def.u.td.x_value / 65536.0), f.to_ny(def.u.td.y_value / 65536.0), 0});
                break;
            default:
                break;
            }
        }
        return values;
    }

    static core::rect world_bounds(const PF_EffectWorld& w)
    {
        return core::rect{w.origin_x, w.origin_y, w.origin_x + w.width, w.origin_y + w.height};
    }

    static PF_Err smart_render(PF_InData* in, PF_SmartRenderExtra* extra)
    {
        const core::effect& fx = plugin_effect::get();
        const layout l(fx);
        PF_EffectWorld* input = nullptr;
        PF_EffectWorld* output = nullptr;
        check(extra->cb->checkout_layer_pixels(in->effect_ref, static_cast<A_long>(checkout::input), &input));
        check(extra->cb->checkout_output(in->effect_ref, &output));
        if (input == nullptr || output == nullptr)
        {
            return PF_Err_NONE;
        }
        PF_PixelFormat format = PF_PixelFormat_INVALID;
        {
            const suite<PF_WorldSuite2> worlds(in, kPFWorldSuite, kPFWorldSuiteVersion2);
            check(worlds->PF_GetPixelFormat(output, &format));
        }
        const auto read_layer = [&](int index, checkout id) {
            core::buffer b;
            PF_EffectWorld* world = nullptr;
            if (index >= 0)
            {
                check(extra->cb->checkout_layer_pixels(in->effect_ref, static_cast<A_long>(id), &world));
            }
            if (world != nullptr)
            {
                b = pixels::read(*world, format, world_bounds(*world));
            }
            return b;
        };

        const core::frame f = layer_frame(in);
        const core::param_values values = read_params(in, fx, l, f, in->current_time);
        const core::buffer src = pixels::read(*input, format, world_bounds(*input));
        const core::buffer mask = read_layer(l.mask, checkout::mask);
        const core::buffer background = read_layer(l.background, checkout::background);
        core::buffer dst(world_bounds(*output), 4);

        core::render_args args;
        set_time(args, in);
        // the same offsets as pre-render: same params, same time
        const std::vector<int> offsets =
            fx.frame_offsets != nullptr ? fx.frame_offsets(values, args.frame_number()) : std::vector<int>{};
        args.frame_at = [&](int offset) {
            core::buffer b;
            const auto it = std::ranges::find(offsets, offset);
            if (it == offsets.end())
            {
                return b;
            }
            const A_long id = static_cast<A_long>(checkout::frames) + static_cast<A_long>(it - offsets.begin());
            PF_EffectWorld* world = nullptr;
            check(extra->cb->checkout_layer_pixels(in->effect_ref, id, &world));
            if (world != nullptr)
            {
                b = pixels::read(*world, format, world_bounds(*world));
                // the effect holds the copy; AE can free its frame now
                check(extra->cb->checkin_layer_pixels(in->effect_ref, id));
            }
            return b;
        };
        args.src = src.view();
        args.dst = dst.view();
        args.mask = mask.view().data != nullptr ? &mask.view() : nullptr;
        args.background = background.view().data != nullptr ? &background.view() : nullptr;
        args.params = &values;
        args.geometry = f;
        args.params_at = [&](double seconds) {
            return read_params(in, fx, l, f, static_cast<A_long>(std::lround(seconds * in->time_scale)));
        };
        fx.render(args);
        pixels::write(dst.view(), *output, format);
        return PF_Err_NONE;
    }

    PF_Err main(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef*, void* extra)
    {
        try
        {
            switch (cmd)
            {
            case PF_Cmd_ABOUT:
                return about(out_data);
            case PF_Cmd_GLOBAL_SETUP:
                return global_setup(in_data, out_data);
            case PF_Cmd_PARAMS_SETUP:
                return params_setup(in_data, out_data);
            case PF_Cmd_UPDATE_PARAMS_UI:
            case PF_Cmd_USER_CHANGED_PARAM:
                return update_params_ui(in_data, params);
            case PF_Cmd_SMART_PRE_RENDER:
                return pre_render(in_data, static_cast<PF_PreRenderExtra*>(extra));
            case PF_Cmd_SMART_RENDER:
                return smart_render(in_data, static_cast<PF_SmartRenderExtra*>(extra));
            default:
                return PF_Err_NONE;
            }
        }
        catch (const std::bad_alloc&)
        {
            return PF_Err_OUT_OF_MEMORY;
        }
        catch (...)
        {
            return PF_Err_INTERNAL_STRUCT_DAMAGED;
        }
    }
} // namespace functional_fx_suite::hosts::ae::plugin

extern "C" DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[],
                                       PF_LayerDef* output, void* extra)
{
    return functional_fx_suite::hosts::ae::plugin::main(cmd, in_data, out_data, params, output, extra);
}
