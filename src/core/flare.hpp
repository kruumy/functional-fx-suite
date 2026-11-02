#pragma once

#include "core/color.hpp"
#include "core/glow.hpp"
#include "core/image.hpp"
#include <cstdint>
#include <span>
#include <string_view>

namespace functional_fx_suite::core::flare
{
    enum class kind : std::uint8_t
    {
        glow,   // soft disc
        ghost,  // circle or polygon with a soft edge
        star,   // rays from the centre
        streak, // a line through the centre, along Rays Rotate
    };

    // which group of the effect's controls steers an element
    enum class role : std::uint8_t
    {
        hotspot, // Hotspot Bright / Color
        rays,    // Hotspot Color x Rays Brightness, Rays Num Scale / Length / Thickness / Rotate, atmosphere
        other,   // Other Brightness / Color / Width
    };

    struct element
    {
        kind shape = kind::glow;
        role steer = role::other;
        double position = 1; // along the axis: 0 at the pivot, 1 at the hotspot, -1 mirrored past the pivot
        double size = 0.1;   // radius (star and streak: length) in flare units
        color::rgb color{1, 1, 1};
        float brightness = 1;
        double aspect = 1;       // height / width
        int sides = 0;           // ghost: 0 is a circle
        double rotate = 0;       // degrees, counter-clockwise
        double softness = 0.2;   // ghost: edge width as a fraction of the radius
        double rim = 0;          // ghost: above 0 brightens the rim, below 0 the centre
        double chroma = 0;       // ghost: red radius grows and blue shrinks by this fraction
        int rays = 0;            // star: number of rays
        double thickness = 0.01; // star and streak: half width as a fraction of the length
        double vary = 0;         // star: random shortening of each ray, 0..1
        bool priority = false;   // kept by Performance: Priority Only
    };

    struct lens
    {
        std::string_view name;
        std::span<const element> elements;
    };

    // the built-in lenses, in the order of the Lens popup
    [[nodiscard]] std::span<const lens> lenses();

    // placement and the effect controls that steer the elements; positions in pixels (y down)
    struct settings
    {
        double hotspot_x = 0;
        double hotspot_y = 0;
        double pivot_x = 0;
        double pivot_y = 0;
        double unit = 1;        // pixels per flare unit
        double rel_height = 1;  // height / width of every element
        double width_scale = 1; // triggers and occlusion
        double gamma = 1;
        float saturation = 1;
        double rays_rotate = 0; // degrees
        double rays_num_scale = 1;
        double rays_length = 1;
        double rays_thickness = 1;
        double other_width = 1;
        color::rgb hotspot_color{1, 1, 1};
        color::rgb rays_color{1, 1, 1};
        color::rgb other_color{1, 1, 1};
        bool priority_only = false;
        const glow::atmosphere* atmosphere = nullptr; // on the rays elements
        frame geometry;
    };

    // adds the lens's elements to the rgb of dst
    void draw(const image& dst, const lens& l, const settings& s);
} // namespace functional_fx_suite::core::flare
