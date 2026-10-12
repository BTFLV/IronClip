#include "ClipperEngine.h"

#include <algorithm>
#include <cmath>

#include "Denormals.h"

namespace ironclip::dsp
{

namespace
{

double SanitizeDb(double value, double minDb, double maxDb, double fallback) noexcept
{
  return std::isnan(value) ? fallback : std::clamp(value, minDb, maxDb);
}

int SecondsToSamples(double seconds, double sampleRate) noexcept
{
  return std::max(1, static_cast<int>(std::lround(seconds * sampleRate)));
}

double OnePoleCoefficient(int samples, double timeConstantSeconds, double sampleRate) noexcept
{
  return 1.0 - std::exp(-static_cast<double>(samples) / (timeConstantSeconds * sampleRate));
}

// Inputs beyond +120 dBFS (including infinities) are treated as that level, so
// they clip cleanly instead of turning filter state into NaN.
template <typename T>
constexpr T kInputLimit = T(1e6);

template <typename T>
inline T Abs(T x) noexcept
{
  return x < T(0) ? -x : x;
}

template <typename T>
inline T Clip(T x, T ceiling) noexcept
{
  return std::min(std::max(x, -ceiling), ceiling);
}

// Reductions use independent partial accumulators so they vectorise and do
// not serialise on floating-point latency.
constexpr int kLanes = 8;

template <typename T>
T PeakAbs(const T* x, int n) noexcept
{
  T acc[kLanes] = {};
  int i = 0;
  for (; i + kLanes <= n; i += kLanes)
    for (int k = 0; k < kLanes; ++k)
      acc[k] = std::max(acc[k], Abs(x[i + k]));
  T peak = T(0);
  for (int k = 0; k < kLanes; ++k)
    peak = std::max(peak, acc[k]);
  for (; i < n; ++i)
    peak = std::max(peak, Abs(x[i]));
  return peak;
}

template <typename T>
T SumSquares(const T* x, int n) noexcept
{
  T acc[kLanes] = {};
  int i = 0;
  for (; i + kLanes <= n; i += kLanes)
    for (int k = 0; k < kLanes; ++k)
      acc[k] += x[i + k] * x[i + k];
  T sum = T(0);
  for (int k = 0; k < kLanes; ++k)
    sum += acc[k];
  for (; i < n; ++i)
    sum += x[i] * x[i];
  return sum;
}

template <typename T>
T SumSquaresProduct(const T* x, const T* g, int n) noexcept
{
  T acc[kLanes] = {};
  int i = 0;
  for (; i + kLanes <= n; i += kLanes)
    for (int k = 0; k < kLanes; ++k)
    {
      const T v = x[i + k] * g[i + k];
      acc[k] += v * v;
    }
  T sum = T(0);
  for (int k = 0; k < kLanes; ++k)
    sum += acc[k];
  for (; i < n; ++i)
  {
    const T v = x[i] * g[i];
    sum += v * v;
  }
  return sum;
}

template <typename Ring, typename T>
void RingWrite(Ring& r, const T* src, int n) noexcept
{
  constexpr int size = Ring::kSize;
  const int first = std::min(n, size - r.write);
  std::copy(src, src + first, r.buffer.data() + r.write);
  std::copy(src + first, src + n, r.buffer.data());
  r.write = (r.write + n) & (size - 1);
}

template <typename Ring, typename T>
void RingRead(const Ring& r, int start, T* dst, int n) noexcept
{
  constexpr int size = Ring::kSize;
  start &= size - 1;
  const int first = std::min(n, size - start);
  std::copy(r.buffer.data() + start, r.buffer.data() + start + first, dst);
  std::copy(r.buffer.data(), r.buffer.data() + (n - first), dst + first);
}

} // namespace

template <typename T>
ClipperEngine<T>::ClipperEngine()
{
  Prepare(mSampleRate);
}

template <typename T>
void ClipperEngine<T>::Prepare(double sampleRate) noexcept
{
  mSampleRate = (std::isfinite(sampleRate) && sampleRate >= 1000.0) ? sampleRate : 48000.0;

  const int gainRamp = SecondsToSamples(kGainRampSeconds, mSampleRate);
  const int mixRamp = SecondsToSamples(kMixRampSeconds, mSampleRate);
  mDrive.SetRampLength(gainRamp);
  mCeiling.SetRampLength(gainRamp);
  mOutput.SetRampLength(gainRamp);
  mDeltaMix.SetRampLength(mixRamp);
  mBypassMix.SetRampLength(mixRamp);
  mModeGain.SetRampLength(SecondsToSamples(kModeFadeSeconds, mSampleRate));

  mSliceLength = SecondsToSamples(kHistorySliceSeconds, mSampleRate);
  mDetectorCoeffBlock = OnePoleCoefficient(kBlock, kAutoGainDetectorSeconds, mSampleRate);
  mGlideCoeffBlock = OnePoleCoefficient(kBlock, kAutoGainSmoothingSeconds, mSampleRate);

  mClipLossDb = 0.0;
  Reset();
}

template <typename T>
void ClipperEngine<T>::ClearAudioState() noexcept
{
  for (auto& os : mOversamplers)
    os.Reset();
  for (auto& d : mDry)
  {
    d.buffer.fill(T(0));
    d.write = 0;
  }
  mEnergyDriven = 0.0;
  mEnergyOut = 0.0;
  mAutoGainHold = 0;
  mSliceCount = 0;
  mSlicePeak = 0.f;
}

template <typename T>
void ClipperEngine<T>::Reset() noexcept
{
  mActiveMode = mRequestedMode;
  for (auto& os : mOversamplers)
    os.SetMode(mActiveMode);

  ClearAudioState();

  mDrive.Reset(DbToGain(mParams.driveDb));
  mCeiling.Reset(DbToGain(mParams.ceilingDb));
  mOutput.Reset(DbToGain(mParams.outputDb));
  mDeltaMix.Reset(mParams.delta ? 1.0 : 0.0);
  mBypassMix.Reset(mParams.bypass ? 1.0 : 0.0);
  mModeGain.Reset(1.0);
  mAutoGainDb = mParams.autoGain ? std::clamp(mClipLossDb - mParams.driveDb, -48.0, 48.0) : 0.0;
  mAutoGainLinear = DbToGain(mAutoGainDb);
  mCachedDriveGain = -1.0;
  mTelemetry.SetAutoGainDb(static_cast<float>(mAutoGainDb));
}

template <typename T>
void ClipperEngine<T>::SetParameters(const ClipperParams& params) noexcept
{
  ClipperParams p = params;
  p.driveDb = SanitizeDb(p.driveDb, ranges::kDriveMinDb, ranges::kDriveMaxDb, 0.0);
  p.ceilingDb = SanitizeDb(p.ceilingDb, ranges::kCeilingMinDb, ranges::kCeilingMaxDb, 0.0);
  p.outputDb = SanitizeDb(p.outputDb, ranges::kOutputMinDb, ranges::kOutputMaxDb, 0.0);
  const int mode = static_cast<int>(p.oversampling);
  if (mode < 0 || mode >= kNumOversamplingModes)
    p.oversampling = Oversampling::Off;

  if (p.driveDb != mParams.driveDb)
    mDrive.SetTarget(DbToGain(p.driveDb));
  if (p.ceilingDb != mParams.ceilingDb)
    mCeiling.SetTarget(DbToGain(p.ceilingDb));
  if (p.outputDb != mParams.outputDb)
    mOutput.SetTarget(DbToGain(p.outputDb));
  mDeltaMix.SetTarget(p.delta ? 1.0 : 0.0);
  mBypassMix.SetTarget(p.bypass ? 1.0 : 0.0);
  mRequestedMode = p.oversampling;

  mParams = p;
}

template <typename T>
void ClipperEngine<T>::UpdateModeSwitch() noexcept
{
  // Oversampling changes fade the output out, swap the filter cascade while
  // silent, and fade back in. Requests arriving mid-fade simply reverse it.
  if (mRequestedMode != mActiveMode)
  {
    if (mModeGain.Target() != 0.0)
    {
      mModeGain.SetTarget(0.0);
    }
    else if (!mModeGain.IsRamping())
    {
      mActiveMode = mRequestedMode;
      for (int ch = 0; ch < kMaxChannels; ++ch)
        WarmUpOversampler(ch);
      mModeGain.SetTarget(1.0);
      // The new cascade starts from silence; keep it out of the level estimate.
      mAutoGainHold = Latency() + SecondsToSamples(kModeFadeSeconds, mSampleRate);
    }
  }
  else if (mModeGain.Target() != 1.0)
  {
    mModeGain.SetTarget(1.0);
  }
}

template <typename T>
void ClipperEngine<T>::WarmUpOversampler(int ch) noexcept
{
  // Run the freshly selected filter cascade over recent input so it starts in
  // steady state; a cold FIR would step in abruptly once its centre tap
  // reaches the signal.
  Oversampler<T>& os = mOversamplers[ch];
  os.SetMode(mActiveMode);
  os.Reset();
  if (mActiveMode == Oversampling::Off)
    return;

  const DryDelay& d = mDry[ch];
  constexpr int mask = DryDelay::kSize - 1;
  const T drive = static_cast<T>(mDrive.Current());
  const T ceiling = static_cast<T>(mCeiling.Current());
  const int total = kWarmUpSamples;
  const int factor = OversamplingFactor(mActiveMode);
  for (int done = 0; done < total; done += kBlock)
  {
    const int n = std::min(kBlock, total - done);
    for (int i = 0; i < n; ++i)
      mU[i] = d.buffer[(d.write - total + done + i) & mask] * drive;
    os.Up(mU.data(), mHigh.data(), n);
    for (int j = 0; j < n * factor; ++j)
      mHigh[j] = Clip(mHigh[j], ceiling);
    os.Down(mHigh.data(), mW.data(), n);
  }
}

template <typename T>
void ClipperEngine<T>::PrepareAutoGain(int n) noexcept
{
  // Everything here runs once per sub-block; transcendental calls are cached
  // so the common steady state costs a few comparisons.
  const double drive = mDrive.Current();
  if (drive != mCachedDriveGain)
  {
    mCachedDriveGain = drive;
    mCachedDriveDb = GainToDb(drive);
  }

  const double target = mParams.autoGain ? std::clamp(mClipLossDb - mCachedDriveDb, -48.0, 48.0) : 0.0;
  const double start = mAutoGainDb;
  if (start != target)
  {
    const double coeff = n == kBlock ? mGlideCoeffBlock : OnePoleCoefficient(n, kAutoGainSmoothingSeconds, mSampleRate);
    const double end = start + coeff * (target - start);
    mAutoGainDb = std::abs(end - target) < 1e-4 ? target : end;
  }

  // Combine the auto gain glide with the output gain ramp.
  mOutput.Fill(mGainBuf.data(), n);
  if (start != mAutoGainDb)
  {
    const double g0 = mAutoGainLinear;
    mAutoGainLinear = DbToGain(mAutoGainDb);
    const double step = (mAutoGainLinear - g0) / n;
    for (int i = 0; i < n; ++i)
      mGainBuf[i] *= static_cast<T>(g0 + step * (i + 1));
    mTelemetry.SetAutoGainDb(static_cast<float>(mAutoGainDb));
  }
  else if (mAutoGainDb != 0.0)
  {
    const T g = static_cast<T>(mAutoGainLinear);
    for (int i = 0; i < n; ++i)
      mGainBuf[i] *= g;
  }
}

template <typename T>
void ClipperEngine<T>::UpdateAutoGainEstimate(double sumDriven2, double sumOut2, int count, int n) noexcept
{
  if (count <= 0)
    return;

  if (!std::isfinite(sumDriven2) || !std::isfinite(sumOut2))
  {
    mEnergyDriven = mEnergyOut = 0.0;
    return;
  }

  const double a = n == kBlock ? mDetectorCoeffBlock : OnePoleCoefficient(n, kAutoGainDetectorSeconds, mSampleRate);
  mEnergyDriven += a * (sumDriven2 / count - mEnergyDriven);
  mEnergyOut += a * (sumOut2 / count - mEnergyOut);

  // Both detectors share a time constant, so their ratio stays put while the
  // signal fades out; below the gate the last estimate is simply held.
  // The energies are always tracked; the ratio is only evaluated when needed.
  constexpr double kGateEnergy = 1e-6; // kAutoGainGateDb as mean-square energy
  static_assert(kAutoGainGateDb == -60.0);
  if (mParams.autoGain && mEnergyDriven > kGateEnergy && mEnergyOut > 1e-30)
  {
    const double lossDb = 10.0 * std::log10(mEnergyDriven / mEnergyOut);
    mClipLossDb = std::clamp(lossDb, kAutoGainMinDb, kAutoGainMaxDb);
  }
}

template <typename T>
void ClipperEngine<T>::Process(const T* const* inputs, int numInputs, T* const* outputs, int numOutputs, int numFrames) noexcept
{
  if (numFrames <= 0 || numOutputs <= 0 || outputs == nullptr)
    return;

  ScopedNoDenormals noDenormals;

  const int numChannels = std::min(numOutputs, kMaxChannels);
  if (inputs == nullptr)
    numInputs = 0;

  for (int offset = 0; offset < numFrames; offset += kBlock)
  {
    const int n = std::min(kBlock, numFrames - offset);
    ProcessSubBlock(inputs, numInputs, outputs, numChannels, offset, n);
  }

  for (int ch = numChannels; ch < numOutputs; ++ch)
    if (outputs[ch])
      std::fill(outputs[ch], outputs[ch] + numFrames, T(0));
}

template <typename T>
void ClipperEngine<T>::ProcessSubBlock(const T* const* inputs, int numInputs, T* const* outputs, int numChannels, int offset, int n) noexcept
{
  UpdateModeSwitch();

  const bool deltaActive = mDeltaMix.IsRamping() || mDeltaMix.Current() != 0.0;
  const bool bypassActive = mBypassMix.IsRamping() || mBypassMix.Current() != 0.0;
  const bool modeFadeActive = mModeGain.IsRamping() || mModeGain.Current() != 1.0;
  const bool measure = !deltaActive && mAutoGainHold <= 0;

  mDrive.Fill(mDriveBuf.data(), n);
  mCeiling.Fill(mCeilingBuf.data(), n);
  PrepareAutoGain(n);
  if (deltaActive)
    mDeltaMix.Fill(mDeltaBuf.data(), n);
  else
    mDeltaMix.Skip(n);
  if (bypassActive)
    mBypassMix.Fill(mBypassBuf.data(), n);
  else
    mBypassMix.Skip(n);
  if (modeFadeActive)
    mModeGain.Fill(mModeBuf.data(), n);
  else
    mModeGain.Skip(n);

  const T* drive = mDriveBuf.data();
  const T* ceiling = mCeilingBuf.data();
  const T* gain = mGainBuf.data();
  const T* deltaMix = mDeltaBuf.data();
  const T* bypassMix = mBypassBuf.data();
  const T* modeGain = mModeBuf.data();
  T* u = mU.data();
  T* w = mW.data();
  T* dry = mDryAligned.data();
  T* high = mHigh.data();

  const int factor = OversamplingFactor(mActiveMode);
  const int shift = static_cast<int>(mActiveMode);
  const int latency = Latency();

  T inputPeak = T(0), drivenPeak = T(0), outputPeak = T(0);
  double sumDriven2 = 0.0, sumOut2 = 0.0;

  // A mono input feeding both outputs is copied first: with in-place hosts the
  // first output channel may share its buffer with the input.
  const T* sources[kMaxChannels];
  for (int ch = 0; ch < numChannels; ++ch)
  {
    const int src = std::min(ch, numInputs - 1);
    sources[ch] = (src >= 0 && inputs[src]) ? inputs[src] + offset : mSilence.data();
  }
  if (numChannels > 1 && sources[1] == sources[0] && sources[0] != mSilence.data())
  {
    std::copy(sources[0], sources[0] + n, mMonoInput.data());
    sources[0] = sources[1] = mMonoInput.data();
  }

  const bool ceilingRamping = ceiling[0] != ceiling[n - 1];

  for (int ch = 0; ch < numChannels; ++ch)
  {
    // Input stage: sanitise, apply drive, feed the latency-matched dry path.
    T* xs = mX.data();
    const T* x = sources[ch];
    for (int i = 0; i < n; ++i)
      xs[i] = x[i] == x[i] ? std::clamp(x[i], -kInputLimit<T>, kInputLimit<T>) : T(0);
    inputPeak = std::max(inputPeak, PeakAbs(xs, n));
    for (int i = 0; i < n; ++i)
      u[i] = xs[i] * drive[i];

    DryDelay& d = mDry[ch];
    RingWrite(d, xs, n);
    const T* dryAligned = xs;
    if (latency > 0)
    {
      RingRead(d, d.write - n - latency, dry, n);
      dryAligned = dry;
    }

    // Clipping stage.
    if (factor == 1)
    {
      drivenPeak = std::max(drivenPeak, PeakAbs(u, n));
      for (int i = 0; i < n; ++i)
        w[i] = Clip(u[i], ceiling[i]);
      if (deltaActive)
        for (int i = 0; i < n; ++i)
          w[i] = (T(1) - deltaMix[i]) * w[i] + deltaMix[i] * (u[i] - w[i]);
    }
    else
    {
      Oversampler<T>& os = mOversamplers[ch];
      const int total = n * factor;
      os.Up(u, high, n);
      drivenPeak = std::max(drivenPeak, PeakAbs(high, total));
      if (deltaActive)
      {
        for (int j = 0; j < total; ++j)
        {
          const T y = Clip(high[j], ceiling[j >> shift]);
          const T m = deltaMix[j >> shift];
          high[j] = (T(1) - m) * y + m * (high[j] - y);
        }
      }
      else if (ceilingRamping)
      {
        for (int j = 0; j < total; ++j)
          high[j] = Clip(high[j], ceiling[j >> shift]);
      }
      else
      {
        const T c = ceiling[0];
        for (int j = 0; j < total; ++j)
          high[j] = Clip(high[j], c);
      }
      os.Down(high, w, n);
    }

    if (measure)
    {
      sumDriven2 += static_cast<double>(SumSquaresProduct(dryAligned, drive, n));
      sumOut2 += static_cast<double>(SumSquares(w, n));
    }

    // Output stage: gain, bypass crossfade, mode-switch fade.
    T* out = outputs[ch] + offset;
    for (int i = 0; i < n; ++i)
      w[i] *= gain[i];
    if (bypassActive)
      for (int i = 0; i < n; ++i)
        w[i] = (T(1) - bypassMix[i]) * w[i] + bypassMix[i] * dryAligned[i]; // exact at both ends
    if (modeFadeActive)
      for (int i = 0; i < n; ++i)
        w[i] *= modeGain[i];
    outputPeak = std::max(outputPeak, PeakAbs(w, n));
    std::copy(w, w + n, out);
  }

  if (measure)
    UpdateAutoGainEstimate(sumDriven2, sumOut2, n * numChannels, n);
  else if (mAutoGainHold > 0)
    mAutoGainHold -= n;

  // Telemetry for the editor.
  const float ceilingNow = static_cast<float>(ceiling[n - 1]);
  const float overshoot = static_cast<float>(drivenPeak) / ceilingNow;
  mTelemetry.AccumulatePeaks(static_cast<float>(inputPeak), static_cast<float>(outputPeak), overshoot);

  mSlicePeak = std::max(mSlicePeak, static_cast<float>(drivenPeak));
  mSliceCount += n;
  if (mSliceCount >= mSliceLength)
  {
    mTelemetry.PushHistory({ mSlicePeak, ceilingNow });
    mSliceCount -= mSliceLength;
    mSlicePeak = 0.f;
  }
}

template class ClipperEngine<float>;
template class ClipperEngine<double>;

} // namespace ironclip::dsp
