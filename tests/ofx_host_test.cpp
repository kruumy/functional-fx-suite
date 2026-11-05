#include "core/buffer.hpp"
#include "core/param_values.hpp"
#include "effects/catalog.hpp"
#include "ofxImageEffect.h"
#include "ofxParam.h"
#include "ofxProperty.h"
#include "test_support.hpp"

#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <format>
#include <iostream>
#include <map>
#include <memory>
#include <ranges>
#include <set>
#include <string>
#include <variant>
#include <vector>

// minimal in-process OFX host: just enough of the property, image effect and parameter suites
// to drive a plugin through describe, instance creation and one render
namespace functional_fx_suite::tests::ofx_host_test
{
    using value = std::variant<int, double, std::string, void*>;

    struct prop_set
    {
        std::map<std::string, std::vector<value>> values;
    };

    struct clip
    {
        prop_set props;
    };

    struct param
    {
        std::string type;
        prop_set props;
    };

    struct effect
    {
        prop_set props;
        std::map<std::string, std::unique_ptr<clip>> clips;
        std::map<std::string, std::unique_ptr<param>> params;
        std::vector<std::unique_ptr<prop_set>> images;
        core::buffer source;
        core::buffer output;
        double frame = 0;                   // render time
        std::map<int, core::buffer> others; // the source at other frames, by offset from `frame`
    };

    static prop_set* ps(OfxPropertySetHandle h)
    {
        return reinterpret_cast<prop_set*>(h);
    }

    static OfxPropertySetHandle handle(prop_set& p)
    {
        return reinterpret_cast<OfxPropertySetHandle>(&p);
    }

    template <class T> static OfxStatus set_value(OfxPropertySetHandle h, const char* name, int i, T v)
    {
        auto& vec = ps(h)->values[name];
        if (vec.size() <= static_cast<std::size_t>(i))
        {
            vec.resize(i + 1);
        }
        vec[i] = v;
        return kOfxStatOK;
    }

    template <class T> static OfxStatus get_value(OfxPropertySetHandle h, const char* name, int i, T* out)
    {
        const auto it = ps(h)->values.find(name);
        if (it == ps(h)->values.end() || it->second.size() <= static_cast<std::size_t>(i))
        {
            return kOfxStatErrUnknown;
        }
        if (const T* v = std::get_if<T>(&it->second[i]))
        {
            *out = *v;
            return kOfxStatOK;
        }
        return kOfxStatErrValue;
    }

    static OfxStatus prop_set_string(OfxPropertySetHandle h, const char* n, int i, const char* v)
    {
        return set_value(h, n, i, std::string(v));
    }
    static OfxStatus prop_set_double(OfxPropertySetHandle h, const char* n, int i, double v)
    {
        return set_value(h, n, i, v);
    }
    static OfxStatus prop_set_int(OfxPropertySetHandle h, const char* n, int i, int v)
    {
        return set_value(h, n, i, v);
    }
    static OfxStatus prop_set_pointer(OfxPropertySetHandle h, const char* n, int i, void* v)
    {
        return set_value(h, n, i, v);
    }
    static OfxStatus prop_get_string(OfxPropertySetHandle h, const char* n, int i, char** v)
    {
        const auto it = ps(h)->values.find(n);
        if (it == ps(h)->values.end() || it->second.size() <= static_cast<std::size_t>(i))
        {
            return kOfxStatErrUnknown;
        }
        auto* s = std::get_if<std::string>(&it->second[i]);
        if (s == nullptr)
        {
            return kOfxStatErrValue;
        }
        *v = s->data();
        return kOfxStatOK;
    }
    static OfxStatus prop_get_double(OfxPropertySetHandle h, const char* n, int i, double* v)
    {
        return get_value(h, n, i, v);
    }
    static OfxStatus prop_get_int(OfxPropertySetHandle h, const char* n, int i, int* v)
    {
        return get_value(h, n, i, v);
    }
    static OfxStatus prop_get_pointer(OfxPropertySetHandle h, const char* n, int i, void** v)
    {
        return get_value(h, n, i, v);
    }

