#include <algorithm>
#include <complex>
#include <cstdint>
#include <limits>
#include <memory>
#include <numbers>
#include <random>
#include <vector>

#include "TestFramework.h"
#include "dsp/ClipperEngine.h"

using namespace ironclip::dsp;

namespace
{

using Buffer = std::vector<std::vector<double>>; // [channel][sample]

template <typename T>
std::unique_ptr<ClipperEngine<T>> MakeEngine(const ClipperParams& p, double fs = 48000.0)
{
  auto e = std::make_unique<ClipperEngine<T>>();
  e->Prepare(fs);
  e->SetParameters(p);
  e->Reset(); // snap smoothing to the targets
  return e;
}

/** Runs `in` through the engine in host blocks of `block` samples. */
template <typename T>
Buffer Render(ClipperEngine<T>& e, const Buffer& in, int block, int numOutputs = -1, bool inPlace = false)
{
  const int numInputs = static_cast<int>(in.size());
  if (numOutputs < 0)
    numOutputs = numInputs;
  const int length = numInputs ? static_cast<int>(in[0].size()) : 0;

  std::vector<std::vector<T>> ins(numInputs), outs(numOutputs);
  for (int c = 0; c < numInputs; ++c)
    ins[c].assign(in[c].begin(), in[c].end());
  for (auto& o : outs)
    o.assign(length, T(0));

  std::vector<const T*> inPtrs(numInputs);
  std::vector<T*> outPtrs(numOutputs);
  for (int pos = 0; pos < length; pos += block)
  {
    const int n = std::min(block, length - pos);
    for (int c = 0; c < numOutputs; ++c)
    {
      outPtrs[c] = outs[c].data() + pos;
      if (inPlace && c < numInputs)
        std::copy(ins[c].begin() + pos, ins[c].begin() + pos + n, outs[c].begin() + pos);
    }
    for (int c = 0; c < numInputs; ++c)
      inPtrs[c] = inPlace && c < numOutputs ? outs[c].data() + pos : ins[c].data() + pos;
    e.Process(inPtrs.data(), numInputs, outPtrs.data(), numOutputs, n);
  }

  Buffer result(numOutputs);
  for (int c = 0; c < numOutputs; ++c)
    result[c].assign(outs[c].begin(), outs[c].end());
  return result;
}

std::vector<double> Sine(double freq, double amp, int length, double fs, double phase = 0.0)
{
  std::vector<double> s(length);
  for (int i = 0; i < length; ++i)
    s[i] = amp * std::sin(2.0 * std::numbers::pi * freq * i / fs + phase);
  return s;
}

std::vector<double> Noise(int length, double amp, std::uint32_t seed)
{
  std::mt19937 rng(seed);
  std::uniform_real_distribution<double> dist(-amp, amp);
  std::vector<double> s(length);
  for (auto& v : s)
    v = dist(rng);
  return s;
}

/** Band-limited noise: white noise through a few passes of a gentle lowpass. */
std::vector<double> SoftNoise(int length, double amp, std::uint32_t seed)
{
  auto s = Noise(length, 1.0, seed);
  for (int pass = 0; pass < 4; ++pass)
  {
    double z = 0.0;
    for (auto& v : s)
      v = z = z + 0.25 * (v - z);
  }
  double peak = 0.0;
  for (double v : s)
    peak = std::max(peak, std::fabs(v));
  for (auto& v : s)
    v *= amp / peak;
  return s;
}

/** Sum of sines well inside the oversampler passband (< 0.4 fs). */
std::vector<double> MultiSine(int length, double amp, double fs)
{
  std::vector<double> s(length, 0.0);
  const double freqs[] = { 61.0, 433.0, 1597.0, 4409.0, 9001.0, 0.37 * fs };
  for (double f : freqs)
  {
    const auto part = Sine(f, amp / 6.0, length, fs, f);
    for (int i = 0; i < length; ++i)
      s[i] += part[i];
  }
  return s;
}

double Rms(const std::vector<double>& s, int from = 0, int to = -1)
{
  if (to < 0)
    to = static_cast<int>(s.size());
  double sum = 0.0;
  for (int i = from; i < to; ++i)
    sum += s[i] * s[i];
  return std::sqrt(sum / std::max(1, to - from));
}

double MaxAbs(const std::vector<double>& s, int from = 0, int to = -1)
{
  if (to < 0)
    to = static_cast<int>(s.size());
  double m = 0.0;
  for (int i = from; i < to; ++i)
    m = std::max(m, std::fabs(s[i]));
  return m;
}

double ToDb(double v) { return 20.0 * std::log10(std::max(v, 1e-300)); }

void Fft(std::vector<std::complex<double>>& a)
{
  const size_t n = a.size();
  for (size_t i = 1, j = 0; i < n; ++i)
  {
    size_t bit = n >> 1;
    for (; j & bit; bit >>= 1)
      j ^= bit;
    j ^= bit;
    if (i < j)
      std::swap(a[i], a[j]);
  }
  for (size_t len = 2; len <= n; len <<= 1)
  {
    const double ang = -2.0 * std::numbers::pi / static_cast<double>(len);
    const std::complex<double> wl(std::cos(ang), std::sin(ang));
    for (size_t i = 0; i < n; i += len)
    {
      std::complex<double> w(1.0);
      for (size_t k = 0; k < len / 2; ++k)
      {
        const auto u = a[i + k], v = a[i + k + len / 2] * w;
        a[i + k] = u + v;
        a[i + k + len / 2] = u - v;
        w *= wl;
      }
    }
  }
}

/** Ratio (dB) of energy at non-harmonic bins to energy at harmonic bins for a
 *  periodic signal whose fundamental sits exactly on FFT bin `k`. */
double AliasToSignalDb(const std::vector<double>& s, int start, int n, int k)
{
  std::vector<std::complex<double>> spec(n);
  for (int i = 0; i < n; ++i)
    spec[i] = s[start + i];
  Fft(spec);
  double harmonic = 0.0, alias = 0.0;
  for (int b = 1; b < n / 2; ++b)
  {
    const double e = std::norm(spec[b]);
    (b % k == 0 ? harmonic : alias) += e;
  }
  return 10.0 * std::log10(alias / harmonic);
}

constexpr Oversampling kAllModes[] = { Oversampling::Off, Oversampling::X2, Oversampling::X4, Oversampling::X8 };

const char* ModeName(Oversampling m)
{
  switch (m)
  {
    case Oversampling::Off: return "off";
    case Oversampling::X2: return "2x";
    case Oversampling::X4: return "4x";
    case Oversampling::X8: return "8x";
  }
  return "?";
}

template <typename T>
void CheckStaticTransfer(double tolerance)
{
  ClipperParams p;
  p.driveDb = 6.0;
  p.ceilingDb = -6.0;
  p.outputDb = -3.0;
  auto e = MakeEngine<T>(p);

  std::vector<double> ramp(4001);
  for (int i = 0; i < 4001; ++i)
    ramp[i] = -2.0 + i * 0.001; // -2 .. +2, passes through 0 and both knees
  const Buffer out = Render(*e, { ramp }, 512);

  const double drive = DbToGain(6.0), ceiling = DbToGain(-6.0), gain = DbToGain(-3.0);
  for (int i = 0; i < 4001; ++i)
  {
    const double x = static_cast<double>(static_cast<T>(ramp[i]));
    const double expected = std::clamp(x * drive, -ceiling, ceiling) * gain;
    CHECK_NEAR(out[0][i], expected, tolerance);
  }
}

} // namespace

