#pragma once

#include <algorithm>
#include <concepts>
#include <future>
#include <thread>
#include <vector>

namespace functional_fx_suite::core::parallel
{
    // runs fn(begin, end) over [first, last) split into chunks across hardware threads
    template <std::invocable<int, int> Fn> void for_range(int first, int last, Fn&& fn)
    {
        const int count = last - first;
        if (count <= 0)
        {
            return;
        }
        const int threads = std::min<int>(count, std::max(1U, std::thread::hardware_concurrency()));
        if (threads == 1)
        {
            fn(first, last);
            return;
        }
        // more chunks than threads keeps uneven rows (rays near the centre) balanced
        const int chunks = std::min(count, threads * 4);
        const auto chunk_begin = [&](int c) {
            return first + static_cast<int>(static_cast<long long>(count) * c / chunks);
        };
        const auto run_strided = [&](int t) {
            for (int c = t; c < chunks; c += threads)
            {
                fn(chunk_begin(c), chunk_begin(c + 1));
            }
        };
        std::vector<std::future<void>> pending;
        pending.reserve(threads - 1);
        for (int t = 1; t < threads; ++t)
        {
            pending.push_back(std::async(std::launch::async, run_strided, t));
        }
        run_strided(0);
        // get() rethrows worker exceptions; wait first so no worker outlives this frame
        std::ranges::for_each(pending, [](std::future<void>& f) { f.wait(); });
        std::ranges::for_each(pending, [](std::future<void>& f) { f.get(); });
    }
} // namespace functional_fx_suite::core::parallel