    static effect* fx(OfxImageEffectHandle h)
    {
        return reinterpret_cast<effect*>(h);
    }

    static OfxStatus get_property_set(OfxImageEffectHandle h, OfxPropertySetHandle* out)
    {
        *out = handle(fx(h)->props);
        return kOfxStatOK;
    }
    static OfxStatus get_param_set(OfxImageEffectHandle h, OfxParamSetHandle* out)
    {
        *out = reinterpret_cast<OfxParamSetHandle>(h);
        return kOfxStatOK;
    }
    static OfxStatus clip_define(OfxImageEffectHandle h, const char* name, OfxPropertySetHandle* out)
    {
        auto& c = fx(h)->clips[name];
        c = std::make_unique<clip>();
        *out = handle(c->props);
        return kOfxStatOK;
    }
    static OfxStatus clip_get_handle(OfxImageEffectHandle h, const char* name, OfxImageClipHandle* out,
                                     OfxPropertySetHandle* props)
    {
        const auto it = fx(h)->clips.find(name);
        if (it == fx(h)->clips.end())
        {
            return kOfxStatErrBadHandle;
        }
        *out = reinterpret_cast<OfxImageClipHandle>(it->second.get());
        if (props != nullptr)
        {
            *props = handle(it->second->props);
        }
        return kOfxStatOK;
    }
    static OfxStatus clip_get_property_set(OfxImageClipHandle c, OfxPropertySetHandle* out)
    {
        *out = handle(reinterpret_cast<clip*>(c)->props);
        return kOfxStatOK;
    }

    static effect* current = nullptr;

    // images are float RGBA, premultiplied, covering the whole 0..w x 0..h frame
    static OfxStatus clip_get_image(OfxImageClipHandle c, OfxTime time, const OfxRectD*, OfxPropertySetHandle* out)
    {
        const auto* cl = reinterpret_cast<clip*>(c);
        const bool is_output = cl == current->clips[kOfxImageEffectOutputClipName].get();
        const bool is_source = cl == current->clips[kOfxImageEffectSimpleSourceClipName].get();
        if (!is_output && !is_source)
        {
            return kOfxStatFailed;
        }
        const auto offset = static_cast<int>(std::lround(time - current->frame));
        const core::rect& b = current->source.view().bounds;
        if (is_source && offset != 0 && !current->others.contains(offset))
        {
            current->others.emplace(offset, spot_card(b.width(), b.height(), offset));
        }
        const core::image& img = (is_output     ? current->output
                                  : offset != 0 ? current->others.at(offset)
                                                : current->source)
                                     .view();
        auto& p = current->images.emplace_back(std::make_unique<prop_set>());
        const auto h = handle(*p);
        // core buffers are y down; OFX rows go bottom up, so hand out the last row with a negative stride
        const int w = img.bounds.width();
        const int hgt = img.bounds.height();
        set_value(h, kOfxImagePropData, 0, static_cast<void*>(img.at(0, hgt - 1)));
        set_value(h, kOfxImagePropBounds, 0, 0);
        set_value(h, kOfxImagePropBounds, 1, 0);
        set_value(h, kOfxImagePropBounds, 2, w);
        set_value(h, kOfxImagePropBounds, 3, hgt);
        set_value(h, kOfxImagePropRowBytes, 0, -static_cast<int>(img.stride * sizeof(float)));
        set_value(h, kOfxImageEffectPropPixelDepth, 0, std::string(kOfxBitDepthFloat));
        set_value(h, kOfxImageEffectPropComponents, 0, std::string(kOfxImageComponentRGBA));
        set_value(h, kOfxImageEffectPropPreMultiplication, 0, std::string(kOfxImagePreMultiplied));
        *out = h;
        return kOfxStatOK;
    }
    static OfxStatus clip_release_image(OfxPropertySetHandle)
    {
        return kOfxStatOK;
    }
    static OfxStatus clip_get_rod(OfxImageClipHandle, OfxTime, OfxRectD* r)
    {
        const core::rect& b = current->source.view().bounds;
        *r = OfxRectD{0, 0, static_cast<double>(b.width()), static_cast<double>(b.height())};
        return kOfxStatOK;
    }

