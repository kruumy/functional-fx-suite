# How to add an effect

An effect is one `.hpp/.cpp` pair in `src/effects/`. The AE and OFX adapters build their UI and render calls from
its param table, and CMake builds one binary per host from it. Use `src/effects/tint.cpp` as the template.
Build and test with `cmake --workflow --preset release`.

## Rules

- Name: `f_<name>`, lowercase, short (`f_shake`, `f_glow`). Files `src/effects/<name>.hpp/.cpp`, namespace
  `functional_fx_suite::effects::<name>`. Category: `Functional <Adjust|Blur|Distort|Lighting|Render|Stylize|Time>`.
- Fully local: no settings files, registry, `%APPDATA%`, network or file reads. Read only what the host hands over.
- No new dependencies; the C++ standard library and `src/core` only.
- C++20, `snake_case`, `SCREAMING_SNAKE_CASE` constants, no anonymous namespaces, no `using namespace`.
  Run `clang-format -i` on touched files. Comments only for what the code can't say.
- Effects must be re-entrant (AE multi-frame rendering, OFX fully safe). Parallelize rows with
  `core::parallel::for_range`.
- Errors: throw; the adapters catch at the host boundary.

## Pixels and units

- `render_args::src` / `dst` are float RGBA **premultiplied**, one pixel space, y down. `dst.bounds` is the output
  window; fill every pixel of it. Unpremultiply around color math that needs straight color.
- `args.geometry` (`core::frame`) maps frame units (origin at the frame centre, 1.0 = frame width, y up) to pixels.
  Distances, blur widths and points are params in frame units, so values don't depend on resolution.
  Blur widths go through `core::blur::amount_px`.
- `args.time` is in seconds, `args.frame_duration` per frame.
- Optional layers: `args.mask` and `args.background` (null when not connected).

## Steps

### 1. Header `src/effects/<name>.hpp`

```cpp
#pragma once

#include "core/effect.hpp"

namespace functional_fx_suite::effects::<name>
{
    [[nodiscard]] const core::effect& get();
} // namespace functional_fx_suite::effects::<name>
```

### 2. Param table in `src/effects/<name>.cpp`

An `enum class p` lists the params in UI order and ends with `count`; `PARAMS` has the same order.

```cpp
enum class p : std::uint8_t { amount, color, mask_use, blur_mask, invert_mask, count };

static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
    param::scalar(1, "amount", "Amount").value(1).range(0, 1000).slider(0, 2),
    param::color(2, "color", "Color").value({1, 1, 1}),
    param::choice(3, "mask_use", "Mask Use", "Luma|Alpha"),
    param::scalar(4, "blur_mask", "Blur Mask").value(0.05).range(0, 1000).slider(0, 2),
    param::toggle(5, "invert_mask", "Invert Mask"),
};
```

- Builders: `scalar`, `toggle`, `choice` (`'|'`-separated, 0-based), `color`, `point` (frame units),
  `group(id, name, label)` ... `group_end(id, name)`.
- Modifiers: `.value(v)` or `.value({x, y, z})`, `.range(lo, hi)` (also sets the slider), `.slider(lo, hi)`,
  `.digits(n)`, `.enabled_when(choice_index, mask)` (bit i: shown while that choice equals i).
- `id` is the stable host id saved in projects and presets. Ids start at 1, are unique per effect, and are never
  renumbered or reused once released. Add new params with new ids. Ids 900 and 901 are taken by the adapters.
- `name` is unique per effect and is what `fxrender name=value` sets.
- At most 128 params (`core::param_values::MAX_PARAMS`).

### 3. Render

```cpp
static void render(const core::render_args& args)
{
    const core::param_values& v = *args.params;
    const double amount = v.scalar(p::amount); // also scalar_f, toggle, choice, vec (color/point)
    const core::image& dst = args.dst;
    core::parallel::for_range(dst.bounds.y0, dst.bounds.y1, [&](int y_begin, int y_end) {
        for (int y = y_begin; y < y_end; ++y)
        {
            for (int x = dst.bounds.x0; x < dst.bounds.x1; ++x)
            {
                float* d = dst.at(x, y);
                // ...; zero d when !args.src.bounds.contains(x, y)
            }
        }
    });
}
```

