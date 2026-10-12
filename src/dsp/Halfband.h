#pragma once

#include <array>

namespace ironclip::dsp
{

/** The 2K non-zero, symmetric taps of a half-band filter, applied as one
 *  polyphase branch. A doubled history buffer keeps the newest 2K samples
 *  contiguous, so each output is a single folded dot product. */
template <typename T, typename Coeffs>
class HalfbandBranch
{
public:
  static constexpr int K = Coeffs::kHalfOdd;
  static constexpr int kLength = 2 * K;

  void Reset() noexcept
  {
    mHistory.fill(T(0));
    mPos = 0;
  }

  /** Pushes x and returns sum_i c_i * (x[n-(K-1-i)] + x[n-(K+i)]). */
  T Push(T x) noexcept
  {
    mPos = (mPos == 0 ? kLength : mPos) - 1;
    mHistory[mPos] = x;
    mHistory[mPos + kLength] = x;

    // Evaluated as two forward dot products (the newer half against the
    // reversed taps) with independent partial sums, which vectorises well.
    const T* h = mHistory.data() + mPos; // h[0] is the newest sample
    constexpr int kLanes = K % 4 == 0 ? 4 : 1;
    T acc[kLanes] = {};
    for (int i = 0; i < K; i += kLanes)
      for (int k = 0; k < kLanes; ++k)
        acc[k] += kTapsReversed[i + k] * h[i + k] + kTaps[i + k] * h[K + i + k];
    T sum = T(0);
    for (int k = 0; k < kLanes; ++k)
      sum += acc[k];
    return sum;
  }

  /** Sample pushed `age` calls ago (0 = most recent), age < 2K. */
  T Delayed(int age) const noexcept { return mHistory[mPos + age]; }

private:
  static constexpr std::array<T, K> MakeTaps()
  {
    std::array<T, K> taps {};
    for (int i = 0; i < K; ++i)
      taps[i] = static_cast<T>(Coeffs::kTaps[i]);
    return taps;
  }

  static constexpr std::array<T, K> MakeReversedTaps()
  {
    std::array<T, K> taps {};
    for (int i = 0; i < K; ++i)
      taps[i] = static_cast<T>(Coeffs::kTaps[K - 1 - i]);
    return taps;
  }

  static constexpr std::array<T, K> kTaps = MakeTaps();
  static constexpr std::array<T, K> kTapsReversed = MakeReversedTaps();

  alignas(32) std::array<T, 2 * kLength> mHistory {};
  int mPos = 0;
};

/** 2x interpolator. Group delay: 2K-1 samples at the output rate. */
template <typename T, typename Coeffs>
class HalfbandUpsampler
{
public:
  static constexpr int K = Coeffs::kHalfOdd;
  static constexpr int kDelayHigh = 2 * K - 1;

  void Reset() noexcept { mBranch.Reset(); }

  /** Reads n samples and writes 2n samples. */
  void Process(const T* in, T* out, int n) noexcept
  {
    for (int i = 0; i < n; ++i)
    {
      out[2 * i] = T(2) * mBranch.Push(in[i]);
      out[2 * i + 1] = mBranch.Delayed(K - 1); // centre tap (0.5) x zero-stuffing gain (2)
    }
  }

private:
  HalfbandBranch<T, Coeffs> mBranch;
};

/** 2x decimator. Group delay: 2K-1 samples at the input rate. */
template <typename T, typename Coeffs>
class HalfbandDownsampler
{
public:
  static constexpr int K = Coeffs::kHalfOdd;
  static constexpr int kDelayHigh = 2 * K - 1;

  void Reset() noexcept
  {
    mBranch.Reset();
    mOdd.fill(T(0));
    mOddPos = 0;
  }

  /** Reads 2n samples and writes n samples. */
  void Process(const T* in, T* out, int n) noexcept
  {
    for (int i = 0; i < n; ++i)
    {
      const T oddDelayed = mOdd[mOddPos]; // x[2(n-K)+1]
      mOdd[mOddPos] = in[2 * i + 1];
      mOddPos = mOddPos + 1 == K ? 0 : mOddPos + 1;
      out[i] = mBranch.Push(in[2 * i]) + T(0.5) * oddDelayed;
    }
  }

private:
  HalfbandBranch<T, Coeffs> mBranch;
  std::array<T, K> mOdd {};
  int mOddPos = 0;
};

} // namespace ironclip::dsp