// ---------------------------------------------------------------------------
// Transfer function

TEST("transfer: exact hard clipping, float")
{
  CheckStaticTransfer<float>(1e-6);
}

TEST("transfer: exact hard clipping, double")
{
  CheckStaticTransfer<double>(1e-14);
}

TEST("transfer: odd symmetry and both thresholds")
{
  ClipperParams p;
  p.driveDb = 12.0;
  p.ceilingDb = -9.0;
  auto e = MakeEngine<double>(p);
  const auto in = Sine(997.0, 0.9, 4800, 48000.0);
  std::vector<double> neg(in.size());
  std::transform(in.begin(), in.end(), neg.begin(), [](double v) { return -v; });

  auto e2 = MakeEngine<double>(p);
  const Buffer a = Render(*e, { in }, 256);
  const Buffer b = Render(*e2, { neg }, 256);

  const double ceiling = DbToGain(-9.0);
  double maxPos = -1.0, minNeg = 1.0;
  for (size_t i = 0; i < in.size(); ++i)
  {
    CHECK_NEAR(a[0][i], -b[0][i], 0.0);
    maxPos = std::max(maxPos, a[0][i]);
    minNeg = std::min(minNeg, a[0][i]);
  }
  CHECK_NEAR(maxPos, ceiling, 1e-12);  // positive threshold reached exactly
  CHECK_NEAR(minNeg, -ceiling, 1e-12); // negative threshold reached exactly
}

