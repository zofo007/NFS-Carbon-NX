#include <atomic>
#include <cstdint>
#include <arm_neon.h>

#if !defined(__aarch64__) || !defined(__SWITCH__)
#error Expected the ARM64 Switch compiler.
#endif
static_assert(sizeof(void*) == 8);
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__);
thread_local std::uint32_t counter;
std::atomic<std::uint32_t> progress;
uint32x4_t smoke() {
  progress.store(++counter, std::memory_order_release);
  return vdupq_n_u32(counter);
}
