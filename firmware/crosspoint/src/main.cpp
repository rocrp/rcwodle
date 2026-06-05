/* crosspoint-on-wodle skeleton: C++20 sanity probe.
 * Verifies -std=gnu++20, static constructors, <memory>, <vector>, concepts
 * under RT-Thread before any CrossPoint code lands. */

#include <concepts>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "rtthread.h"

template <typename T>
concept Numeric = std::integral<T> || std::floating_point<T>; /* C++20 gate */

template <Numeric T>
static T square(T v) { return v * v; }

struct StaticCtorProbe
{
    int value;
    StaticCtorProbe() : value(42) {}
};
static StaticCtorProbe probe;

extern "C" int main(void)
{
    std::vector<int> v;
    v.reserve(8);
    for (int i = 0; i < 8; i++) v.push_back(square(i));

    auto buf = std::make_unique<uint8_t[]>(1024);
    buf[0] = static_cast<uint8_t>(v.back());

    constexpr std::string_view banner = "crosspoint-wodle skeleton";
    rt_kprintf("[%.*s] static-ctor=%d vec=%d buf=%d\n",
               (int)banner.size(), banner.data(), probe.value, v.back(), buf[0]);

    while (1)
        rt_thread_mdelay(1000);
    return 0;
}