TEST("transfer: below threshold the clipper is a pure gain (drive x output)")
{
  ClipperParams p;
  p.driveDb = 7.5;
  p.ceilingDb = 0.0;
  p.outputDb = -4.25;
  auto e = MakeEngine<double>(p);
  const auto in = Sine(440.0, 0.25, 2000, 48000.0); // peak 0.25 * 2.37 < 1
  const Buffer out = Render(*e, { in }, 64);
  const double g = DbToGain(7.5 - 4.25);
  for (size_t i = 0; i < in.size(); ++i)
    CHECK_NEAR(out[0][i], in[i] * g, 1e-12);
}

TEST("transfer: parameter boundaries and invalid values are clamped")
{
  {
    ClipperParams p;
    p.driveDb = ranges::kDriveMaxDb;
    p.ceilingDb = ranges::kCeilingMinDb;
    p.outputDb = ranges::kOutputMaxDb;
    auto e = MakeEngine<double>(p);
    const Buffer out = Render(*e, { Sine(100.0, 1.0, 4800, 48000.0) }, 128);
    CHECK_NEAR(MaxAbs(out[0]), DbToGain(ranges::kCeilingMinDb + ranges::kOutputMaxDb), 1e-12);
  }
  {
    ClipperParams p;
    p.driveDb = ranges::kDriveMinDb;
    p.ceilingDb = ranges::kCeilingMaxDb;
    p.outputDb = ranges::kOutputMinDb;
    auto e = MakeEngine<double>(p);
    const Buffer out = Render(*e, { Sine(100.0, 1.0, 4800, 48000.0) }, 128);
    CHECK_NEAR(MaxAbs(out[0]), DbToGain(ranges::kDriveMinDb + ranges::kOutputMinDb), 1e-6);
  }
  {
    ClipperParams p;
    p.driveDb = 1000.0;
    p.ceilingDb = std::numeric_limits<double>::quiet_NaN();
    p.outputDb = -std::numeric_limits<double>::infinity();
    p.oversampling = static_cast<Oversampling>(17);
    auto e = MakeEngine<float>(p);
    const Buffer out = Render(*e, { Sine(100.0, 1.0, 4800, 48000.0) }, 128);
    // drive clamps to +24 dB, ceiling falls back to 0 dBFS, output clamps to -24 dB
    CHECK_NEAR(MaxAbs(out[0]), DbToGain(-24.0), 1e-6);
    CHECK(e->ActiveMode() == Oversampling::Off);
  }
}

// ---------------------------------------------------------------------------
// Channels, silence, edge cases

TEST("channels: stereo channels are independent and match mono processing")
{
  ClipperParams p;
  p.driveDb = 9.0;
  p.ceilingDb = -3.0;
  p.oversampling = Oversampling::X4;
  const auto left = Sine(300.0, 0.8, 9600, 48000.0);
  const auto right = SoftNoise(9600, 0.9, 7);

  auto stereo = MakeEngine<double>(p);
  const Buffer s = Render(*stereo, { left, right }, 333);
  auto monoL = MakeEngine<double>(p);
  auto monoR = MakeEngine<double>(p);
  const Buffer l = Render(*monoL, { left }, 333);
  const Buffer r = Render(*monoR, { right }, 333);

  CHECK(s[0] == l[0]);
  CHECK(s[1] == r[0]);
}

TEST("channels: mono input feeds both outputs, including in-place hosts")
{
  ClipperParams p;
  p.driveDb = 6.0;
  p.oversampling = Oversampling::X2;
  const auto in = Sine(1234.0, 0.9, 4800, 48000.0);
  auto a = MakeEngine<float>(p);
  auto b = MakeEngine<float>(p);
  const Buffer separate = Render(*a, { in }, 100, 2, false);
  const Buffer inPlace = Render(*b, { in }, 100, 2, true);
  CHECK(separate[0] == separate[1]);
  CHECK(inPlace[0] == separate[0]);
  CHECK(inPlace[1] == separate[1]);
}

