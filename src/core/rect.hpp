#pragma once

namespace functional_fx_suite::core
{
    // half-open pixel rectangle, y down
    struct rect
    {
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;

        [[nodiscard]] constexpr int width() const
        {
            return x1 - x0;
        }
        [[nodiscard]] constexpr int height() const
        {
            return y1 - y0;
        }
        [[nodiscard]] constexpr bool empty() const
        {
            return x1 <= x0 || y1 <= y0;
        }
        [[nodiscard]] constexpr bool contains(int x, int y) const
        {
            return x >= x0 && x < x1 && y >= y0 && y < y1;
        }
    };
} // namespace functional_fx_suite::core