    static OfxStatus param_define(OfxParamSetHandle s, const char* type, const char* name, OfxPropertySetHandle* out)
    {
        auto& p = reinterpret_cast<effect*>(s)->params[name];
        p = std::make_unique<param>();
        p->type = type;
        if (out != nullptr)
        {
            *out = handle(p->props);
        }
        return kOfxStatOK;
    }
    static OfxStatus param_get_handle(OfxParamSetHandle s, const char* name, OfxParamHandle* out,
                                      OfxPropertySetHandle* props)
    {
        const auto& params = reinterpret_cast<effect*>(s)->params;
        const auto it = params.find(name);
        if (it == params.end())
        {
            return kOfxStatErrUnknown;
        }
        *out = reinterpret_cast<OfxParamHandle>(it->second.get());
        if (props != nullptr)
        {
            *props = handle(it->second->props);
        }
        return kOfxStatOK;
    }
    static OfxStatus param_get_property_set(OfxParamHandle p, OfxPropertySetHandle* out)
    {
        *out = handle(reinterpret_cast<param*>(p)->props);
        return kOfxStatOK;
    }

    // every param returns its default value
    static OfxStatus param_values(OfxParamHandle h, va_list args)
    {
        auto* p = reinterpret_cast<param*>(h);
        const auto ph = handle(p->props);
        if (p->type == kOfxParamTypeBoolean || p->type == kOfxParamTypeChoice)
        {
            return get_value(ph, kOfxParamPropDefault, 0, va_arg(args, int*));
        }
        const int n = p->type == kOfxParamTypeRGB ? 3 : p->type == kOfxParamTypeDouble2D ? 2 : 1;
        // OFX 1.4: absolute positions with a normalized default are scaled to the project by the host
        char* coords = nullptr;
        const bool normalized_default =
            prop_get_string(ph, kOfxParamPropDefaultCoordinateSystem, 0, &coords) == kOfxStatOK &&
            std::string_view(coords) == kOfxParamCoordinatesNormalised;
        const core::rect& project = current->source.view().bounds;
        for (int i = 0; i < n; ++i)
        {
            double* out = va_arg(args, double*);
            if (get_value(ph, kOfxParamPropDefault, i, out) != kOfxStatOK)
            {
                return kOfxStatErrUnknown;
            }
            if (normalized_default)
            {
                *out *= i == 0 ? project.width() : project.height();
            }
        }
        return kOfxStatOK;
    }
    static OfxStatus param_get_value(OfxParamHandle h, ...)
    {
        va_list args;
        va_start(args, h);
        const OfxStatus s = param_values(h, args);
        va_end(args);
        return s;
    }
    static OfxStatus param_get_value_at_time(OfxParamHandle h, OfxTime time, ...)
    {
        va_list args;
        va_start(args, time);
        const OfxStatus s = param_values(h, args);
        va_end(args);
        return s;
    }

    static OfxPropertySuiteV1 property_suite()
    {
        OfxPropertySuiteV1 s{};
        s.propSetPointer = prop_set_pointer;
        s.propSetString = prop_set_string;
        s.propSetDouble = prop_set_double;
        s.propSetInt = prop_set_int;
        s.propGetPointer = prop_get_pointer;
        s.propGetString = prop_get_string;
        s.propGetDouble = prop_get_double;
        s.propGetInt = prop_get_int;
        return s;
    }

    static OfxImageEffectSuiteV1 effect_suite()
    {
        OfxImageEffectSuiteV1 s{};
        s.getPropertySet = get_property_set;
        s.getParamSet = get_param_set;
        s.clipDefine = clip_define;
        s.clipGetHandle = clip_get_handle;
        s.clipGetPropertySet = clip_get_property_set;
        s.clipGetImage = clip_get_image;
        s.clipReleaseImage = clip_release_image;
        s.clipGetRegionOfDefinition = clip_get_rod;
        return s;
    }

    static OfxParameterSuiteV1 parameter_suite()
    {
        OfxParameterSuiteV1 s{};
        s.paramDefine = param_define;
        s.paramGetHandle = param_get_handle;
        s.paramGetPropertySet = param_get_property_set;
        s.paramGetValue = param_get_value;
        s.paramGetValueAtTime = param_get_value_at_time;
        return s;
    }

