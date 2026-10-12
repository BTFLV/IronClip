#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace ironclip::dsp
{

/** One slice of the clipping history (roughly 10 ms of audio). */
struct HistoryPoint
{
  float drivenPeak = 0.f; // peak |input x drive| before clipping (linear)
  float ceiling = 1.f;    // clipping threshold during the slice (linear)
};

/** Lock-free, wait-free data path from the audio thread to the editor.
 *  The audio thread is the only producer, the UI thread the only consumer. */
class Telemetry
{
public:
  static constexpr std::uint32_t kHistoryCapacity = 1024; // power of two

  // Audio thread ---------------------------------------------------------

  void AccumulatePeaks(float inputPeak, float outputPeak, float overshoot) noexcept
  {
    AtomicMax(mInputPeak, inputPeak);
    AtomicMax(mOutputPeak, outputPeak);
    AtomicMax(mOvershoot, overshoot);
  }

  void SetAutoGainDb(float db) noexcept { mAutoGainDb.store(db, std::memory_order_relaxed); }

  void PushHistory(const HistoryPoint& p) noexcept
  {
    const std::uint32_t w = mWrite.load(std::memory_order_relaxed);
    const std::uint32_t r = mRead.load(std::memory_order_acquire);
    if (w - r >= kHistoryCapacity)
      return; // editor is not draining (closed); drop
    mHistory[w & (kHistoryCapacity - 1)] = p;
    mWrite.store(w + 1, std::memory_order_release);
  }

  // UI thread ------------------------------------------------------------

  /** Returns the peaks accumulated since the previous call and clears them. */
  float TakeInputPeak() noexcept { return mInputPeak.exchange(0.f, std::memory_order_relaxed); }
  float TakeOutputPeak() noexcept { return mOutputPeak.exchange(0.f, std::memory_order_relaxed); }
  /** Largest ratio |driven signal| / ceiling (> 1 means clipping). */
  float TakeOvershoot() noexcept { return mOvershoot.exchange(0.f, std::memory_order_relaxed); }
  float AutoGainDb() const noexcept { return mAutoGainDb.load(std::memory_order_relaxed); }

  bool PopHistory(HistoryPoint& p) noexcept
  {
    const std::uint32_t r = mRead.load(std::memory_order_relaxed);
    if (r == mWrite.load(std::memory_order_acquire))
      return false;
    p = mHistory[r & (kHistoryCapacity - 1)];
    mRead.store(r + 1, std::memory_order_release);
    return true;
  }

  void DiscardHistory() noexcept { mRead.store(mWrite.load(std::memory_order_acquire), std::memory_order_release); }

private:
  static void AtomicMax(std::atomic<float>& target, float value) noexcept
  {
    float prev = target.load(std::memory_order_relaxed);
    while (value > prev && !target.compare_exchange_weak(prev, value, std::memory_order_relaxed))
    {
    }
  }

  std::atomic<float> mInputPeak { 0.f };
  std::atomic<float> mOutputPeak { 0.f };
  std::atomic<float> mOvershoot { 0.f };
  std::atomic<float> mAutoGainDb { 0.f };

  std::array<HistoryPoint, kHistoryCapacity> mHistory {};
  alignas(64) std::atomic<std::uint32_t> mWrite { 0 };
  alignas(64) std::atomic<std::uint32_t> mRead { 0 };
};

} // namespace ironclip::dsp
