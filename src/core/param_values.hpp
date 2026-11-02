#pragma once

#include "core/param.hpp"
#include <array>
#include <cmath>
#include <span>
#include <stdexcept>
#include <type_traits>

namespace functional_fx_suite::core
{
    template <class E>
    concept param_index = std::is_enum_v<E>;

    // current values of an effect's parameters, indexed like its param table
    class param_values
    {
      public:
        static constexpr std::size_t MAX_PARAMS = 128;
        using value = std::array<double, 3>;

        explicit param_values(std::span<const param> table)
        {
            if (table.size() > MAX_PARAMS)
            {
                throw std::length_error("param table too large");
            }
            for (std::size_t i = 0; i < table.size(); ++i)
            {
                values_[i] = table[i].def;
            }
        }

        void set(std::size_t i, value v)
        {
            values_.at(i) = v;
        }
        [[nodiscard]] const value& get(std::size_t i) const
        {
            return values_.at(i);
        }

        template <param_index E> [[nodiscard]] double scalar(E e) const
        {
            return get(index(e))[0];
        }
        template <param_index E> [[nodiscard]] float scalar_f(E e) const
        {
            return static_cast<float>(scalar(e));
        }
        template <param_index E> [[nodiscard]] bool toggle(E e) const
        {
            return scalar(e) >= 0.5;
        }
        template <param_index E> [[nodiscard]] int choice(E e) const
        {
            return static_cast<int>(std::lround(scalar(e)));
        }
        template <param_index E> [[nodiscard]] const value& vec(E e) const
        {
            return get(index(e));
        }

      private:
        template <param_index E> static constexpr std::size_t index(E e)
        {
            return static_cast<std::size_t>(e);
        }

        std::array<value, MAX_PARAMS> values_{};
    };
} // namespace functional_fx_suite::core
