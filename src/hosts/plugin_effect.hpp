#pragma once

#include "core/effect.hpp"

namespace functional_fx_suite::hosts::plugin_effect
{
    // the one effect a plugin binary wraps; defined by a file generated per plugin target
    [[nodiscard]] const core::effect& get();
} // namespace functional_fx_suite::hosts::plugin_effect
