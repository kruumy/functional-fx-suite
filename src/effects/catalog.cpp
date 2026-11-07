#include "effects/catalog.hpp"
#include "effects/ascii.hpp"
#include "effects/badcolors.hpp"
#include "effects/blurmocurves.hpp"
#include "effects/digitaldamage.hpp"
#include "effects/digitize.hpp"
#include "effects/distort.hpp"
#include "effects/distortblur.hpp"
#include "effects/distortchroma.hpp"
#include "effects/distortrgb.hpp"
#include "effects/duotone.hpp"
#include "effects/edgecolorize.hpp"
#include "effects/edgedetect.hpp"
#include "effects/edgerays.hpp"
#include "effects/filmdamage.hpp"
#include "effects/flicker.hpp"
#include "effects/freezeframe.hpp"
#include "effects/glint.hpp"
#include "effects/glow.hpp"
#include "effects/glowaura.hpp"
#include "effects/glowdark.hpp"
#include "effects/glowrings.hpp"
#include "effects/halftone.hpp"
#include "effects/halftonecolor.hpp"
#include "effects/heatdistortion.hpp"
#include "effects/hotspots.hpp"
#include "effects/jitterframes.hpp"
#include "effects/jpegdamage.hpp"
#include "effects/lensdirt.hpp"
#include "effects/lensflare.hpp"
#include "effects/mosaic.hpp"
#include "effects/motiondetect.hpp"
#include "effects/pixelsort.hpp"
#include "effects/pseudocolor.hpp"
#include "effects/psykoblobs.hpp"
#include "effects/psykostripes.hpp"
#include "effects/rays.hpp"
#include "effects/reverseedits.hpp"
#include "effects/rollingbar.hpp"
#include "effects/scanlines.hpp"
#include "effects/shake.hpp"
#include "effects/timedisplace.hpp"
#include "effects/timewarprgb.hpp"
#include "effects/tint.hpp"
#include "effects/tritone.hpp"
#include "effects/tvdamage.hpp"
#include "effects/ultraglow.hpp"
#include "effects/vignette.hpp"
#include "effects/warpchroma.hpp"
#include "effects/warpfisheye.hpp"
#include "effects/warppuddle.hpp"
#include "effects/zblur.hpp"
#include "effects/zdefocus.hpp"
#include <algorithm>
#include <array>

namespace functional_fx_suite::effects::catalog
{
    std::span<const core::effect* const> all()
    {
        static const std::array<const core::effect*, 52> EFFECTS{
            &shake::get(),         &tint::get(),          &rays::get(),         &glow::get(),
            &ultraglow::get(),     &glowaura::get(),      &glowdark::get(),     &glowrings::get(),
            &glint::get(),         &hotspots::get(),      &edgerays::get(),     &vignette::get(),
            &badcolors::get(),     &duotone::get(),       &tritone::get(),      &edgedetect::get(),
            &edgecolorize::get(),  &flicker::get(),       &mosaic::get(),       &scanlines::get(),
            &halftone::get(),      &halftonecolor::get(), &jpegdamage::get(),   &pixelsort::get(),
            &digitaldamage::get(), &tvdamage::get(),      &filmdamage::get(),   &distort::get(),
            &distortblur::get(),   &distortchroma::get(), &distortrgb::get(),   &warpchroma::get(),
            &warpfisheye::get(),   &warppuddle::get(),    &zblur::get(),        &blurmocurves::get(),
            &pseudocolor::get(),   &psykoblobs::get(),    &psykostripes::get(), &ascii::get(),
            &rollingbar::get(),    &zdefocus::get(),      &lensflare::get(),    &timewarprgb::get(),
            &timedisplace::get(),  &motiondetect::get(),  &freezeframe::get(),  &jitterframes::get(),
            &reverseedits::get(),  &lensdirt::get(),      &digitize::get(),     &heatdistortion::get()};
        return EFFECTS;
    }

    const core::effect* find(std::string_view match_name)
    {
        const auto effects = all();
        const auto it = std::ranges::find(effects, match_name, &core::effect::match_name);
        return it != effects.end() ? *it : nullptr;
    }
} // namespace functional_fx_suite::effects::catalog
