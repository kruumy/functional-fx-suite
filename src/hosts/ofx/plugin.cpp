#include "core/param_values.hpp"
#include "hosts/ofx/pixels.hpp"
#include "hosts/plugin_effect.hpp"
#include "ofxImageEffect.h"
#include "ofxOld.h"
#include "ofxParam.h"
#include "ofxProperty.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace functional_fx_suite::hosts::ofx::plugin
{
    static constexpr const char* SOURCE_CLIP = kOfxImageEffectSimpleSourceClipName;
    static constexpr const char* OUTPUT_CLIP = kOfxImageEffectOutputClipName;
    static constexpr const char* MASK_CLIP = "Mask";
    static constexpr const char* BACKGROUND_CLIP = "Background";

    struct host_suites
    {
        const OfxHost* host = nullptr;
        const OfxPropertySuiteV1* props = nullptr;
        const OfxImageEffectSuiteV1* effects = nullptr;
        const OfxParameterSuiteV1* params = nullptr;
    };

    // set once by the host through OfxSetHost and kOfxActionLoad, read-only afterwards
    static host_suites suites;

    static void check(OfxStatus status)
    {
        if (status != kOfxStatOK)
        {
            throw std::runtime_error("host call failed");
        }
    }

    // property access; optional properties report success instead of throwing
    class property_set
    {
      public:
        explicit property_set(OfxPropertySetHandle h) : h_(h)
        {
        }

        bool set(const char* name, const char* v, int i = 0) const
        {
            return suites.props->propSetString(h_, name, i, v) == kOfxStatOK;
        }
        bool set(const char* name, std::string_view v, int i = 0) const
        {
            const std::string copy(v);
            return set(name, copy.c_str(), i);
        }
        bool set(const char* name, int v, int i = 0) const
        {
            return suites.props->propSetInt(h_, name, i, v) == kOfxStatOK;
        }
        bool set(const char* name, double v, int i = 0) const
        {
            return suites.props->propSetDouble(h_, name, i, v) == kOfxStatOK;
        }
        [[nodiscard]] std::string_view get_string(const char* name, int i = 0) const
        {
            char* v = nullptr;
            check(suites.props->propGetString(h_, name, i, &v));
            return v != nullptr ? std::string_view(v) : std::string_view();
        }
        [[nodiscard]] int get_int(const char* name, int i = 0) const
        {
            int v = 0;
            check(suites.props->propGetInt(h_, name, i, &v));
            return v;
        }
        [[nodiscard]] double get_double(const char* name, int i = 0, double fallback = 0.0) const
        {
            double v = fallback;
            return suites.props->propGetDouble(h_, name, i, &v) == kOfxStatOK ? v : fallback;
        }
        [[nodiscard]] void* get_pointer(const char* name) const
        {
            void* v = nullptr;
            check(suites.props->propGetPointer(h_, name, 0, &v));
            return v;
        }

      private:
        OfxPropertySetHandle h_;
    };

    // an image fetched from a clip, released on scope exit
    class clip_image
    {
      public:
        clip_image(OfxImageClipHandle clip, OfxTime time)
        {
            if (suites.effects->clipGetImage(clip, time, nullptr, &h_) != kOfxStatOK)
            {
                h_ = nullptr;
            }
        }
        clip_image(const clip_image&) = delete;
        clip_image& operator=(const clip_image&) = delete;
        ~clip_image()
        {
            if (h_ != nullptr)
            {
                suites.effects->clipReleaseImage(h_);
            }
        }

        [[nodiscard]] bool valid() const
        {
            return h_ != nullptr;
        }
        [[nodiscard]] pixels::host_image describe() const
        {
            const property_set p(h_);
            pixels::host_image img;
            img.data = p.get_pointer(kOfxImagePropData);
            img.x1 = p.get_int(kOfxImagePropBounds, 0);
            img.y1 = p.get_int(kOfxImagePropBounds, 1);
            img.x2 = p.get_int(kOfxImagePropBounds, 2);
            img.y2 = p.get_int(kOfxImagePropBounds, 3);
            img.row_bytes = p.get_int(kOfxImagePropRowBytes);
            const std::string_view depth = p.get_string(kOfxImageEffectPropPixelDepth);
            img.bit_depth = depth == kOfxBitDepthByte    ? pixels::depth::byte
                            : depth == kOfxBitDepthShort ? pixels::depth::word
                            : depth == kOfxBitDepthFloat ? pixels::depth::real
                                                         : throw std::runtime_error("unsupported bit depth");
            const std::string_view comps = p.get_string(kOfxImageEffectPropComponents);
            img.components = comps == kOfxImageComponentAlpha ? 1 : comps == kOfxImageComponentRGBA ? 4 : 0;
            img.premultiplied = p.get_string(kOfxImageEffectPropPreMultiplication) != kOfxImageUnPreMultiplied;
            return img;
        }

      private:
        OfxPropertySetHandle h_ = nullptr;
    };

    static OfxImageClipHandle clip_handle(OfxImageEffectHandle effect, const char* name)
    {
        OfxImageClipHandle clip = nullptr;
        return suites.effects->clipGetHandle(effect, name, &clip, nullptr) == kOfxStatOK ? clip : nullptr;
    }

    static bool connected(OfxImageClipHandle clip)
    {
        OfxPropertySetHandle props = nullptr;
        return clip != nullptr && suites.effects->clipGetPropertySet(clip, &props) == kOfxStatOK &&
               property_set(props).get_int(kOfxImageClipPropConnected) != 0;
    }

    static OfxStatus load()
    {
        if (suites.host == nullptr)
        {
            return kOfxStatErrMissingHostFeature;
        }
        const auto fetch = [](const char* name, int version) {
            return suites.host->fetchSuite(suites.host->host, name, version);
        };
        suites.props = static_cast<const OfxPropertySuiteV1*>(fetch(kOfxPropertySuite, 1));
        suites.effects = static_cast<const OfxImageEffectSuiteV1*>(fetch(kOfxImageEffectSuite, 1));
        suites.params = static_cast<const OfxParameterSuiteV1*>(fetch(kOfxParameterSuite, 1));
        const bool ok = suites.props != nullptr && suites.effects != nullptr && suites.params != nullptr;
        return ok ? kOfxStatOK : kOfxStatErrMissingHostFeature;
    }

    static OfxStatus describe(OfxImageEffectHandle effect)
    {
        const core::effect& fx = plugin_effect::get();
        OfxPropertySetHandle handle = nullptr;
        check(suites.effects->getPropertySet(effect, &handle));
        const property_set p(handle);
        p.set(kOfxPropLabel, fx.name);
        // "Functional Distort" groups as "Functional/Distort"
        std::string grouping(fx.category);
        std::ranges::replace(grouping, ' ', '/');
        p.set(kOfxImageEffectPluginPropGrouping, grouping);
        p.set(kOfxPropPluginDescription, fx.description);
        p.set(kOfxImageEffectPropSupportedContexts, kOfxImageEffectContextFilter, 0);
        p.set(kOfxImageEffectPropSupportedContexts, kOfxImageEffectContextGeneral, 1);
        p.set(kOfxImageEffectPropSupportedPixelDepths, kOfxBitDepthByte, 0);
        p.set(kOfxImageEffectPropSupportedPixelDepths, kOfxBitDepthShort, 1);
        p.set(kOfxImageEffectPropSupportedPixelDepths, kOfxBitDepthFloat, 2);
        p.set(kOfxImageEffectPropSupportsTiles, 1);
        p.set(kOfxImageEffectPropSupportsMultiResolution, 1);
        p.set(kOfxImageEffectPropTemporalClipAccess, fx.frame_offsets != nullptr ? 1 : 0);
        p.set(kOfxImageEffectPropSupportsMultipleClipDepths, 0);
        p.set(kOfxImageEffectPluginRenderThreadSafety, kOfxImageEffectRenderFullySafe);
        // rendering is already multi-threaded inside the effect
        p.set(kOfxImageEffectPluginPropHostFrameThreading, 0);
        return kOfxStatOK;
    }

    static property_set define_clip(OfxImageEffectHandle effect, const char* name, std::string_view label,
                                    bool optional, bool mask)
    {
        OfxPropertySetHandle handle = nullptr;
        check(suites.effects->clipDefine(effect, name, &handle));
        const property_set p(handle);
        p.set(kOfxImageEffectPropSupportedComponents, kOfxImageComponentRGBA, 0);
        if (mask)
        {
            p.set(kOfxImageEffectPropSupportedComponents, kOfxImageComponentAlpha, 1);
            p.set(kOfxImageClipPropIsMask, 1);
        }
        if (!label.empty())
        {
            p.set(kOfxPropLabel, label);
        }
        p.set(kOfxImageClipPropOptional, optional ? 1 : 0);
        p.set(kOfxImageEffectPropSupportsTiles, 1);
        return p;
    }

    static void define_param(OfxParamSetHandle set, const core::param& prm, std::string_view parent)
    {
        static constexpr std::array TYPES{kOfxParamTypeDouble, kOfxParamTypeBoolean,  kOfxParamTypeChoice,
                                          kOfxParamTypeRGB,    kOfxParamTypeDouble2D, kOfxParamTypeGroup};
        const std::string name(prm.name);
        OfxPropertySetHandle handle = nullptr;
        check(suites.params->paramDefine(set, TYPES[static_cast<int>(prm.kind)], name.c_str(), &handle));
        const property_set p(handle);
        p.set(kOfxPropLabel, prm.label);
        p.set(kOfxParamPropScriptName, prm.name);
        if (!parent.empty())
        {
            p.set(kOfxParamPropParent, parent);
        }
        switch (prm.kind)
        {
        case core::param_kind::scalar:
            p.set(kOfxParamPropDoubleType, kOfxParamDoubleTypePlain);
            p.set(kOfxParamPropDefault, prm.def[0]);
            p.set(kOfxParamPropMin, prm.min);
            p.set(kOfxParamPropMax, prm.max);
            p.set(kOfxParamPropDisplayMin, prm.slider_min);
            p.set(kOfxParamPropDisplayMax, prm.slider_max);
            p.set(kOfxParamPropDigits, prm.precision);
            break;
        case core::param_kind::toggle:
            p.set(kOfxParamPropDefault, prm.def[0] >= 0.5 ? 1 : 0);
            break;
        case core::param_kind::choice: {
            std::string_view rest = prm.choices;
            for (int i = 0; !rest.empty(); ++i)
            {
                const auto bar = rest.find('|');
                p.set(kOfxParamPropChoiceOption, rest.substr(0, bar), i);
                rest = bar == std::string_view::npos ? std::string_view() : rest.substr(bar + 1);
            }
            p.set(kOfxParamPropDefault, static_cast<int>(prm.def[0]));
            break;
        }
        case core::param_kind::color:
            for (int c = 0; c < 3; ++c)
            {
                p.set(kOfxParamPropDefault, prm.def[c], c);
            }
            break;
        case core::param_kind::point: {
            // prefer absolute positions with a normalized default (OFX 1.4); older hosts get normalized values
            p.set(kOfxParamPropDoubleType, kOfxParamDoubleTypeXYAbsolute);
            if (!p.set(kOfxParamPropDefaultCoordinateSystem, kOfxParamCoordinatesNormalised))
            {
                p.set(kOfxParamPropDoubleType, kOfxParamDoubleTypeNormalisedXYAbsolute);
            }
            p.set(kOfxParamPropDefault, 0.5 + prm.def[0], 0);
            p.set(kOfxParamPropDefault, 0.5 + prm.def[1], 1);
            break;
        }
        case core::param_kind::group_begin:
        case core::param_kind::group_end:
            break;
        }
    }

    static OfxStatus describe_in_context(OfxImageEffectHandle effect)
    {
        const core::effect& fx = plugin_effect::get();
        define_clip(effect, OUTPUT_CLIP, {}, false, false);
        define_clip(effect, SOURCE_CLIP, {}, false, false)
            .set(kOfxImageEffectPropTemporalClipAccess, fx.frame_offsets != nullptr ? 1 : 0);
        if (!fx.background_input.empty())
        {
            define_clip(effect, BACKGROUND_CLIP, fx.background_input, true, false);
        }
        if (!fx.mask_input.empty())
        {
            define_clip(effect, MASK_CLIP, fx.mask_input, true, true);
        }
        OfxParamSetHandle set = nullptr;
        check(suites.effects->getParamSet(effect, &set));
        std::vector<std::string_view> parents;
        for (const core::param& prm : fx.params)
        {
            if (prm.kind == core::param_kind::group_end)
            {
                if (!parents.empty())
                {
                    parents.pop_back();
                }
                continue;
            }
            define_param(set, prm, parents.empty() ? std::string_view() : parents.back());
            if (prm.kind == core::param_kind::group_begin)
            {
                parents.push_back(prm.name);
            }
        }
        return kOfxStatOK;
    }

    static OfxParamHandle param_handle(OfxParamSetHandle set, std::string_view name,
                                       OfxPropertySetHandle* props = nullptr)
    {
        const std::string n(name);
        OfxParamHandle h = nullptr;
        check(suites.params->paramGetHandle(set, n.c_str(), &h, props));
        return h;
    }

    // hides params whose controlling choice (e.g. Style) doesn't use them, grayed out on hosts that ignore secret
    static OfxStatus update_enabled(OfxImageEffectHandle effect)
    {
        const core::effect& fx = plugin_effect::get();
        OfxParamSetHandle set = nullptr;
        check(suites.effects->getParamSet(effect, &set));
        for (const core::param& prm : fx.params)
        {
            if (prm.enable_param < 0)
            {
                continue;
            }
            int choice = 0;
            check(suites.params->paramGetValue(param_handle(set, fx.params[prm.enable_param].name), &choice));
            const bool enabled = choice >= 0 && choice < 32 && ((prm.enable_mask >> choice) & 1U) != 0;
            OfxPropertySetHandle props = nullptr;
            param_handle(set, prm.name, &props);
            property_set(props).set(kOfxParamPropEnabled, enabled ? 1 : 0);
            property_set(props).set(kOfxParamPropSecret, enabled ? 0 : 1);
        }
        return kOfxStatOK;
    }

    static void set_roi(const property_set& out, const char* clip_name, const OfxRectD& r)
    {
        const std::string prop = std::string("OfxImageClipPropRoI_") + clip_name;
        out.set(prop.c_str(), r.x1, 0);
        out.set(prop.c_str(), r.y1, 1);
        out.set(prop.c_str(), r.x2, 2);
        out.set(prop.c_str(), r.y2, 3);
    }

    static OfxStatus regions_of_interest(OfxImageEffectHandle effect, OfxPropertySetHandle in_args,
                                         OfxPropertySetHandle out_args)
    {
        const core::effect& fx = plugin_effect::get();
        const OfxTime time = property_set(in_args).get_double(kOfxPropTime);
        const property_set out(out_args);
        const auto full = [&](const char* name) {
            OfxImageClipHandle clip = clip_handle(effect, name);
            OfxRectD rod{};
            if (connected(clip) && suites.effects->clipGetRegionOfDefinition(clip, time, &rod) == kOfxStatOK)
            {
                set_roi(out, name, rod);
            }
        };
        if (fx.reads_outside)
        {
            full(SOURCE_CLIP);
        }
        if (!fx.mask_input.empty())
        {
            // mask blur needs neighbours outside the render window
            full(MASK_CLIP);
        }
        return kOfxStatOK;
    }

    static core::param_values read_params(OfxImageEffectHandle effect, OfxTime time, const core::frame& f,
                                          double scale_x, double scale_y, double par)
    {
        const core::effect& fx = plugin_effect::get();
        core::param_values values(fx.params);
        OfxParamSetHandle set = nullptr;
        check(suites.effects->getParamSet(effect, &set));
        OfxPropertySetHandle effect_props = nullptr;
        check(suites.effects->getPropertySet(effect, &effect_props));
        const property_set ep(effect_props);
        for (std::size_t i = 0; i < fx.params.size(); ++i)
        {
            const core::param& prm = fx.params[i];
            if (prm.kind == core::param_kind::group_begin || prm.kind == core::param_kind::group_end)
            {
                continue;
            }
            OfxPropertySetHandle props = nullptr;
            const OfxParamHandle h = param_handle(set, prm.name, &props);
            switch (prm.kind)
            {
            case core::param_kind::scalar: {
                double v = 0;
                check(suites.params->paramGetValueAtTime(h, time, &v));
                values.set(i, {v, 0, 0});
                break;
            }
            case core::param_kind::toggle:
            case core::param_kind::choice: {
                int v = 0;
                check(suites.params->paramGetValueAtTime(h, time, &v));
                values.set(i, {static_cast<double>(v), 0, 0});
                break;
            }
            case core::param_kind::color: {
                double r = 0;
                double g = 0;
                double b = 0;
                check(suites.params->paramGetValueAtTime(h, time, &r, &g, &b));
                values.set(i, {r, g, b});
                break;
            }
            case core::param_kind::point: {
                double x = 0;
                double y = 0;
                check(suites.params->paramGetValueAtTime(h, time, &x, &y));
                if (property_set(props).get_string(kOfxParamPropDoubleType) == kOfxParamDoubleTypeNormalisedXYAbsolute)
                {
                    x = ep.get_double(kOfxImageEffectPropProjectOffset, 0) +
                        x * ep.get_double(kOfxImageEffectPropProjectExtent, 0);
                    y = ep.get_double(kOfxImageEffectPropProjectOffset, 1) +
                        y * ep.get_double(kOfxImageEffectPropProjectExtent, 1);
                }
                // canonical (y up) -> pixels at this render scale -> core (y down) -> frame units
                values.set(i, {f.to_nx(x * scale_x / par), f.to_ny(-y * scale_y), 0});
                break;
            }
            default:
                break;
            }
        }
        return values;
    }

    // the source frame in pixels at a render scale, and its pixel aspect ratio
    struct source_geometry
    {
        core::frame f;
        double par = 1.0;

        source_geometry(OfxImageClipHandle source, OfxTime time, double scale_x, double scale_y)
        {
            OfxPropertySetHandle source_props = nullptr;
            check(suites.effects->clipGetPropertySet(source, &source_props));
            par = property_set(source_props).get_double(kOfxImagePropPixelAspectRatio, 0, 1.0);
            OfxRectD rod{};
            check(suites.effects->clipGetRegionOfDefinition(source, time, &rod));
            const double sx = scale_x / par;
            f = core::frame{(rod.x1 + rod.x2) * 0.5 * sx, -(rod.y1 + rod.y2) * 0.5 * scale_y, (rod.x2 - rod.x1) * sx};
        }
    };

    // the render's time as the core sees it
    static void set_time(core::render_args& args, OfxImageEffectHandle effect, OfxTime time)
    {
        OfxPropertySetHandle output_props = nullptr;
        check(suites.effects->clipGetPropertySet(clip_handle(effect, OUTPUT_CLIP), &output_props));
        const double fps = property_set(output_props).get_double(kOfxImageEffectPropFrameRate, 0, 24.0);
        args.frame_duration = 1.0 / (fps > 0.0 ? fps : 24.0);
        args.time = time * args.frame_duration;
    }

    // the effect's other frames and the current one, consecutive frames merged into one range
    static OfxStatus frames_needed(OfxImageEffectHandle effect, OfxPropertySetHandle in_args,
                                   OfxPropertySetHandle out_args)
    {
        const core::effect& fx = plugin_effect::get();
        if (fx.frame_offsets == nullptr)
        {
            return kOfxStatReplyDefault;
        }
        const OfxTime time = property_set(in_args).get_double(kOfxPropTime);
        const source_geometry g(clip_handle(effect, SOURCE_CLIP), time, 1.0, 1.0);
        core::render_args timing;
        set_time(timing, effect, time);
        std::vector<int> offsets =
            fx.frame_offsets(read_params(effect, time, g.f, 1.0, 1.0, g.par), timing.frame_number());
        offsets.push_back(0);
        std::ranges::sort(offsets);
        const std::string prop = std::string("OfxImageClipPropFrameRange_") + SOURCE_CLIP;
        const property_set out(out_args);
        int index = 0;
        for (std::size_t i = 0; i < offsets.size();)
        {
            std::size_t j = i;
            while (j + 1 < offsets.size() && offsets[j + 1] - offsets[j] <= 1)
            {
                ++j;
            }
            out.set(prop.c_str(), time + offsets[i], index++);
            out.set(prop.c_str(), time + offsets[j], index++);
            i = j + 1;
        }
        return kOfxStatOK;
    }

    static OfxStatus render(OfxImageEffectHandle effect, OfxPropertySetHandle in_args)
    {
        const core::effect& fx = plugin_effect::get();
        const property_set in(in_args);
        const OfxTime time = in.get_double(kOfxPropTime);
        const core::rect window = pixels::to_core(
            in.get_int(kOfxImageEffectPropRenderWindow, 0), in.get_int(kOfxImageEffectPropRenderWindow, 1),
            in.get_int(kOfxImageEffectPropRenderWindow, 2), in.get_int(kOfxImageEffectPropRenderWindow, 3));
        const double scale_x = in.get_double(kOfxImageEffectPropRenderScale, 0, 1.0);
        const double scale_y = in.get_double(kOfxImageEffectPropRenderScale, 1, 1.0);

        OfxImageClipHandle source = clip_handle(effect, SOURCE_CLIP);
        OfxImageClipHandle output = clip_handle(effect, OUTPUT_CLIP);
        const clip_image src_image(source, time);
        const clip_image dst_image(output, time);
        if (!src_image.valid() || !dst_image.valid())
        {
            return kOfxStatFailed;
        }

        const source_geometry g(source, time, scale_x, scale_y);
        const core::frame& f = g.f;
        const double par = g.par;

        const core::param_values values = read_params(effect, time, f, scale_x, scale_y, par);
        const core::buffer src = pixels::read(src_image.describe());
        const auto optional_input = [&](const char* name, std::string_view label) {
            core::buffer b;
            OfxImageClipHandle clip = label.empty() ? nullptr : clip_handle(effect, name);
            if (connected(clip))
            {
                const clip_image img(clip, time);
                if (img.valid())
                {
                    b = pixels::read(img.describe());
                }
            }
            return b;
        };
        const core::buffer mask = optional_input(MASK_CLIP, fx.mask_input);
        const core::buffer background = optional_input(BACKGROUND_CLIP, fx.background_input);
        core::buffer dst(window, 4);

        core::render_args args;
        set_time(args, effect, time);
        const std::vector<int> offsets =
            fx.frame_offsets != nullptr ? fx.frame_offsets(values, args.frame_number()) : std::vector<int>{};
        args.frame_at = [&](int offset) {
            core::buffer b;
            if (std::ranges::find(offsets, offset) != offsets.end())
            {
                const clip_image img(source, time + offset);
                if (img.valid())
                {
                    b = pixels::read(img.describe());
                }
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
            return read_params(effect, seconds / args.frame_duration, f, scale_x, scale_y, par);
        };
        fx.render(args);
        pixels::write(dst.view(), dst_image.describe(), window);
        return kOfxStatOK;
    }

    static OfxStatus main_entry(const char* action, const void* handle, OfxPropertySetHandle in_args,
                                OfxPropertySetHandle out_args)
    {
        try
        {
            const std::string_view a(action);
            auto effect = static_cast<OfxImageEffectHandle>(const_cast<void*>(handle));
            if (a == kOfxActionLoad)
            {
                return load();
            }
            if (a == kOfxActionDescribe)
            {
                return describe(effect);
            }
            if (a == kOfxImageEffectActionDescribeInContext)
            {
                return describe_in_context(effect);
            }
            if (a == kOfxActionCreateInstance || a == kOfxActionInstanceChanged)
            {
                return update_enabled(effect);
            }
            if (a == kOfxImageEffectActionGetRegionsOfInterest)
            {
                return regions_of_interest(effect, in_args, out_args);
            }
            if (a == kOfxImageEffectActionGetFramesNeeded)
            {
                return frames_needed(effect, in_args, out_args);
            }
            if (a == kOfxImageEffectActionRender)
            {
                return render(effect, in_args);
            }
            return kOfxStatReplyDefault;
        }
        catch (const std::bad_alloc&)
        {
            return kOfxStatErrMemory;
        }
        catch (...)
        {
            return kOfxStatFailed;
        }
    }

    static void set_host(OfxHost* host)
    {
        suites.host = host;
    }

    static OfxPlugin& descriptor()
    {
        static const std::string id = std::string("functionalfx.") + std::string(plugin_effect::get().match_name);
        static OfxPlugin plugin{kOfxImageEffectPluginApi, 1,        id.c_str(), FFX_VERSION_MAJOR,
                                FFX_VERSION_MINOR,        set_host, main_entry};
        return plugin;
    }
} // namespace functional_fx_suite::hosts::ofx::plugin

OfxExport int OfxGetNumberOfPlugins(void)
{
    return 1;
}

OfxExport OfxPlugin* OfxGetPlugin(int nth)
{
    return nth == 0 ? &functional_fx_suite::hosts::ofx::plugin::descriptor() : nullptr;
}
