#include "hosts/ofx/pixels.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace functional_fx_suite::hosts::ofx::pixels
{
    template <class Channel> static Channel* host_row(const host_image& img, int ofx_y)
    {
        return reinterpret_cast<Channel*>(static_cast<char*>(img.data) +
                                          static_cast<std::ptrdiff_t>(ofx_y - img.y1) * img.row_bytes);
    }

    template <class Channel> static void read_rows(const host_image& img, const core::image& dst, float scale)
    {
        const int width = img.x2 - img.x1;
        core::parallel::for_range(img.y1, img.y2, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                const Channel* s = host_row<const Channel>(img, y);
                float* d = dst.at(img.x1, -1 - y);
                for (int x = 0; x < width; ++x, s += img.components, d += 4)
                {
                    if (img.components == 1)
                    {
                        const float a = static_cast<float>(s[0]) * scale;
                        std::fill_n(d, 4, a);
                        continue;
                    }
                    const float a = static_cast<float>(s[3]) * scale;
                    const float k = img.premultiplied ? scale : scale * a;
                    d[0] = static_cast<float>(s[0]) * k;
                    d[1] = static_cast<float>(s[1]) * k;
                    d[2] = static_cast<float>(s[2]) * k;
                    d[3] = a;
                }
            }
        });
    }

    template <class Channel>
    static void write_rows(const core::image& src, const host_image& img, core::rect window, float max_value,
                           bool integer)
    {
        const auto to_channel = [&](float v) {
            if (!integer)
            {
                return static_cast<Channel>(v);
            }
            return static_cast<Channel>(std::lround(std::clamp(v, 0.0f, 1.0f) * max_value));
        };
        core::parallel::for_range(window.y0, window.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                Channel* d =
                    host_row<Channel>(img, -1 - y) + static_cast<std::ptrdiff_t>(window.x0 - img.x1) * img.components;
                const float* s = src.at(window.x0, y);
                for (int x = window.x0; x < window.x1; ++x, s += 4, d += img.components)
                {
                    if (img.components == 1)
                    {
                        d[0] = to_channel(s[3]);
                        continue;
                    }
                    const float inv = img.premultiplied ? 1.0f : (s[3] > 0.0f ? 1.0f / s[3] : 0.0f);
                    d[0] = to_channel(s[0] * inv);
                    d[1] = to_channel(s[1] * inv);
                    d[2] = to_channel(s[2] * inv);
                    d[3] = to_channel(s[3]);
                }
            }
        });
    }

    static void check_components(const host_image& img)
    {
        if (img.data == nullptr || (img.components != 1 && img.components != 4))
        {
            throw std::runtime_error("unsupported image layout");
        }
    }

    core::buffer read(const host_image& img)
    {
        check_components(img);
        core::buffer out(to_core(img.x1, img.y1, img.x2, img.y2), 4);
        switch (img.bit_depth)
        {
        case depth::byte:
            read_rows<std::uint8_t>(img, out.view(), 1.0f / 255.0f);
            break;
        case depth::word:
            read_rows<std::uint16_t>(img, out.view(), 1.0f / 65535.0f);
            break;
        case depth::real:
            read_rows<float>(img, out.view(), 1.0f);
            break;
        }
        return out;
    }

    void write(const core::image& src, const host_image& dst, core::rect window)
    {
        check_components(dst);
        const core::rect host = to_core(dst.x1, dst.y1, dst.x2, dst.y2);
        window = core::rect{std::max(window.x0, std::max(host.x0, src.bounds.x0)),
                            std::max(window.y0, std::max(host.y0, src.bounds.y0)),
                            std::min(window.x1, std::min(host.x1, src.bounds.x1)),
                            std::min(window.y1, std::min(host.y1, src.bounds.y1))};
        if (window.empty())
        {
            return;
        }
        switch (dst.bit_depth)
        {
        case depth::byte:
            write_rows<std::uint8_t>(src, dst, window, 255.0f, true);
            break;
        case depth::word:
            write_rows<std::uint16_t>(src, dst, window, 65535.0f, true);
            break;
        case depth::real:
            write_rows<float>(src, dst, window, 1.0f, false);
            break;
        }
    }
} // namespace functional_fx_suite::hosts::ofx::pixels
