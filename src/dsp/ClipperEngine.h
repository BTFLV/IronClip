#pragma once

#include <array>

#include "Oversampler.h"
#include "Smoothing.h"
#include "Telemetry.h"

namespace ironclip::dsp
{

namespace ranges
{
constexpr double kDriveMinDb = -12.0;
constexpr double kDriveMaxDb = 24.0;
constexpr double kCeilingMinDb = -30.0;
constexpr double kCeilingMaxDb = 0.0;
constexpr double kOutputMinDb = -24.0;
constexpr double kOutputMaxDb = 12.0;
} // namespace ranges

struct ClipperParams
{
  double driveDb = 0.0;
  double ceilingDb = 0.0;
  double outputDb = 0.0;
  Oversampling oversampling = Oversampling::Off;
  bool delta = false;
  bool autoGain = false;
  bool bypass = false;
};

/** Symmetric hard clipper with optional oversampling, delta monitoring,
 *  automatic level matching and a latency-compensated soft bypass.
 *
 *  Signal flow per channel:
 *    u   = input * drive
 *    y   = clamp(u, -ceiling, +ceiling)        (at the oversampled rate)
 *    out = (delta ? u - y : y) * output * autoGain
 *  Bypass crossfades to the input delayed by the oversampling latency.
 *
 *  All methods are real-time safe: no allocation, locks or system calls. */
template <typename T>
class ClipperEngine
{
public:
  static constexpr int kMaxChannels = 2;
  static constexpr int kBlock = Oversampler<T>::kMaxBlock;

  // Parameter and transition timing
  static constexpr double kGainRampSeconds = 0.020;
  static constexpr double kMixRampSeconds = 0.020;
  static constexpr double kModeFadeSeconds = 0.005;

  // Auto gain
  static constexpr double kAutoGainDetectorSeconds = 0.400; // energy averaging time constant
  static constexpr double kAutoGainSmoothingSeconds = 0.100; // gain glide time constant
  static constexpr double kAutoGainGateDb = -60.0;          // estimate holds below this level
  static constexpr double kAutoGainMinDb = -6.0;            // bounds of the clipping loss estimate
  static constexpr double kAutoGainMaxDb = 36.0;

  static constexpr double kHistorySliceSeconds = 0.010;

  static constexpr int LatencyFor(Oversampling mode) noexcept { return Oversampler<T>::Latency(mode); }

  ClipperEngine();

  /** Sets the sample rate and clears all state. Not real-time critical. */
  void Prepare(double sampleRate) noexcept;

  /** Clears audio state (filters, delay lines) and snaps all smoothing to the
   *  current parameter targets. The auto gain estimate is kept. */
  void Reset() noexcept;

  /** Updates parameter targets. Call before Process() on the audio thread. */
  void SetParameters(const ClipperParams& params) noexcept;

  /** Processes numFrames samples. Inputs and outputs may alias. Mono input
   *  feeding a stereo output is duplicated; missing inputs read as silence. */
  void Process(const T* const* inputs, int numInputs, T* const* outputs, int numOutputs, int numFrames) noexcept;

  /** Latency of the oversampling mode currently in use by the audio path. */
  int Latency() const noexcept { return LatencyFor(mActiveMode); }
  Oversampling ActiveMode() const noexcept { return mActiveMode; }
  double SampleRate() const noexcept { return mSampleRate; }

  /** Smoothed auto gain currently applied (0 dB when auto gain is off). */
  double AppliedAutoGainDb() const noexcept { return mAutoGainDb; }

  Telemetry& GetTelemetry() noexcept { return mTelemetry; }

private:
  void ProcessSubBlock(const T* const* inputs, int numInputs, T* const* outputs, int numChannels, int offset, int n) noexcept;
  void UpdateModeSwitch() noexcept;
  void WarmUpOversampler(int ch) noexcept;
  void PrepareAutoGain(int n) noexcept;
  void UpdateAutoGainEstimate(double sumDriven2, double sumOut2, int count, int n) noexcept;
  void ClearAudioState() noexcept;

  // Longer than the round-trip impulse response of the 8x cascade (2 x 71 + 1)
  static constexpr int kWarmUpSamples = 2 * Oversampler<T>::kMaxLatency + 16;

  struct DryDelay
  {
    static constexpr int kSize = 256; // power of two > max latency + block
    static_assert(kSize > Oversampler<T>::kMaxLatency + kBlock);
    static_assert(kSize >= kWarmUpSamples);
    std::array<T, kSize> buffer {};
    int write = 0;
  };

  double mSampleRate = 48000.0;
  ClipperParams mParams;

  Oversampling mRequestedMode = Oversampling::Off;
  Oversampling mActiveMode = Oversampling::Off;

  LinearRamp mDrive, mCeiling, mOutput, mDeltaMix, mBypassMix, mModeGain;

  // Auto gain
  double mEnergyDriven = 0.0;
  double mEnergyOut = 0.0;
  double mClipLossDb = 0.0; // estimated level lost to clipping
  double mAutoGainDb = 0.0; // smoothed gain being applied
  double mAutoGainLinear = 1.0;
  double mCachedDriveGain = -1.0;
  double mCachedDriveDb = 0.0;
  double mDetectorCoeffBlock = 0.0;
  double mGlideCoeffBlock = 0.0;
  int mAutoGainHold = 0;    // samples to ignore after a mode switch

  // History slicing
  int mSliceLength = 480;
  int mSliceCount = 0;
  float mSlicePeak = 0.f;

  std::array<Oversampler<T>, kMaxChannels> mOversamplers;
  std::array<DryDelay, kMaxChannels> mDry;

  // Per sub-block scratch
  alignas(32) std::array<T, kBlock> mDriveBuf {}, mCeilingBuf {}, mGainBuf {}, mDeltaBuf {}, mBypassBuf {}, mModeBuf {};
  alignas(32) std::array<T, kBlock> mX {}, mU {}, mW {}, mDryAligned {};
  alignas(32) std::array<T, kBlock * Oversampler<T>::kMaxFactor> mHigh {};
  alignas(32) std::array<T, kBlock> mSilence {}, mMonoInput {};

  Telemetry mTelemetry;
};

extern template class ClipperEngine<float>;
extern template class ClipperEngine<double>;

} // namespace ironclip::dsp
