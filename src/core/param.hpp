#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace functional_fx_suite::core
{
    enum class param_kind : std::uint8_t
    {
        scalar,
        toggle,
        choice,
        color,
        point, // normalized frame units; host UI shows a position
        group_begin,
        group_end
    };

    // one host-independent parameter description; both adapters build their UI from it
    struct param
    {
        param_kind kind = param_kind::scalar;
        std::uint16_t id = 0; // stable host id, never reuse or renumber
        std::string_view name;
        std::string_view label;
        std::array<double, 3> def{};
        double min = 0.0;
        double max = 1000.0;
        double slider_min = 0.0;
        double slider_max = 1.0;
        std::string_view choices; // '|' separated
        int precision = 3;
        int enable_param = -1;         // choice param index controlling enablement
        std::uint32_t enable_mask = 0; // bit i: enabled while that choice equals i

        static constexpr param make(param_kind kind, std::uint16_t id, std::string_view name, std::string_view label)
        {
            param p;
            p.kind = kind;
            p.id = id;
            p.name = name;
            p.label = label;
            return p;
        }
        static constexpr param scalar(std::uint16_t id, std::string_view name, std::string_view label)
        {
            return make(param_kind::scalar, id, name, label);
        }
        static constexpr param toggle(std::uint16_t id, std::string_view name, std::string_view label)
        {
            return make(param_kind::toggle, id, name, label).range(0, 1);
        }
        static constexpr param choice(std::uint16_t id, std::string_view name, std::string_view label,
                                      std::string_view choices)
        {
            param p = make(param_kind::choice, id, name, label);
            p.choices = choices;
            return p;
        }
        static constexpr param color(std::uint16_t id, std::string_view name, std::string_view label)
        {
            return make(param_kind::color, id, name, label);
        }
        static constexpr param point(std::uint16_t id, std::string_view name, std::string_view label)
        {
            return make(param_kind::point, id, name, label);
        }
        static constexpr param group(std::uint16_t id, std::string_view name, std::string_view label)
        {
            return make(param_kind::group_begin, id, name, label);
        }
        static constexpr param group_end(std::uint16_t id, std::string_view name)
        {
            return make(param_kind::group_end, id, name, name);
        }

        constexpr param value(double v) const
        {
            param p = *this;
            p.def = {v, v, v};
            return p;
        }
        constexpr param value(std::array<double, 3> v) const
        {
            param p = *this;
            p.def = v;
            return p;
        }
        constexpr param range(double lo, double hi) const
        {
            param p = *this;
            p.min = lo;
            p.max = hi;
            return p.slider(lo, hi);
        }
        constexpr param slider(double lo, double hi) const
        {
            param p = *this;
            p.slider_min = lo;
            p.slider_max = hi;
            return p;
        }
        constexpr param digits(int d) const
        {
            param p = *this;
            p.precision = d;
            return p;
        }
        constexpr param enabled_when(int choice_param, std::uint32_t mask) const
        {
            param p = *this;
            p.enable_param = choice_param;
            p.enable_mask = mask;
            return p;
        }

        // number of options of a choice param
        [[nodiscard]] constexpr int choice_count() const
        {
            return static_cast<int>(std::ranges::count(choices, '|')) + 1;
        }
        // whether this param is in use while its controlling choice equals `choice`
        [[nodiscard]] constexpr bool enabled_for(int choice) const
        {
            return choice >= 0 && choice < 32 && ((enable_mask >> choice) & 1U) != 0;
        }
    };
} // namespace functional_fx_suite::core
