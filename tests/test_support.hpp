#pragma once

#include "core/buffer.hpp"
#include "core/frame.hpp"
#include <cmath>
#include <format>
#include <iostream>
#include <string_view>

namespace functional_fx_suite::tests
{
    inline int failures = 0;

    inline void check(bool ok, std::string_view what)
    {
        if (!ok)
        {
            ++failures;
            std::cerr << "FAIL: " << what << '\n';
        }
    }

    // prints the pass/fail line for `kind` tests (e.g. "AE host ") and returns main's exit code
    inline int finish(std::string_view kind)
    {
        std::cout << (failures == 0 ? std::format("all {}tests passed\n", kind)
                                    : std::format("{} {}test(s) failed\n", failures, kind));
        return failures == 0 ? 0 : 1;
    }

    // channel c (rgb) of the moving-spot card at pixel (x, y); the spot moves 5 px per frame
    inline double spot_value(int x, int y, int c, int w, int h, int frame)
    {
        const double spot = std::exp(-std::pow(std::hypot(x - w * 0.3 - 5.0 * frame, y - h * 0.4) / 6.0, 2.0));
        return c == 0 ? 0.2 + 0.5 * x / w + spot : c == 1 ? 0.3 + spot : ((x / 8 + y / 8) % 2 == 0 ? 0.8 : 0.2);
    }

    // the moving-spot card as an opaque float buffer, `frame` frames from now
    inline core::buffer spot_card(int w, int h, int frame)
    {
        core::buffer b(core::rect{0, 0, w, h}, 4);
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                float* p = b.view().at(x, y);
                for (int c = 0; c < 3; ++c)
                {
                    p[c] = static_cast<float>(spot_value(x, y, c, w, h, frame));
                }
                p[3] = 1.0f;
            }
        }
        return b;
    }

    // a w x h frame centred in its pixels, as the hosts describe a layer
    inline core::frame centered_frame(int w, int h)
    {
        return core::frame{w * 0.5, h * 0.5, static_cast<double>(w)};
    }
} // namespace functional_fx_suite::tests