TEST("silence: exact zeros in, exact zeros out, in every mode")
{
  for (auto mode : kAllModes)
  {
    for (bool ag : { false, true })
    {
      ClipperParams p;
      p.driveDb = 24.0;
      p.oversampling = mode;
      p.autoGain = ag;
      auto e = MakeEngine<float>(p);
      const Buffer out = Render(*e, { std::vector<double>(48000, 0.0), std::vector<double>(48000, 0.0) }, 512);
      CHECK(MaxAbs(out[0]) == 0.0);
      CHECK(MaxAbs(out[1]) == 0.0);
      CHECK(std::isfinite(e->AppliedAutoGainDb()));
    }
  }
}

TEST("edge cases: NaN, infinity and denormal input stay contained")
{
  for (auto mode : kAllModes)
  {
    ClipperParams p;
    p.ceilingDb = -6.0;
    p.oversampling = mode;
    auto e = MakeEngine<float>(p);

    auto in = Sine(500.0, 0.5, 9600, 48000.0);
    in[1000] = std::numeric_limits<double>::quiet_NaN();
    in[2000] = std::numeric_limits<double>::infinity();
    in[3000] = -std::numeric_limits<double>::infinity();
    for (int i = 4000; i < 4100; ++i)
      in[i] = 1e-41; // denormal in float
    const Buffer out = Render(*e, { in }, 256);

    const double ceiling = DbToGain(-6.0);
    bool finite = true;
    for (double v : out[0])
      finite = finite && std::isfinite(v);
    CHECK(finite);
    if (mode == Oversampling::Off)
    {
      CHECK(out[0][1000] == 0.0);
      CHECK_NEAR(out[0][2000], ceiling, 1e-6);
      CHECK_NEAR(out[0][3000], -ceiling, 1e-6);
    }
    // well after the events, processing is back to normal
    const int lat = e->Latency();
    CHECK_LT(MaxAbs(out[0], 9000 + lat, 9600), ceiling * 1.02);
    CHECK_LT(ceiling * 0.9, MaxAbs(out[0], 9000, 9600));
  }
}

TEST("edge cases: auto gain survives non-finite input and recovers")
{
  ClipperParams p;
  p.driveDb = 6.0;
  p.ceilingDb = -6.0;
  p.autoGain = true;
  auto e = MakeEngine<float>(p);
  auto in = SoftNoise(12 * 48000, 0.5, 17);
  in[48000] = std::numeric_limits<double>::infinity();
  in[48001] = std::numeric_limits<double>::quiet_NaN();
  const Buffer out = Render(*e, { in }, 512);
  bool finite = true;
  for (double v : out[0])
    finite = finite && std::isfinite(v);
  CHECK(finite);
  CHECK(std::isfinite(e->AppliedAutoGainDb()));
  CHECK_LT(e->AppliedAutoGainDb(), 48.0 + 1e-9);
  // ten seconds later the level match is back on target
  CHECK_NEAR(ToDb(Rms(out[0], 11 * 48000)), ToDb(Rms(in, 11 * 48000)), 0.5);
}

// ---------------------------------------------------------------------------
// Oversampling

TEST("oversampling: reported latencies")
{
  CHECK(ClipperEngine<float>::LatencyFor(Oversampling::Off) == 0);
  CHECK(ClipperEngine<float>::LatencyFor(Oversampling::X2) == 63);
  CHECK(ClipperEngine<float>::LatencyFor(Oversampling::X4) == 69);
  CHECK(ClipperEngine<float>::LatencyFor(Oversampling::X8) == 71);
}

TEST("oversampling: impulse response peaks exactly at the reported latency")
{
  for (auto mode : kAllModes)
  {
    ClipperParams p;
    p.oversampling = mode;
    auto e = MakeEngine<double>(p);
    std::vector<double> in(1024, 0.0);
    in[100] = 0.5;
    const Buffer out = Render(*e, { in }, 64);
    int peakIdx = 0;
    for (int i = 0; i < 1024; ++i)
      if (std::fabs(out[0][i]) > std::fabs(out[0][peakIdx]))
        peakIdx = i;
    CHECK(peakIdx == 100 + e->Latency());
    // symmetric (linear-phase) response around the peak
    for (int k = 1; k < 40; ++k)
      CHECK_NEAR(out[0][peakIdx - k], out[0][peakIdx + k], 1e-12);
  }
}

