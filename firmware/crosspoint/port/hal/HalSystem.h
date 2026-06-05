/* WODLE-PORT: HalSystem panic plumbing stub. RT-Thread asserts/hard-faults
 * print to the (currently unreliable) UART; SD panic dumps are a future
 * upgrade once the port is HIL-stable. */
#pragma once

#include <cstdint>
#include <string>

namespace HalSystem
{
struct StackFrame
{
    uint32_t sp;
    uint32_t spp[8];
};

inline void begin() {}
inline void checkPanic() {}
inline void clearPanic() {}
inline std::string getPanicInfo(bool = false) { return {}; }
inline bool isRebootFromPanic() { return false; }
} // namespace HalSystem
