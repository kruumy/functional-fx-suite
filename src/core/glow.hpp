#pragma once

#include "core/blur.hpp"
#include "core/buffer.hpp"
#include "core/render_args.hpp"
#include "core/sample.hpp"
#include <array>
#include <cstdint>

namespace functional_fx_suite::core::glow
{
    using rgb = std::array<double, 3>;

    // the source a glow starts from, over the source bounds: mixed toward its alpha by `from_alpha`, rgb times the
    // matte (1 or 3 channels) when there is one
    [[nodiscard]] buffer source(const render_args& args, const image* matte, double from_alpha);

    // hotspots on rgb: keeps hue, v * sum(max(v - t, 0)) / sum(max(v, 0))
    void hotspots(const image& img, const rgb& th);

    // glow threshold on rgb: hotspots scaled by 1 / max(1/128, 1 - min threshold), never above the input
    void threshold(const image& img, const rgb& th);

    // one gaussian per rgb channel (pixel amounts), then times gain; alpha is left alone
    void blur_channels(const image& img, const rgb& amount_x, const rgb& amount_y, const rgb& gain, blur::edge mode);

    // atmosphere: 1 - amp * fractal noise that drifts with time
    struct atmosphere
    {
        double amp = 0;
        double freq = 1;
        double detail = 0.6;
        std::uint32_t seed = 0;
        double z = 0;

        [[nodiscard]] float at(double nx, double ny) const;
    };

    [[nodiscard]] atmosphere make_atmosphere(double amp, double freq, double detail, double seed, double speed,
                                             double time);

    // rgb times the atmosphere
    void apply(const atmosphere& a, const image& img, const frame& f);

    // order matches the "Combine" popup
    enum class combine : std::uint8_t
    {
        mult,
        add,
        screen,
        difference,
        overlay
    };

    // one glow over one base, all four channels; `mult_overlay` is UltraGlow's Mult (product overlaid on the base)
    [[nodiscard]] sample::pixel blend(combine mode, const sample::pixel& g, const sample::pixel& base,
                                      bool mult_overlay);

    struct composite_settings
    {
        double affect_alpha = 0;
        combine mode = combine::screen;
        double glow_under_source = 0;
        double light_bg = 0;
        double scale_background = 1;
        double scale_source = 1;
        bool show = false;
        bool mult_overlay = false;
    };

    // glow composite into args.dst: the glow (rgb) over `source` and the optional background;
    // Show displays `shown` (rgb) instead
    void composite(const render_args& args, const image& source, const image& glow, const image& shown,
                   const composite_settings& c);
} // namespace functional_fx_suite::core::glow
