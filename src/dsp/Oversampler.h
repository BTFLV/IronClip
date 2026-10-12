#pragma once

#include <algorithm>
#include <array>

#include "Halfband.h"
#include "HalfbandCoefficients.h"

namespace ironclip::dsp
{

enum class Oversampling : int
{
  Off = 0,
  X2 = 1,
  X4 = 2,
  X8 = 3
};

constexpr int kNumOversamplingModes = 4;

constexpr int OversamplingFactor(Oversampling mode) noexcept { return 1 << static_cast<int>(mode); }

/** Cascaded linear-phase half-band oversampler (up to 8x) for one channel.
 *
 *  The round trip (up, then down) is a pure delay plus a flat passband up to
 *  0.45 x the base sample rate. A short pad delay at the top rate rounds the
 *  total latency up to a whole number of base-rate samples, so dry signals can
 *  be aligned exactly with a plain integer delay. */
template <typename T>
class Oversampler
{
public:
  static constexpr int kMaxFactor = 8;
  static constexpr int kMaxBlock = 64; // base-rate samples per call

  /** Round-trip latency in samples at the oversampled rate, before padding. */
  static constexpr int RawLatencyHigh(Oversampling mode) noexcept
  {
    const int factor = OversamplingFactor(mode);
    int total = 0;
    // A 2x stage at stage rate R adds (2K-1) samples (at rate R) on the way up
    // and the same on the way down; one sample at rate R is factor/R top-rate samples.
    if (factor >= 2) total += 2 * (2 * halfband::Stage1::kHalfOdd - 1) * (factor / 2);
    if (factor >= 4) total += 2 * (2 * halfband::Stage2::kHalfOdd - 1) * (factor / 4);
    if (factor >= 8) total += 2 * (2 * halfband::Stage3::kHalfOdd - 1) * (factor / 8);
    return total;
  }

  static constexpr int PadHigh(Oversampling mode) noexcept
  {
    const int factor = OversamplingFactor(mode);
    return (factor - RawLatencyHigh(mode) % factor) % factor;
  }

  /** Round-trip latency in base-rate samples (always an integer). */
  static constexpr int Latency(Oversampling mode) noexcept
  {
    return (RawLatencyHigh(mode) + PadHigh(mode)) / OversamplingFactor(mode);
  }

  static constexpr int kMaxLatency = std::max({ Latency(Oversampling::Off), Latency(Oversampling::X2),
                                                Latency(Oversampling::X4), Latency(Oversampling::X8) });

  void SetMode(Oversampling mode) noexcept
  {
    if (mode != mMode)
    {
      mMode = mode;
      Reset();
    }
  }

  Oversampling Mode() const noexcept { return mMode; }
  int Factor() const noexcept { return OversamplingFactor(mMode); }

  void Reset() noexcept
  {
    mUp1.Reset();
    mUp2.Reset();
    mUp3.Reset();
    mDown1.Reset();
    mDown2.Reset();
    mDown3.Reset();
    mPad.fill(T(0));
    mPadPos = 0;
  }

  /** Upsamples n (<= kMaxBlock) base-rate samples into n * Factor() samples. */
  void Up(const T* in, T* out, int n) noexcept
  {
    switch (mMode)
    {
      case Oversampling::Off:
        std::copy(in, in + n, out);
        break;
      case Oversampling::X2:
        mUp1.Process(in, out, n);
        break;
      case Oversampling::X4:
        mUp1.Process(in, mWork1.data(), n);
        mUp2.Process(mWork1.data(), out, 2 * n);
        break;
      case Oversampling::X8:
        mUp1.Process(in, mWork1.data(), n);
        mUp2.Process(mWork1.data(), mWork2.data(), 2 * n);
        mUp3.Process(mWork2.data(), out, 4 * n);
        break;
    }
  }

  /** Downsamples n * Factor() samples into n base-rate samples. `in` is used
   *  as scratch space and may be modified. */
  void Down(T* in, T* out, int n) noexcept
  {
    const int pad = PadHigh(mMode);
    if (pad > 0)
    {
      const int total = n * Factor();
      for (int i = 0; i < total; ++i)
      {
        const T delayed = mPad[mPadPos];
        mPad[mPadPos] = in[i];
        mPadPos = mPadPos + 1 >= pad ? 0 : mPadPos + 1;
        in[i] = delayed;
      }
    }

    switch (mMode)
    {
      case Oversampling::Off:
        std::copy(in, in + n, out);
        break;
      case Oversampling::X2:
        mDown1.Process(in, out, n);
        break;
      case Oversampling::X4:
        mDown2.Process(in, mWork1.data(), 2 * n);
        mDown1.Process(mWork1.data(), out, n);
        break;
      case Oversampling::X8:
        mDown3.Process(in, mWork2.data(), 4 * n);
        mDown2.Process(mWork2.data(), mWork1.data(), 2 * n);
        mDown1.Process(mWork1.data(), out, n);
        break;
    }
  }

private:
  Oversampling mMode = Oversampling::Off;

  HalfbandUpsampler<T, halfband::Stage1> mUp1;
  HalfbandUpsampler<T, halfband::Stage2> mUp2;
  HalfbandUpsampler<T, halfband::Stage3> mUp3;
  HalfbandDownsampler<T, halfband::Stage1> mDown1;
  HalfbandDownsampler<T, halfband::Stage2> mDown2;
  HalfbandDownsampler<T, halfband::Stage3> mDown3;

  std::array<T, 2 * kMaxBlock> mWork1 {};
  std::array<T, 4 * kMaxBlock> mWork2 {};
  std::array<T, kMaxFactor> mPad {};
  int mPadPos = 0;
};

static_assert(Oversampler<float>::Latency(Oversampling::Off) == 0);
static_assert(Oversampler<float>::Latency(Oversampling::X2) == 63);
static_assert(Oversampler<float>::Latency(Oversampling::X4) == 69);
static_assert(Oversampler<float>::Latency(Oversampling::X8) == 71);

} // namespace ironclip::dsp