TEST("oversampling: unclipped audio passes as a pure delay")
{
  for (double fs : { 44100.0, 48000.0, 96000.0 })
  {
    for (auto mode : kAllModes)
    {
      ClipperParams p;
      p.oversampling = mode;
      auto e = MakeEngine<double>(p, fs);
      // 20 Hz .. 0.4 fs content, never reaching the ceiling
      std::vector<double> in(16384, 0.0);
      for (double f : { 20.0, 1000.0, 5000.0, 0.3 * fs, 0.4 * fs })
      {
        const auto s = Sine(f, 0.15, 16384, fs, f);
        for (size_t i = 0; i < in.size(); ++i)
          in[i] += s[i];
      }
      const Buffer out = Render(*e, { in }, 480);
      const int lat = e->Latency();
      double err = 0.0;
      for (int i = 2048; i < 16384; ++i)
        err = std::max(err, std::fabs(out[0][i] - in[i - lat]));
      // passband ripple of the cascade is below 0.0001 dB (~1.2e-5 relative)
      CHECK_LT(err, 2e-5);
    }
  }
}

TEST("oversampling: aliasing is strongly reduced")
{
  // 48 kHz, fundamental exactly on an FFT bin so every component is bin-centred;
  // heavy clipping of a 6.5 kHz tone folds many harmonics back below Nyquist
  const double fs = 48000.0;
  const int n = 16384;
  const int bin = 2213; // 6483 Hz
  const double f0 = bin * fs / n;
  double asr[4];
  for (auto mode : kAllModes)
  {
    ClipperParams p;
    p.driveDb = 18.0;
    p.ceilingDb = -6.0;
    p.oversampling = mode;
    auto e = MakeEngine<double>(p, fs);
    const Buffer out = Render(*e, { Sine(f0, 0.9, 4096 + n, fs) }, 512);
    asr[static_cast<int>(mode)] = AliasToSignalDb(out[0], 4096, n, bin);
    std::printf("    alias-to-signal ratio, %s: %.1f dB\n", ModeName(mode), asr[static_cast<int>(mode)]);
  }
  // A near-square wave has slowly decaying harmonics, so each doubling buys
  // a steady improvement rather than a single huge step.
  CHECK_LT(asr[1], asr[0] - 6.0);
  CHECK_LT(asr[2], asr[1] - 6.0);
  CHECK_LT(asr[3], asr[2] - 6.0);
  CHECK_LT(asr[3], asr[0] - 30.0);
}

TEST("oversampling: switching modes is click-free and settles on the new latency")
{
  ClipperParams p;
  p.driveDb = 6.0;
  auto e = MakeEngine<float>(p);
  const double fs = 48000.0;
  const auto in = Sine(220.0, 0.5, 48000, fs);
  std::vector<const float*> ip(1);
  std::vector<float*> op(1);
  std::vector<float> inF(in.begin(), in.end()), out(in.size());

  const Oversampling sequence[] = { Oversampling::X8, Oversampling::X2, Oversampling::Off, Oversampling::X4 };
  int step = 0;
  for (int pos = 0; pos < 48000; pos += 96)
  {
    if (pos % 9600 == 4800 && step < 4)
    {
      p.oversampling = sequence[step++];
      e->SetParameters(p);
    }
    ip[0] = inF.data() + pos;
    op[0] = out.data() + pos;
    e->Process(ip.data(), 1, op.data(), 1, 96);
  }
  CHECK(step == 4);
  CHECK(e->ActiveMode() == Oversampling::X4);
  CHECK(e->Latency() == 69);

  // A 220 Hz sine at this level moves at most ~0.03 per sample; the fades and
  // the warmed-up filters keep every switch far below a hard cut (~1.0).
  double maxJump = 0.0;
  for (size_t i = 1; i < out.size(); ++i)
  {
    CHECK(std::isfinite(out[i]));
    maxJump = std::max(maxJump, static_cast<double>(std::fabs(out[i] - out[i - 1])));
  }
  CHECK_LT(maxJump, 0.06);
}

// ---------------------------------------------------------------------------
// Delta and bypass

