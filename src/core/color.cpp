#include "core/color.hpp"
#include <algorithm>
#include <cmath>

namespace functional_fx_suite::core::color
{
    static float positive_luma(const rgb& c)
    {
        return sat_luma({std::max(c[0], 0.0f), std::max(c[1], 0.0f), std::max(c[2], 0.0f)});
    }

    rgb hue_shift_preserve_luma(const rgb& c, float turns)
    {
        const float before = positive_luma(c);
        // mostly negative colors rotate as their negation
        const float sign = std::ranges::max(c) < -std::ranges::min(c) ? -1.0f : 1.0f;
        const rgb v{c[0] * sign, c[1] * sign, c[2] * sign};
        const float hi = std::ranges::max(v);
        const float delta = hi - std::ranges::min(v);
        if (hi == 0.0f || delta == 0.0f)
        {
            return c;
        }
        float sextant = 0.0f;
        if (v[0] == hi)
        {
            sextant = (v[1] - v[2]) / delta;
            sextant += sextant < 0.0f ? 6.0f : 0.0f;
        }
        else if (v[1] == hi)
        {
            sextant = (v[2] - v[0]) / delta + 2.0f;
        }
        else
        {
            sextant = (v[0] - v[1]) / delta + 4.0f;
        }
        const float hue = sextant / 6.0f + turns;
        float h6 = (hue - std::floor(hue)) * 6.0f;
        h6 = h6 >= 6.0f ? 0.0f : h6;
        const int i = static_cast<int>(h6);
        const float f = h6 - static_cast<float>(i);
        const float p = hi - delta;
        const float q = hi - delta * f;
        const float t = hi - delta * (1.0f - f);
        static constexpr std::array<std::array<int, 3>, 6> PICK{
            {{0, 3, 1}, {2, 0, 1}, {1, 0, 3}, {1, 2, 0}, {3, 1, 0}, {0, 1, 2}}};
        const std::array<float, 4> parts{hi, p, q, t};
        rgb out{};
        for (int k = 0; k < 3; ++k)
        {
            out[k] = parts[PICK[i][k]] * sign;
        }
        const float after = positive_luma(out);
        if (after > 0.0f && after != before)
        {
            const float scale = before / after;
            for (float& o : out)
            {
                o *= scale;
            }
        }
        return out;
    }

    rgb hsv_to_rgb(double hue, double saturation, double value)
    {
        const double h6 = (hue - std::floor(hue)) * 6.0;
        rgb out{};
        for (int c = 0; c < 3; ++c)
        {
            // distance of the hue from this channel's primary (red 0, green 1/3, blue 2/3), in sextants
            double d = std::abs(h6 - 2.0 * c);
            d = std::min(d, 6.0 - d);
            const double pure = std::clamp(2.0 - d, 0.0, 1.0);
            out[c] = static_cast<float>(value * (1.0 - saturation * (1.0 - pure)));
        }
        return out;
    }
} // namespace functional_fx_suite::core::color
