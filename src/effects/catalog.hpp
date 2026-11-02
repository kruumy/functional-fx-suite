#pragma once

#include "core/effect.hpp"
#include <span>
#include <string_view>

namespace functional_fx_suite::effects::catalog
{
    [[nodiscard]] std::span<const core::effect* const> all();

    // nullptr when no effect has that match name
    [[nodiscard]] const core::effect* find(std::string_view match_name);
} // namespace functional_fx_suite::effects::catalog
