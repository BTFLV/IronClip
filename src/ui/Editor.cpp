#include "Editor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>

#include "Controls.h"
#include "IControls.h"
#include "IronClip.h"

using namespace iplug;
using namespace iplug::igraphics;

namespace ironclip::ui
{

namespace
{

// Layout in unscaled editor coordinates (PLUG_WIDTH x PLUG_HEIGHT = 860 x 520).
const IRECT kHeader(0.f, 0.f, 860.f, 60.f);
const IRECT kHistory(24.f, 76.f, 540.f, 300.f);
const IRECT kTransfer(552.f, 76.f, 764.f, 300.f);
const IRECT kMeter(776.f, 76.f, 836.f, 492.f);
const IRECT kKnobPanel(24.f, 312.f, 764.f, 492.f);
const IRECT kFooter(24.f, 498.f, 836.f, 516.f);

float GainFromDb(double db) { return static_cast<float>(std::pow(10.0, db / 20.0)); }

/** Drains the audio-thread telemetry once per frame and hands it to the
 *  displays; only controls whose content changed are marked dirty. */
class TelemetryPump
{
public:
  TelemetryPump(IronClip& plugin, ClipMeter* meter, ClipHistory* history, TransferCurve* curve, Footer* footer)
  : mPlugin(plugin)
  , mMeter(meter)
  , mHistory(history)
  , mCurve(curve)
  , mFooter(footer)
  , mLast(std::chrono::steady_clock::now())
  {
    mPlugin.GetTelemetry().DiscardHistory();
    mPlugin.GetTelemetry().TakeOvershoot();
    mPlugin.GetTelemetry().TakeInputPeak();
    mPlugin.GetTelemetry().TakeOutputPeak();
  }

  void Tick()
  {
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::clamp(std::chrono::duration<float>(now - mLast).count(), 0.f, 0.1f);
    mLast = now;

    dsp::Telemetry& t = mPlugin.GetTelemetry();

    const float overshoot = t.TakeOvershoot();
    mMeter->Update(overshoot > 0.f ? 20.f * std::log10(overshoot) : -100.f, dt);

    dsp::HistoryPoint p;
    while (t.PopHistory(p))
      mHistory->Push(p);

    const double ceilingDb = mPlugin.GetParam(kParamCeiling)->Value();
    mHistory->SetCeilingDb(static_cast<float>(ceilingDb));
    mCurve->SetCurve(GainFromDb(mPlugin.GetParam(kParamDrive)->Value()), GainFromDb(ceilingDb), GainFromDb(mPlugin.GetParam(kParamOutput)->Value()));

    // Input marker with a short release so it reads as a level, not a flicker.
    const float peak = t.TakeInputPeak();
    t.TakeOutputPeak();
    mInputPeak = std::max(peak, mInputPeak * std::exp(-dt / 0.25f));
    mCurve->SetInputPeak(mInputPeak < 1e-4f ? 0.f : mInputPeak);

    mFooter->SetStatus(mPlugin.GetLatency(), mPlugin.GetSampleRate(), mPlugin.GetParam(kParamAutoGain)->Bool(), t.AutoGainDb());
  }

private:
  IronClip& mPlugin;
  ClipMeter* mMeter;
  ClipHistory* mHistory;
  TransferCurve* mCurve;
  Footer* mFooter;
  std::chrono::steady_clock::time_point mLast;
  float mInputPeak = 0.f;
};

} // namespace

void BuildEditor(IGraphics& g, IronClip& plugin)
{
  if (g.NControls() > 0)
    return;

  g.SetScaleConstraints(0.75f, 2.0f);
  g.AttachCornerResizer(EUIResizerMode::Scale, false, COLOR_TRANSPARENT, color::kStrokeStrong, color::kAccent, 18.f);
  g.AttachPanelBackground(color::kBackground);
  g.EnableMouseOver(true);
  g.EnableTooltips(true);
  LoadFonts(g);

  g.AttachControl(new Header(kHeader));

  // Header controls, right-aligned
  g.AttachControl(new PillToggle(IRECT(760.f, 16.f, 836.f, 44.f), kParamBypass, "BYPASS", color::kBypass))
    ->SetTooltip("Crossfades to the unprocessed signal, delayed to match the processing latency");
  g.AttachControl(new PillToggle(IRECT(668.f, 16.f, 752.f, 44.f), kParamAutoGain, "AUTO GAIN", color::kAccent))
    ->SetTooltip("Matches the output loudness to the input for honest A/B comparisons");
  g.AttachControl(new PillToggle(IRECT(596.f, 16.f, 660.f, 44.f), kParamDelta, "DELTA", color::kAccent))
    ->SetTooltip("Listen to only what the clipper removes");
  g.AttachControl(new SegmentedControl(IRECT(330.f, 16.f, 576.f, 44.f), kParamOversampling, "OVERSAMPLING", 98.f))
    ->SetTooltip("Linear-phase oversampling reduces aliasing; adds latency when enabled");

  // Displays
  auto* history = new ClipHistory(kHistory);
  auto* curve = new TransferCurve(kTransfer);
  auto* meter = new ClipMeter(kMeter);
  g.AttachControl(history);
  g.AttachControl(curve);
  g.AttachControl(meter);

  // Knobs
  g.AttachControl(new ILambdaControl(kKnobPanel, [](ILambdaControl*, IGraphics& gr, IRECT& r) {
    gr.FillRoundRect(color::kPanel, r, 8.f);
    gr.DrawRoundRect(color::kStroke, r.GetPadded(-0.5f), 8.f, nullptr, 1.f);
    for (int i = 1; i < 3; ++i)
    {
      const float x = r.L + r.W() * i / 3.f;
      gr.DrawLine(color::kGrid, x, r.T + 24.f, x, r.B - 24.f, nullptr, 1.f);
    }
  }, DEFAULT_ANIMATION_DURATION, false, false, kNoParameter, true));

  const struct
  {
    int param;
    const char* label;
    double originDb;
  } knobs[] = { { kParamDrive, "DRIVE", 0.0 }, { kParamCeiling, "CEILING", 0.0 }, { kParamOutput, "OUTPUT", 0.0 } };
  for (int i = 0; i < 3; ++i)
  {
    const IRECT cell = kKnobPanel.SubRectHorizontal(3, i).GetPadded(-12.f);
    g.AttachControl(new ParamKnob(cell.GetCentredInside(176.f, cell.H()), knobs[i].param, knobs[i].label, knobs[i].originDb));
  }

  WDL_String version;
  version.SetFormatted(64, "IRONCLIP %s%s%s", PLUG_VERSION_STR, IRONCLIP_BUILD_ID[0] ? " \xC2\xB7 " : "", IRONCLIP_BUILD_ID);
  auto* footer = new Footer(kFooter, version.Get());
  g.AttachControl(footer);

  auto pump = std::make_shared<TelemetryPump>(plugin, meter, history, curve, footer);
  g.SetDisplayTickFunc([pump]() { pump->Tick(); });
}

} // namespace ironclip::ui
