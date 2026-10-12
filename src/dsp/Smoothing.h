#pragma once

#include <algorithm>
#include <cmath>

namespace ironclip::dsp
{

inline double DbToGain(double db) noexcept { return std::pow(10.0, db / 20.0); }

inline double GainToDb(double gain, double floorDb = -200.0) noexcept
{
  return gain > 0.0 ? std::max(20.0 * std::log10(gain), floorDb) : floorDb;
}

/** Linear ramp towards a target over a fixed number of samples.
 *  The ramp always lands exactly on its target, so a settled smoother never
 *  drifts, and a constant parameter costs nothing beyond a fill. */
class LinearRamp
{
public:
  void SetRampLength(int samples) noexcept { mRampLength = std::max(1, samples); }

  void Reset(double value) noexcept
  {
    mCurrent = mTarget = value;
    mStep = 0.0;
    mRemaining = 0;
  }

  void SetTarget(double target) noexcept
  {
    if (target == mTarget)
      return;

    mTarget = target;
    mRemaining = mRampLength;
    mStep = (mTarget - mCurrent) / static_cast<double>(mRemaining);
  }

  bool IsRamping() const noexcept { return mRemaining > 0; }
  double Current() const noexcept { return mCurrent; }
  double Target() const noexcept { return mTarget; }

  /** Writes the next n values and advances the ramp. */
  template <typename T>
  void Fill(T* dst, int n) noexcept
  {
    int i = 0;
    for (; i < n && mRemaining > 0; ++i)
    {
      --mRemaining;
      mCurrent = mRemaining == 0 ? mTarget : mCurrent + mStep;
      dst[i] = static_cast<T>(mCurrent);
    }
    const T value = static_cast<T>(mCurrent);
    for (; i < n; ++i)
      dst[i] = value;
  }

  /** Advances the ramp by n samples without writing values. */
  void Skip(int n) noexcept
  {
    if (mRemaining <= 0)
      return;
    if (n >= mRemaining)
    {
      mRemaining = 0;
      mCurrent = mTarget;
    }
    else
    {
      mRemaining -= n;
      mCurrent += mStep * n;
    }
  }

private:
  double mCurrent = 0.0;
  double mTarget = 0.0;
  double mStep = 0.0;
  int mRemaining = 0;
  int mRampLength = 1;
};

} // namespace ironclip::dsp