    static const void* fetch_suite(OfxPropertySetHandle, const char* name, int)
    {
        static const OfxPropertySuiteV1 PROPS = property_suite();
        static const OfxImageEffectSuiteV1 EFFECTS = effect_suite();
        static const OfxParameterSuiteV1 PARAMS = parameter_suite();
        const std::string_view n(name);
        return n == kOfxPropertySuite      ? static_cast<const void*>(&PROPS)
               : n == kOfxImageEffectSuite ? static_cast<const void*>(&EFFECTS)
               : n == kOfxParameterSuite   ? static_cast<const void*>(&PARAMS)
                                           : nullptr;
    }

    static void run_plugin(const std::string& path)
    {
        HMODULE lib = LoadLibraryA(path.c_str());
        check(lib != nullptr, std::format("load {}", path));
        if (lib == nullptr)
        {
            return;
        }
        using get_count_fn = int (*)();
        using get_plugin_fn = OfxPlugin* (*)(int);
        auto get_count = reinterpret_cast<get_count_fn>(GetProcAddress(lib, "OfxGetNumberOfPlugins"));
        auto get_plugin = reinterpret_cast<get_plugin_fn>(GetProcAddress(lib, "OfxGetPlugin"));
        check(get_count != nullptr && get_plugin != nullptr && get_count() == 1, "exports");
        OfxPlugin* plugin = get_plugin(0);

        prop_set host_props;
        OfxHost host{handle(host_props), fetch_suite};
        plugin->setHost(&host);

        effect e;
        current = &e;
        const auto h = static_cast<const void*>(&e);
        check(plugin->mainEntry(kOfxActionLoad, nullptr, nullptr, nullptr) == kOfxStatOK, "load action");
        check(plugin->mainEntry(kOfxActionDescribe, h, nullptr, nullptr) == kOfxStatOK, "describe");
        prop_set ctx;
        set_value(handle(ctx), kOfxImageEffectPropContext, 0, std::string(kOfxImageEffectContextFilter));
        check(plugin->mainEntry(kOfxImageEffectActionDescribeInContext, h, handle(ctx), nullptr) == kOfxStatOK,
              "describe in context");
        check(plugin->mainEntry(kOfxActionCreateInstance, h, nullptr, nullptr) == kOfxStatOK, "create instance");

        std::string label;
        char* label_ptr = nullptr;
        if (prop_get_string(handle(e.props), kOfxPropLabel, 0, &label_ptr) == kOfxStatOK)
        {
            label = label_ptr;
        }
        const core::effect* reference = effects::catalog::find(label);
        check(reference != nullptr, std::format("{}: label '{}' names a known effect", path, label));
        if (reference == nullptr)
        {
            return;
        }
        const auto expected_params = std::ranges::count_if(
            reference->params, [](const core::param& p) { return p.kind != core::param_kind::group_end; });
        check(static_cast<std::ptrdiff_t>(e.params.size()) == expected_params, std::format("{}: param count", label));

        constexpr int W = 96;
        constexpr int H = 64;
        constexpr double FRAME = 7.0;
        constexpr double FPS = 24.0;
        e.source = spot_card(W, H, 0);
        e.frame = FRAME;
        e.output.reset(core::rect{0, 0, W, H}, 4);
        set_value(handle(e.clips[kOfxImageEffectOutputClipName]->props), kOfxImageEffectPropFrameRate, 0, FPS);
        for (auto& [name, c] : e.clips)
        {
            set_value(handle(c->props), kOfxImageClipPropConnected, 0,
                      name == kOfxImageEffectSimpleSourceClipName || name == kOfxImageEffectOutputClipName ? 1 : 0);
        }
        prop_set args;
        const auto ah = handle(args);
        set_value(ah, kOfxPropTime, 0, FRAME);
        for (int i = 0; i < 4; ++i)
        {
            set_value(ah, kOfxImageEffectPropRenderWindow, i, i == 2 ? W : i == 3 ? H : 0);
        }
        set_value(ah, kOfxImageEffectPropRenderScale, 0, 1.0);
        set_value(ah, kOfxImageEffectPropRenderScale, 1, 1.0);
        check(plugin->mainEntry(kOfxImageEffectActionRender, h, ah, nullptr) == kOfxStatOK,
              std::format("{}: render", label));

        // the same render straight through the core, with default params
        core::param_values values(reference->params);
        core::render_args ra;
        ra.frame_duration = 1.0 / FPS;
        ra.time = FRAME / FPS;
        const std::vector<int> offsets = reference->offsets(values, ra.frame_number());
        int temporal = -1;
        get_value(handle(e.props), kOfxImageEffectPropTemporalClipAccess, 0, &temporal);
        check(temporal == (reference->frame_offsets != nullptr ? 1 : 0), std::format("{}: temporal access", label));
        prop_set needed;
        const OfxStatus needed_status = plugin->mainEntry(kOfxImageEffectActionGetFramesNeeded, h, ah, handle(needed));
        const auto& ranges =
            needed.values[std::string("OfxImageClipPropFrameRange_") + kOfxImageEffectSimpleSourceClipName];
        // the ranges must cover exactly the current frame and every offset
        std::set<int> wanted(offsets.begin(), offsets.end());
        wanted.insert(0);
        std::set<int> covered;
        bool ranges_ok = ranges.size() % 2 == 0;
        for (std::size_t i = 0; ranges_ok && i < ranges.size(); i += 2)
        {
            const double lo = std::get<double>(ranges[i]) - FRAME;
            const double hi = std::get<double>(ranges[i + 1]) - FRAME;
            ranges_ok = lo <= hi;
            for (int n = static_cast<int>(lo); ranges_ok && n <= static_cast<int>(hi); ++n)
            {
                covered.insert(n);
            }
        }
        ranges_ok = ranges_ok && covered == wanted;
        check(reference->frame_offsets != nullptr ? needed_status == kOfxStatOK && ranges_ok
                                                  : needed_status == kOfxStatReplyDefault,
              std::format("{}: frames needed", label));
        // the frames the core asks for, served as the mock host serves them
        std::set<int> fetched;
        std::set<int> calls;
        bool repeated = false;
        ra.frame_at = [&](int offset) {
            core::buffer b;
            repeated = repeated || !calls.insert(offset).second;
            if (std::ranges::find(offsets, offset) != offsets.end())
            {
                fetched.insert(offset);
                b = spot_card(W, H, offset);
            }
            return b;
        };
        core::buffer expected(core::rect{0, 0, W, H}, 4);
        ra.src = e.source.view();
        ra.dst = expected.view();
        ra.params = &values;
        ra.geometry = centered_frame(W, H);
        reference->render(ra);
        check(std::ranges::equal(e.others | std::views::keys, fetched),
              std::format("{}: render fetches the source at the frames the core reads", label));
        check(!repeated, std::format("{}: render fetches each frame offset at most once", label));

        double max_diff = 0.0;
        double changed = 0.0;
        for (int y = 0; y < H; ++y)
        {
            for (int x = 0; x < W; ++x)
            {
                for (int c = 0; c < 4; ++c)
                {
                    max_diff = std::max(max_diff, std::abs(static_cast<double>(e.output.view().at(x, y)[c] -
                                                                               expected.view().at(x, y)[c])));
                    changed = std::max(changed, std::abs(static_cast<double>(e.output.view().at(x, y)[c] -
                                                                             e.source.view().at(x, y)[c])));
                }
            }
        }
        check(max_diff < 1e-5, std::format("{}: OFX render matches core (max diff {})", label, max_diff));
        std::cout << std::format("{}: {} params, max diff vs core {:.2e}, max change vs source {:.3f}\n", label,
                                 e.params.size(), max_diff, changed);
        current = nullptr;
    }
} // namespace functional_fx_suite::tests::ofx_host_test

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        functional_fx_suite::tests::ofx_host_test::run_plugin(argv[i]);
    }
    return functional_fx_suite::tests::finish("OFX host ");
}