Reuse `src/core` before writing new helpers: `blur`, `color`, `mask` (`build` + `dissolve` for the standard Mask
params), `sample` (bilinear + wrap), `noise`, `warp`, `glow`, `edges`, `fft`, `mip`, `retime`.

Mask pattern, at the end of `render`:

```cpp
const auto mode = v.choice(p::mask_use) == 1 ? core::mask::source::alpha : core::mask::source::luma;
const core::buffer m = core::mask::build(args, mode, v.scalar(p::blur_mask), v.toggle(p::invert_mask));
if (m.view().data != nullptr)
{
    core::mask::dissolve(args, m.view());
}
```

### 4. Descriptor

```cpp
const core::effect& get()
{
    static constexpr core::effect EFFECT{
        .match_name = "f_<name>",
        .name = "f_<name>",
        .category = "Functional Stylize",
        .description = "One sentence on what it does.",
        .params = PARAMS,
        .mask_input = "Mask",     // "" for no mask layer
        .background_input = "",   // a label such as "Background" for a second layer
        .time_varying = false,    // true if the output changes over time with constant params
        .reads_outside = false,   // true if it samples source pixels outside dst.bounds
        .render = render,
    };
    return EFFECT;
}
```

Reading other times or frames:

- Params at another time: `args.params_at(seconds)`. It is empty in hosts that can't, so fall back to `args.params`.
- Other frames: set `.frame_offsets` to a function returning the distinct non-zero frame offsets the effect may read
  (given the params and `args.frame_number()`). In `render`, `args.frame_at(offset)` fetches one frame as an owned
  buffer; hold one at a time. See `src/effects/freezeframe.cpp`.
- Both need `WIDE_TIME` in step 5; the AE plugin refuses to load without it.

### 5. Register

All four are required; the category and time flag must match the descriptor.

1. `src/effects/catalog.cpp`: `#include "effects/<name>.hpp"`, add `&<name>::get()` to `all()` and bump the
   array size.
2. `CMakeLists.txt`: add `src/effects/<name>.cpp` to `add_library` (alphabetical).
3. `CMakeLists.txt`: add `<name>` to `FFX_EFFECTS` (builds the OFX plugin and adds it to the host tests).
4. `CMakeLists.txt`: add `ffx_add_ae_plugin(<name> CATEGORY "Functional Stylize")`, with `TIME_VARYING` when
   `.time_varying` is true and `WIDE_TIME` when it uses `params_at` or `frame_offsets`.

### 6. Test

1. In `tests/effects_test.cpp`, add checks in the matching group function (`identities`, `stylize`, `warps`, ...)
   with `run("f_<name>", src.view(), setup)`: an identity case (e.g. amount 0 equals the source), a visible change at
   the defaults, determinism (same output twice) and anything specific to the effect.
2. `cmake --workflow --preset release`. `effects_test` checks the param table (unique ids and names, defaults in
   range); `ae_host_test` and `ofx_host_test` check the built plugins against the core render.
3. Look at it: `build/fxrender f_<name> amount=0.5 --out out.ppm`.
4. Before release, run an ASan + UBSan build if you have one set up (a local `CMakeUserPresets.json` preset).

## Changing an existing effect

- Never change a released param's id, kind or meaning. Add params with new ids; old projects keep loading.
- Keep the defaults unless the old look was a bug.
- `src/effects/shake.cpp`, `flicker.cpp` and `blurmocurves.cpp` are used by `tools/presets`: after changing their
  params, re-save `tools/presets/templates/f_<name>.ffx` from AE (only that effect, at its defaults) and update the
  label renames in `EFFECTS` in `tools/presets/sapphire_presets.py` if a label changed.
