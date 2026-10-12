#pragma once

#include <cstdint>

#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
  #include <xmmintrin.h>
  #define IRONCLIP_DENORMALS_SSE 1
#elif defined(__aarch64__)
  #define IRONCLIP_DENORMALS_AARCH64 1
#endif

namespace ironclip::dsp
{

/** Enables flush-to-zero (and denormals-are-zero on x86) for the lifetime of
 *  the object and restores the previous floating-point mode afterwards. */
class ScopedNoDenormals
{
public:
  ScopedNoDenormals() noexcept
  {
#if defined(IRONCLIP_DENORMALS_SSE)
    mPrevious = _mm_getcsr();
    _mm_setcsr(mPrevious | 0x8040u); // FTZ | DAZ
#elif defined(IRONCLIP_DENORMALS_AARCH64)
    std::uint64_t fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    mPrevious = fpcr;
    __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr | (1ull << 24))); // FZ
#endif
  }

  ~ScopedNoDenormals()
  {
#if defined(IRONCLIP_DENORMALS_SSE)
    _mm_setcsr(mPrevious);
#elif defined(IRONCLIP_DENORMALS_AARCH64)
    __asm__ __volatile__("msr fpcr, %0" : : "r"(mPrevious));
#endif
  }

  ScopedNoDenormals(const ScopedNoDenormals&) = delete;
  ScopedNoDenormals& operator=(const ScopedNoDenormals&) = delete;

private:
#if defined(IRONCLIP_DENORMALS_SSE)
  unsigned int mPrevious = 0;
#elif defined(IRONCLIP_DENORMALS_AARCH64)
  std::uint64_t mPrevious = 0;
#endif
};

} // namespace ironclip::dsp