TEST("delta: processed + delta nulls against the delayed driven input")
{
  for (auto mode : kAllModes)
  {
    ClipperParams p;
    p.driveDb = 9.0;
    p.ceilingDb = -4.0;
    p.outputDb = -2.0;
    p.oversampling = mode;
    const auto in = MultiSine(24000, 1.2, 48000.0);

    auto wet = MakeEngine<double>(p);
    const Buffer w = Render(*wet, { in }, 256);
    p.delta = true;
    auto del = MakeEngine<double>(p);
    const Buffer d = Render(*del, { in }, 256);

    const int lat = wet->Latency();
    const double g = DbToGain(9.0 - 2.0);
    double err = 0.0, deltaRms = Rms(d[0], 4096);
    for (int i = 4096; i < 24000; ++i)
      err = std::max(err, std::fabs(w[0][i] + d[0][i] - in[i - lat] * g));
    std::printf("    %s: null residual %.1f dB, delta level %.1f dBFS RMS\n", ModeName(mode), ToDb(err), ToDb(deltaRms));
    CHECK_LT(deltaRms, 1.0);
    CHECK_LT(1e-3, deltaRms); // there is clipping to hear
    // The test signal is band-limited, so the oversampling round trip is transparent.
    CHECK_LT(err, mode == Oversampling::Off ? 1e-12 : 1e-4);
  }
}

TEST("delta: silent when nothing is clipped")
{
  for (auto mode : kAllModes)
  {
    ClipperParams p;
    p.oversampling = mode;
    p.delta = true;
    auto e = MakeEngine<double>(p);
    const Buffer out = Render(*e, { Sine(1000.0, 0.5, 9600, 48000.0) }, 256);
    CHECK(MaxAbs(out[0]) == 0.0);
  }
}

TEST("bypass: settled bypass is the input delayed by the latency, bit-exact")
{
  for (auto mode : kAllModes)
  {
    ClipperParams p;
    p.driveDb = 20.0;
    p.ceilingDb = -20.0;
    p.oversampling = mode;
    p.bypass = true;
    auto e = MakeEngine<float>(p);
    const auto in = SoftNoise(9600, 0.95, 11);
    const Buffer out = Render(*e, { in }, 97);
    const int lat = e->Latency();
    for (int i = 0; i < 9600; ++i)
      CHECK(out[0][i] == (i >= lat ? static_cast<double>(static_cast<float>(in[i - lat])) : 0.0));
  }
}

TEST("bypass: engaging and releasing crossfades without discontinuities")
{
  for (auto mode : { Oversampling::Off, Oversampling::X4 })
  {
    ClipperParams p;
    p.driveDb = 18.0;
    p.ceilingDb = -12.0;
    p.oversampling = mode;
    auto e = MakeEngine<double>(p);
    const auto in = Sine(100.0, 0.9, 48000, 48000.0);
    std::vector<double> out(in.size());
    for (int pos = 0; pos < 48000; pos += 64)
    {
      if (pos == 9600 || pos == 28800)
      {
        p.bypass = pos == 9600;
        e->SetParameters(p);
      }
      const double* ip[] = { in.data() + pos };
      double* op[] = { out.data() + pos };
      e->Process(ip, 1, op, 1, 64);
    }
    auto maxJump = [&](int from, int to) {
      double m = 0.0;
      for (int i = from + 1; i < to; ++i)
        m = std::max(m, std::fabs(out[i] - out[i - 1]));
      return m;
    };
    // Natural slopes of the settled wet (steep clipped edges) and dry signals.
    const double natural = std::max({ maxJump(2000, 9600), maxJump(11000, 28800), maxJump(30000, 48000) });
    // Wet and dry differ by up to ~0.65 here; a hard switch would add that in
    // one sample. The 20 ms crossfade adds less than 0.001 per sample.
    // (The first 2000 samples are skipped: right after Reset() the filters start
    // cold against a full-scale signal, which only happens in this test.)
    CHECK_LT(maxJump(2000, 48000), natural + 0.002);
    // fully bypassed region equals the delayed input
    const int lat = e->Latency();
    for (int i = 15000; i < 28000; ++i)
      CHECK_NEAR(out[i], in[i - lat], 1e-15);
  }
}

// ---------------------------------------------------------------------------
// Auto gain

TEST("auto gain: matches input loudness and stays stable")
{
  for (auto mode : { Oversampling::Off, Oversampling::X4 })
  {
    ClipperParams p;
    p.driveDb = 15.0;
    p.ceilingDb = -3.0;
    p.autoGain = true;
    p.oversampling = mode;
    const double fs = 48000.0;
    auto e = MakeEngine<double>(p, fs);
    const int length = 6 * 48000;
    const auto in = SoftNoise(length, 0.7, 5);
    std::vector<double> out(length);
    std::vector<double> gains;
    for (int pos = 0; pos < length; pos += 480)
    {
      const double* ip[] = { in.data() + pos };
      double* op[] = { out.data() + pos };
      e->Process(ip, 1, op, 1, 480);
      gains.push_back(e->AppliedAutoGainDb());
    }
    const int from = 3 * 48000;
    const double inRms = ToDb(Rms(in, from, length));
    const double outRms = ToDb(Rms(out, from, length));
    double gMin = 1e9, gMax = -1e9;
    for (size_t i = gains.size() / 2; i < gains.size(); ++i)
    {
      gMin = std::min(gMin, gains[i]);
      gMax = std::max(gMax, gains[i]);
    }
    std::printf("    %s: in %.2f dBFS, out %.2f dBFS, gain %.2f..%.2f dB\n", ModeName(mode), inRms, outRms, gMin, gMax);
    CHECK_NEAR(outRms, inRms, 0.5);
    CHECK_LT(gMax - gMin, 0.5);
  }
}

