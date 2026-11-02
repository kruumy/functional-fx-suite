#include "effects/timedisplace.hpp"
#include "core/mask.hpp"
#include "core/parallel.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace functional_fx_suite::effects::timedisplace
{
    enum class p : std::uint8_t
    {
        black_time_shift,
        white_time_shift,
        shift_relative_to,
        interp_frames,
        matte_use,
        blur_matte,
        count
    };

    using core::param;

    static constexpr double UNBOUNDED = 1e6;

    // the shifts are in frames
    static constexpr std::array<param, static_cast<std::size_t>(p::count)> PARAMS{
        param::scalar(1, "black_time_shift", "Black Time Shift")
            .value(-10)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(-250, 250),
        param::scalar(2, "white_time_shift", "White Time Shift")
            .value(10)
            .range(-UNBOUNDED, UNBOUNDED)
            .slider(-250, 250),
        param::choice(3, "shift_relative_to", "Shift Relative To", "Frame 0|Current Frame").value(1),
        param::toggle(4, "interp_frames", "Interp Frames"),
        param::choice(5, "matte_use", "Matte Use", "Luma|Alpha"),
        param::scalar(6, "blur_matte", "Blur Matte").value(0.224).range(0, UNBOUNDED).slider(0, 2),
    };

    // the frames from now a pixel can read: black (matte 0) to white (matte 1), counted from frame 0 or from now
    struct shift_range
    {
        double black;
        double white;
        double lo;
        double hi;
        bool interp;

        shift_range(const core::param_values& v, int frame)
            : black(std::clamp(v.scalar(p::black_time_shift), -UNBOUNDED, UNBOUNDED)),
              white(std::clamp(v.scalar(p::white_time_shift), -UNBOUNDED, UNBOUNDED)), lo(std::min(black, white)),
              hi(std::max(black, white)), interp(v.toggle(p::interp_frames))
        {
            if (v.choice(p::shift_relative_to) == 0)
            {
                black -= frame;
                white -= frame;
                lo -= frame;
                hi -= frame;
            }
        }

        // the whole frames that range touches
        [[nodiscard]] int first() const
        {
            return static_cast<int>(interp ? std::floor(lo) : std::round(lo));
        }
        [[nodiscard]] int last() const
        {
            return static_cast<int>(interp ? std::ceil(hi) : std::round(hi));
        }

        // how much of frame `n` a pixel `shift` frames from now gets: the nearest frame, or the two around it
        [[nodiscard]] double weight(double shift, int n) const
        {
            if (!interp)
            {
                return n == std::round(shift) ? 1.0 : 0.0;
            }
            const double k = std::floor(shift);
            const double t = shift - k;
            return n == k ? 1.0 - t : n == k + 1.0 ? t : 0.0;
        }
    };

    static std::vector<int> frame_offsets(const core::param_values& v, int frame)
    {
        const shift_range r(v, frame);
        std::vector<int> offsets;
        for (int n = r.first(); n <= r.last(); ++n)
        {
            if (n != 0)
            {
                offsets.push_back(n);
            }
        }
        return offsets;
    }

    static void render(const core::render_args& args)
    {
        const core::param_values& v = *args.params;
        const shift_range r(v, args.frame_number());

        // the matte (the source when none is connected), blurred; its 0..1 picks each pixel's frame
        core::render_args matte_args = args;
        matte_args.mask = args.background != nullptr ? args.background : &args.src;
        const core::buffer matte = core::mask::build(
            matte_args, v.choice(p::matte_use) == 1 ? core::mask::source::alpha : core::mask::source::luma,
            v.scalar(p::blur_matte), false);
        const core::image& m = matte.view();

        const core::image& dst = args.dst;
        const auto width = static_cast<std::size_t>(dst.bounds.width());
        const auto index = [&](int x, int y) {
            return static_cast<std::size_t>(y - dst.bounds.y0) * width + static_cast<std::size_t>(x - dst.bounds.x0);
        };
        std::vector<double> shifts(width * static_cast<std::size_t>(dst.bounds.height()));
        core::parallel::for_range(dst.bounds.y0, dst.bounds.y1, [&](int y_begin, int y_end) {
            for (int y = y_begin; y < y_end; ++y)
            {
                for (int x = dst.bounds.x0; x < dst.bounds.x1; ++x)
                {
                    const double k = m.bounds.contains(x, y) ? std::clamp(*m.at(x, y), 0.0f, 1.0f) : 0.0f;
                    // clamped so rounding can't step outside the frames the host was asked for
                    shifts[index(x, y)] = std::clamp(r.black + k * (r.white - r.black), r.lo, r.hi);
                    std::fill_n(dst.at(x, y), 4, 0.0f);
                }
            }
        });

        // only the frames some pixel reads are fetched, one at a time
        std::vector<char> used(static_cast<std::size_t>(r.last() - r.first() + 1));
        for (const double s : shifts)
        {
            const int k = static_cast<int>(r.interp ? std::floor(s) : std::round(s));
            used[static_cast<std::size_t>(k - r.first())] = 1;
            if (r.interp && s > k)
            {
                used[static_cast<std::size_t>(k + 1 - r.first())] = 1;
            }
        }
        for (int n = r.first(); n <= r.last(); ++n)
        {
            if (used[static_cast<std::size_t>(n - r.first())] == 0)
            {
                continue;
            }
            const core::buffer fetched = n == 0 ? core::buffer{} : args.frame_at(n);
            const core::image& img = n == 0 ? args.src : fetched.view();
            if (img.data == nullptr)
            {
                continue; // a frame the host has none of is transparent
            }
            core::parallel::for_range(dst.bounds.y0, dst.bounds.y1, [&](int y_begin, int y_end) {
                for (int y = y_begin; y < y_end; ++y)
                {
                    for (int x = dst.bounds.x0; x < dst.bounds.x1; ++x)
                    {
                        const auto w = static_cast<float>(r.weight(shifts[index(x, y)], n));
                        if (w > 0.0f && img.bounds.contains(x, y))
                        {
                            const float* s = img.at(x, y);
                            float* d = dst.at(x, y);
                            for (int c = 0; c < 4; ++c)
                            {
                                d[c] += w * s[c];
                            }
                        }
                    }
                }
            });
        }
    }

    const core::effect& get()
    {
        static constexpr core::effect EFFECT{
            .match_name = "f_timedisplace",
            .name = "f_timedisplace",
            .category = "Functional Time",
            .description = "Each pixel shows the clip at its own frame, picked by the brightness of a matte between "
                           "Black Time Shift and White Time Shift.",
            .params = PARAMS,
            .mask_input = "",
            .background_input = "Matte",
            .time_varying = false,
            .reads_outside = true,
            .render = render,
            .frame_offsets = frame_offsets,
        };
        return EFFECT;
    }
} // namespace functional_fx_suite::effects::timedisplace
