#include "hosts/ae/pixels.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace functional_fx_suite::hosts::ae::pixels
{
    template <class Pixel> static Pixel* world_row(const PF_EffectWorld& world, int y)
    {
        return reinterpret_cast<Pixel*>(static_cast<char*>(static_cast<void*>(world.data)) +
                                        static_cast<std::ptrdiff_t>(y) * world.rowbytes);
    }

    template <class Pixel> static void read_rows(const PF_EffectWorld& world, const core::image& dst, float scale)
    {
        core::parallel::for_range(0, world.height, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                const Pixel* row = world_row<const Pixel>(world, y);
                float* d = dst.at(dst.bounds.x0, dst.bounds.y0 + y);
                for (int x = 0; x < world.width; ++x, d += 4)
                {
                    const Pixel& p = row[x];
                    const float a = static_cast<float>(p.alpha) * scale;
                    d[0] = static_cast<float>(p.red) * scale * a;
                    d[1] = static_cast<float>(p.green) * scale * a;
                    d[2] = static_cast<float>(p.blue) * scale * a;
                    d[3] = a;
                }
            }
        });
    }

    template <class Pixel, class Channel>
    static void write_rows(const core::image& src, PF_EffectWorld& world, float max_value, bool integer)
    {
        const auto to_channel = [&](float v) {
            if (!integer)
            {
                return static_cast<Channel>(v);
            }
            return static_cast<Channel>(std::lround(std::clamp(v, 0.0f, 1.0f) * max_value));
        };
        core::parallel::for_range(0, world.height, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                Pixel* row = world_row<Pixel>(world, y);
                const float* s = src.at(src.bounds.x0, src.bounds.y0 + y);
                for (int x = 0; x < world.width; ++x, s += 4)
                {
                    const float a = integer ? std::clamp(s[3], 0.0f, 1.0f) : s[3];
                    const float inv = a > 0.0f ? 1.0f / a : 0.0f;
                    row[x].alpha = to_channel(a);
                    row[x].red = to_channel(s[0] * inv);
                    row[x].green = to_channel(s[1] * inv);
                    row[x].blue = to_channel(s[2] * inv);
                }
            }
        });
    }

    core::buffer read(const PF_EffectWorld& world, PF_PixelFormat format, core::rect bounds)
    {
        if (bounds.width() != world.width || bounds.height() != world.height)
        {
            throw std::logic_error("bounds don't match the world size");
        }
        core::buffer out(bounds, 4);
        switch (format)
        {
        case PF_PixelFormat_ARGB32:
            read_rows<PF_Pixel8>(world, out.view(), 1.0f / PF_MAX_CHAN8);
            break;
        case PF_PixelFormat_ARGB64:
            read_rows<PF_Pixel16>(world, out.view(), 1.0f / PF_MAX_CHAN16);
            break;
        case PF_PixelFormat_ARGB128:
            read_rows<PF_PixelFloat>(world, out.view(), 1.0f);
            break;
        default:
            throw std::runtime_error("unsupported pixel format");
        }
        return out;
    }

    void write(const core::image& src, PF_EffectWorld& world, PF_PixelFormat format)
    {
        if (src.bounds.width() != world.width || src.bounds.height() != world.height)
        {
            throw std::logic_error("image doesn't match the world size");
        }
        switch (format)
        {
        case PF_PixelFormat_ARGB32:
            write_rows<PF_Pixel8, A_u_char>(src, world, PF_MAX_CHAN8, true);
            break;
        case PF_PixelFormat_ARGB64:
            write_rows<PF_Pixel16, A_u_short>(src, world, PF_MAX_CHAN16, true);
            break;
        case PF_PixelFormat_ARGB128:
            write_rows<PF_PixelFloat, PF_FpShort>(src, world, 1.0f, false);
            break;
        default:
            throw std::runtime_error("unsupported pixel format");
        }
    }
} // namespace functional_fx_suite::hosts::ae::pixels