TEST("auto gain: holds through silence and never runs away")
{
  ClipperParams p;
  p.driveDb = 12.0;
  p.ceilingDb = -6.0;
  p.autoGain = true;
  auto e = MakeEngine<float>(p);
  const double fs = 48000.0;
  std::vector<double> in = SoftNoise(3 * 48000, 0.8, 9);
  in.resize(13 * 48000, 0.0);                  // 10 s of silence
  const auto tail = SoftNoise(48000, 1e-5, 4); // -100 dBFS noise floor afterwards
  in.insert(in.end(), tail.begin(), tail.end());

  double atSilence = 0.0, minGain = 1e9, maxGain = -1e9;
  for (size_t pos = 0; pos < in.size(); pos += 256)
  {
    const int n = static_cast<int>(std::min<size_t>(256, in.size() - pos));
    std::vector<float> buf(in.begin() + pos, in.begin() + pos + n);
    const float* ip[] = { buf.data() };
    float* op[] = { buf.data() };
    e->Process(ip, 1, op, 1, n);
    const double g = e->AppliedAutoGainDb();
    if (pos >= 3 * 48000 && atSilence == 0.0)
      atSilence = g;
    if (pos >= 3 * 48000)
    {
      minGain = std::min(minGain, g);
      maxGain = std::max(maxGain, g);
    }
  }
  CHECK(std::isfinite(maxGain));
  CHECK_LT(maxGain - minGain, 1.0);
  CHECK_NEAR(e->AppliedAutoGainDb(), atSilence, 1.0);
  (void)fs;
}

TEST("auto gain: off means unity, toggling glides")
{
  ClipperParams p;
  p.driveDb = 12.0;
  auto e = MakeEngine<double>(p);
  const auto in = SoftNoise(4 * 48000, 0.8, 21);
  std::vector<double> out(in.size());
  double maxStep = 0.0, prev = 0.0;
  for (int pos = 0; pos < static_cast<int>(in.size()); pos += 64)
  {
    if (pos == 2 * 48000)
    {
      CHECK(e->AppliedAutoGainDb() == 0.0);
      p.autoGain = true;
      e->SetParameters(p);
    }
    const double* ip[] = { in.data() + pos };
    double* op[] = { out.data() + pos };
    e->Process(ip, 1, op, 1, 64);
    const double g = e->AppliedAutoGainDb();
    maxStep = std::max(maxStep, std::fabs(g - prev));
    prev = g;
  }
  CHECK_LT(e->AppliedAutoGainDb(), -6.0); // pulled down to compensate the drive
  CHECK_LT(maxStep, 1.0);                 // dB per 64-sample block: a glide, not a jump
}

// ---------------------------------------------------------------------------
// Automation, block sizes and sample rates

TEST("automation: smoothing reaches targets and never exceeds the ceiling envelope")
{
  ClipperParams p;
  p.driveDb = 12.0;
  p.ceilingDb = -6.0;
  auto e = MakeEngine<double>(p);
  const auto in = Sine(150.0, 1.0, 48000, 48000.0);
  std::vector<double> out(in.size());
  double oldCeiling = DbToGain(-6.0), newCeiling = oldCeiling;
  for (int pos = 0; pos < 48000; pos += 32)
  {
    if (pos % 2400 == 0) // changes every 50 ms; the ceiling ramp takes 20 ms
    {
      oldCeiling = newCeiling;
      p.ceilingDb = (pos / 2400) % 2 ? -18.0 : -3.0;
      p.driveDb = (pos / 2400) % 3 ? 3.0 : 18.0;
      e->SetParameters(p);
      newCeiling = DbToGain(p.ceilingDb);
    }
    const double* ip[] = { in.data() + pos };
    double* op[] = { out.data() + pos };
    e->Process(ip, 1, op, 1, 32);
    const bool ramping = pos % 2400 < 960;
    const double bound = ramping ? std::max(oldCeiling, newCeiling) : newCeiling;
    CHECK_LT(MaxAbs(out, pos, pos + 32), bound + 1e-12);
  }
  // after the final change has settled the output is exactly the static curve
  const double drive = DbToGain(p.driveDb), ceiling = DbToGain(p.ceilingDb);
  for (int i = 47000; i < 48000; ++i)
    CHECK_NEAR(out[i], std::clamp(in[i] * drive, -ceiling, ceiling), 1e-12);
}

