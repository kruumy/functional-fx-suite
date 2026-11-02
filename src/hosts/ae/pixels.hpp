#pragma once

#include "AE_Effect.h"
#include "AE_EffectPixelFormat.h"
#include "core/buffer.hpp"

namespace functional_fx_suite::hosts::ae::pixels
{
    // copies an AE world (straight ARGB) into a premultiplied float RGBA buffer;
    // `bounds` places the world's pixels in the core pixel space
    [[nodiscard]] core::buffer read(const PF_EffectWorld& world, PF_PixelFormat format, core::rect bounds);

    // writes premultiplied float RGBA into an AE world as straight ARGB
    void write(const core::image& src, PF_EffectWorld& world, PF_PixelFormat format);
} // namespace functional_fx_suite::hosts::ae::pixels
