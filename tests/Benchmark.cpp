// Measures the DSP cost per sample for every oversampling mode.
// Usage: ironclip-benchmark [seconds-of-audio-per-run]

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <vector>

#include "dsp/ClipperEngine.h"

using namespace ironclip::dsp;

namespace
{

template <typename T>
double Measure(Oversampling mode, bool extras, int block, double seconds)
{
  constexpr double fs = 48000.0;
  const int total = static_cast<int>(seconds * fs);

  ClipperEngine<T> engine;
  engine.Prepare(fs);
  ClipperParams p;
  p.driveDb = 12.0;
  p.ceilingDb = -3.0;
  p.oversampling = mode;
  p.autoGain = extras;
  engine.SetParameters(p);
  engine.Reset();

  std::vector<T> l(block), r(block), ol(block), orr(block);
  for (int i = 0; i < block; ++i)
  {
    l[i] = static_cast<T>(0.8 * std::sin(2.0 * std::numbers::pi * 997.0 * i / fs));
    r[i] = static_cast<T>(0.8 * std::sin(2.0 * std::numbers::pi * 1499.0 * i / fs));
  }
  const T* in[] = { l.data(), r.data() };
  T* out[] = { ol.data(), orr.data() };

  // warm-up
  for (int i = 0; i < 100; ++i)
    engine.Process(in, 2, out, 2, block);

  double best = 1e30;
  for (int run = 0; run < 5; ++run)
  {
    const auto t0 = std::chrono::steady_clock::now();
    for (int pos = 0; pos < total; pos += block)
    {
      if (extras && (pos / block) % 64 == 0)
      {
        // exercise automation ramps
        p.driveDb = p.driveDb == 12.0 ? 11.0 : 12.0;
        engine.SetParameters(p);
      }
      engine.Process(in, 2, out, 2, block);
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    best = std::min(best, ns / (static_cast<double>(total) * 2.0)); // per sample per channel
  }
  volatile T sink = ol[block - 1] + orr[block - 1];
  (void)sink;
  return best;
}

} // namespace

int main(int argc, char** argv)
{
  const double seconds = argc > 1 ? std::atof(argv[1]) : 20.0;
  const char* names[] = { "off", "2x", "4x", "8x" };
  std::printf("IronClip DSP benchmark: stereo, 48 kHz, %g s of audio per run, best of 5\n", seconds);
  std::printf("ns per sample per channel (lower is better); realtime CPU %% for one stereo instance\n\n");
  std::printf("%-6s %-7s %-6s %12s %12s\n", "type", "mode", "block", "ns/sample", "CPU %");
  for (int m = 0; m < 4; ++m)
  {
    for (int block : { 64, 512 })
    {
      const double nsF = Measure<float>(static_cast<Oversampling>(m), false, block, seconds);
      std::printf("%-6s %-7s %-6d %12.2f %12.3f\n", "float", names[m], block, nsF, nsF * 2.0 * 48000.0 * 1e-9 * 100.0);
    }
  }
  for (int m = 0; m < 4; ++m)
  {
    const double nsD = Measure<double>(static_cast<Oversampling>(m), false, 512, seconds);
    std::printf("%-6s %-7s %-6d %12.2f %12.3f\n", "double", names[m], 512, nsD, nsD * 2.0 * 48000.0 * 1e-9 * 100.0);
  }
  std::printf("\nwith auto gain + automation ramps:\n");
  for (int m = 0; m < 4; ++m)
  {
    const double nsF = Measure<float>(static_cast<Oversampling>(m), true, 512, seconds);
    std::printf("%-6s %-7s %-6d %12.2f %12.3f\n", "float", names[m], 512, nsF, nsF * 2.0 * 48000.0 * 1e-9 * 100.0);
  }
  return 0;
}