TEST("automation: gain changes ramp instead of stepping")
{
  ClipperParams p;
  p.ceilingDb = 0.0;
  auto e = MakeEngine<double>(p);
  std::vector<double> in(9600, 0.25), out(9600);
  for (int pos = 0; pos < 9600; pos += 64)
  {
    if (pos == 4800)
    {
      p.outputDb = 12.0;
      e->SetParameters(p);
    }
    const double* ip[] = { in.data() + pos };
    double* op[] = { out.data() + pos };
    e->Process(ip, 1, op, 1, 64);
  }
  double maxStep = 0.0;
  for (int i = 1; i < 9600; ++i)
    maxStep = std::max(maxStep, std::fabs(out[i] - out[i - 1]));
  CHECK_LT(maxStep, 0.01);
  CHECK_NEAR(out[9599], 0.25 * DbToGain(12.0), 1e-12);
}

TEST("block sizes: output is independent of the host block size")
{
  for (auto mode : kAllModes)
  {
    ClipperParams p;
    p.driveDb = 10.0;
    p.ceilingDb = -5.0;
    p.oversampling = mode;
    const auto in = SoftNoise(20000, 0.9, 13);
    auto ref = MakeEngine<float>(p);
    const Buffer expected = Render(*ref, { in }, 64);
    for (int block : { 1, 2, 7, 63, 65, 100, 512, 4096, 20000 })
    {
      auto e = MakeEngine<float>(p);
      const Buffer out = Render(*e, { in }, block);
      CHECK(out[0] == expected[0]);
    }
  }
}

TEST("sample rates: transfer, latency alignment and ramps at common and extreme rates")
{
  for (double fs : { 22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0, 384000.0 })
  {
    for (auto mode : { Oversampling::Off, Oversampling::X8 })
    {
      ClipperParams p;
      p.driveDb = 6.0;
      p.ceilingDb = -6.0;
      p.oversampling = mode;
      p.bypass = true;
      auto e = MakeEngine<double>(p, fs);
      const auto in = Sine(fs / 97.0, 0.9, 8192, fs);
      const Buffer bypassed = Render(*e, { in }, 300);
      const int lat = e->Latency();
      CHECK_NEAR(bypassed[0][5000], in[5000 - lat], 1e-15);

      p.bypass = false;
      auto w = MakeEngine<double>(p, fs);
      const Buffer wet = Render(*w, { in }, 300);
      const double peak = MaxAbs(wet[0], 1000);
      if (mode == Oversampling::Off)
        CHECK_NEAR(peak, DbToGain(-6.0), 1e-12);
      else
        CHECK_LT(std::fabs(ToDb(peak) + 6.0), 1.0); // band-limited clip may overshoot slightly
    }
  }
}

// ---------------------------------------------------------------------------
// Telemetry

TEST("telemetry: clip meter reports the overshoot above the ceiling in dB")
{
  ClipperParams p;
  p.driveDb = 6.0;
  p.ceilingDb = -6.0;
  auto e = MakeEngine<float>(p);
  e->GetTelemetry().TakeOvershoot();
  Render(*e, { Sine(1000.0, 1.0, 4800, 48000.0) }, 256);
  CHECK_NEAR(ToDb(e->GetTelemetry().TakeOvershoot()), 12.0, 0.01);
  CHECK(e->GetTelemetry().TakeOvershoot() == 0.f); // taking clears

  // ~100 history slices per second
  int count = 0;
  HistoryPoint hp;
  while (e->GetTelemetry().PopHistory(hp))
  {
    ++count;
    CHECK_NEAR(hp.ceiling, DbToGain(-6.0), 1e-6);
  }
  CHECK(count == 10);
}

int main(int argc, char** argv)
{
  return testing::RunAll(argc > 1 ? argv[1] : nullptr);
}
