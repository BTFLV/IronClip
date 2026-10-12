#include "Controls.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "IPlugParameter.h"

using namespace iplug;
using namespace iplug::igraphics;

namespace ironclip::ui
{

namespace
{

constexpr float kPi = 3.14159265358979f;
constexpr float kKnobMinAngle = -135.f;
constexpr float kKnobMaxAngle = 135.f;

float KnobAngle(double normalized)
{
  return kKnobMinAngle + static_cast<float>(normalized) * (kKnobMaxAngle - kKnobMinAngle);
}

/** Point on a circle; angles in degrees clockwise from 12 o'clock. */
void PolarPoint(float cx, float cy, float r, float angleDeg, float& x, float& y)
{
  const float a = angleDeg * kPi / 180.f;
  x = cx + r * std::sin(a);
  y = cy - r * std::cos(a);
}

float ToDb(float linear) { return linear > 1e-9f ? 20.f * std::log10(linear) : -180.f; }

IStrokeOptions RoundStroke()
{
  IStrokeOptions o;
  o.mCapOption = ELineCap::Round;
  o.mJoinOption = ELineJoin::Round;
  return o;
}

void DrawPanel(IGraphics& g, const IRECT& r)
{
  g.FillRoundRect(color::kPanel, r, 8.f);
  g.DrawRoundRect(color::kStroke, r.GetPadded(-0.5f), 8.f, nullptr, 1.f);
}

void DrawPanelTitle(IGraphics& g, const IRECT& panel, const char* title)
{
  DrawTracked(g, Text(10.5f, color::kTextDim, font::kSemiBold, EAlign::Near), title,
              IRECT(panel.L + 12.f, panel.T + 8.f, panel.R - 12.f, panel.T + 22.f), 1.6f);
}

/** Format a dB value with a real minus sign and no "-0.0". */
void FormatDb(char* buf, size_t size, float db, int decimals, bool forceSign)
{
  if (std::fabs(db) < 0.5f * std::pow(10.f, static_cast<float>(-decimals)))
    db = 0.f;
  char number[32];
  std::snprintf(number, sizeof number, forceSign ? "%+.*f" : "%.*f", decimals, std::fabs(db));
  if (db < 0.f)
    std::snprintf(buf, size, "\xE2\x88\x92%s", forceSign ? number + 1 : number); // U+2212
  else
    std::snprintf(buf, size, "%s", number);
}

} // namespace

void SetValueWithGesture(IControl& control, double normalizedValue)
{
  const int idx = control.GetParamIdx();
  if (idx < 0)
    return;
  auto* delegate = control.GetDelegate();
  delegate->BeginInformHostOfParamChangeFromUI(idx);
  control.SetValue(std::clamp(normalizedValue, 0.0, 1.0));
  control.SetDirty(true);
  delegate->EndInformHostOfParamChangeFromUI(idx);
}

// ---------------------------------------------------------------------------
// ParamKnob

ParamKnob::ParamKnob(const IRECT& bounds, int paramIdx, const char* label, double originDb)
: IKnobControlBase(bounds, paramIdx, EDirection::Vertical, 2.5)
, mLabel(label)
, mOriginDb(originDb)
{
  mText = IText(17.f, color::kText, font::kSemiBold, EAlign::Center, EVAlign::Middle, 0.f, color::kPanelRaised, color::kText);
  mHideCursorOnDrag = true;
  SetTooltip("Drag to adjust \xE2\x80\xA2 Shift or Ctrl/Cmd for fine control \xE2\x80\xA2 double-click to reset \xE2\x80\xA2 click the value to type");
}

void ParamKnob::OnResize()
{
  mLabelRect = mRECT.GetFromTop(22.f);
  mValueRect = mRECT.GetFromBottom(30.f).GetCentredInside(std::min(mRECT.W(), 132.f), 26.f);
  const IRECT area = mRECT.GetReducedFromTop(26.f).GetReducedFromBottom(32.f);
  const float d = std::min(area.W(), area.H());
  mKnobRect = area.GetCentredInside(d, d);
  SetDirty(false);
}

void ParamKnob::Draw(IGraphics& g)
{
  const IParam* pParam = GetParam();
  if (!pParam)
    return;

  const bool active = mDragging || mMouseIsOver;
  const float cx = mKnobRect.MW();
  const float cy = mKnobRect.MH();
  const float radius = mKnobRect.W() * 0.5f - 6.f;
  const float trackR = radius;
  const double value = GetValue();
  const double origin = pParam->ToNormalized(mOriginDb);

  // Label
  DrawTracked(g, Text(12.5f, active ? color::kText : color::kTextDim, font::kSemiBold), mLabel.c_str(), mLabelRect, 2.2f);

  // Scale ticks just outside the track
  for (int i = 0; i <= 12; ++i)
  {
    const float a = KnobAngle(i / 12.0);
    float x1, y1, x2, y2;
    PolarPoint(cx, cy, trackR + 5.f, a, x1, y1);
    PolarPoint(cx, cy, trackR + (i % 6 == 0 ? 9.f : 7.f), a, x2, y2);
    g.DrawLine(i % 6 == 0 ? color::kStrokeStrong : color::kStroke, x1, y1, x2, y2, nullptr, 1.f);
  }

  // Track and value arc
  g.PathClear();
  g.PathArc(cx, cy, trackR, kKnobMinAngle, kKnobMaxAngle);
  g.PathStroke(active ? color::kStrokeStrong : color::kStroke, 4.f, RoundStroke());

  const float a0 = KnobAngle(origin);
  const float a1 = KnobAngle(value);
  if (std::fabs(a1 - a0) > 0.5f)
  {
    g.PathClear();
    g.PathArc(cx, cy, trackR, std::min(a0, a1), std::max(a0, a1));
    g.PathStroke(color::kAccent, 4.f, RoundStroke());
  }

  // Origin marker
  {
    float x1, y1, x2, y2;
    PolarPoint(cx, cy, trackR - 4.f, a0, x1, y1);
    PolarPoint(cx, cy, trackR + 4.f, a0, x2, y2);
    g.DrawLine(color::kTextFaint, x1, y1, x2, y2, nullptr, 1.5f);
  }

  // Body
  const float bodyR = radius * 0.72f;
  g.FillCircle(IColor(90, 0, 0, 0), cx, cy + 2.f, bodyR + 1.f);
  g.PathClear();
  g.PathCircle(cx, cy, bodyR);
  g.PathFill(IPattern::CreateLinearGradient(cx, cy - bodyR, cx, cy + bodyR,
                                            { IColorStop(IColor(255, 46, 50, 56), 0.f), IColorStop(IColor(255, 26, 28, 32), 1.f) }));
  g.PathClear();
  g.PathCircle(cx, cy, bodyR - 0.5f);
  g.PathStroke(IPattern::CreateLinearGradient(cx, cy - bodyR, cx, cy + bodyR,
                                              { IColorStop(IColor(255, 78, 84, 92), 0.f), IColorStop(IColor(255, 30, 33, 37), 1.f) }),
               1.f);

  // Pointer
  float px1, py1, px2, py2;
  PolarPoint(cx, cy, bodyR * 0.30f, a1, px1, py1);
  PolarPoint(cx, cy, bodyR * 0.86f, a1, px2, py2);
  g.DrawLine(active ? color::kAccentHot : color::kText, px1, py1, px2, py2, nullptr, 3.f);

  // Value readout: number and unit
  WDL_String display;
  pParam->GetDisplay(display);
  const char* raw = display.Get();
  char number[48];
  if (raw[0] == '-')
    std::snprintf(number, sizeof number, "\xE2\x88\x92%s", raw + 1);
  else
    std::snprintf(number, sizeof number, "%s", raw);

  const IText numText = Text(18.f, mDragging ? color::kAccentHot : color::kText, font::kSemiBold, EAlign::Near);
  const IText unitText = Text(12.f, color::kTextDim, font::kMedium, EAlign::Near);
  IRECT tmp;
  const float wNum = g.MeasureText(numText, number, tmp);
  const float wUnit = g.MeasureText(unitText, pParam->GetLabel(), tmp);
  const float total = wNum + 4.f + wUnit;
  const float x0 = mValueRect.MW() - total * 0.5f;

  if (mMouseIsOver && !mDragging)
    g.FillRoundRect(color::kPanelRaised, mValueRect, 5.f);
  g.DrawText(numText, number, IRECT(x0, mValueRect.T, x0 + wNum + 1.f, mValueRect.B));
  g.DrawText(unitText, pParam->GetLabel(), IRECT(x0 + wNum + 4.f, mValueRect.T + 1.5f, x0 + total + 1.f, mValueRect.B + 1.5f));
}

void ParamKnob::OnMouseDown(float x, float y, const IMouseMod& mod)
{
  if (!mod.R && mValueRect.Contains(x, y))
  {
    PromptUserInput(mValueRect);
    return;
  }
  mDragging = true;
  IKnobControlBase::OnMouseDown(x, y, mod);
}

void ParamKnob::OnMouseUp(float x, float y, const IMouseMod& mod)
{
  mDragging = false;
  IKnobControlBase::OnMouseUp(x, y, mod);
}

void ParamKnob::OnMouseDblClick(float, float, const IMouseMod&)
{
  if (const IParam* pParam = GetParam())
    SetValueWithGesture(*this, pParam->GetDefault(true));
}

void ParamKnob::OnMouseWheel(float, float, const IMouseMod& mod, float d)
{
  const double step = IsFineControl(mod, true) ? 0.001 : 0.01;
  SetValueWithGesture(*this, GetValue() + step * d);
}

void ParamKnob::SetValueFromUserInput(double value, int)
{
  if (value != GetValue())
    SetValueWithGesture(*this, value);
}

// ---------------------------------------------------------------------------
// PillToggle

PillToggle::PillToggle(const IRECT& bounds, int paramIdx, const char* label, const IColor& onColor)
: IControl(bounds, paramIdx)
, mLabel(label)
, mOnColor(onColor)
{
}

void PillToggle::Draw(IGraphics& g)
{
  const bool on = GetValue() > 0.5;
  const IRECT r = mRECT.GetPadded(-0.5f);
  if (on)
  {
    g.FillRoundRect(mOnColor.WithOpacity(0.14f), r, 6.f);
    g.DrawRoundRect(mOnColor.WithOpacity(0.85f), r, 6.f, nullptr, 1.f);
  }
  else
  {
    g.FillRoundRect(mMouseIsOver ? color::kPanelRaised : color::kPanel, r, 6.f);
    g.DrawRoundRect(mMouseIsOver ? color::kStrokeStrong : color::kStroke, r, 6.f, nullptr, 1.f);
  }

  // indicator dot + label
  const IText text = Text(11.5f, on ? mOnColor : (mMouseIsOver ? color::kText : color::kTextDim), font::kSemiBold, EAlign::Near);
  const float wLabel = MeasureTracked(g, text, mLabel.c_str(), 1.4f);
  const float total = 6.f + 7.f + wLabel;
  const float x0 = r.MW() - total * 0.5f;
  g.FillCircle(on ? mOnColor : color::kStrokeStrong, x0 + 3.f, r.MH(), 3.f);
  DrawTracked(g, text, mLabel.c_str(), IRECT(x0 + 13.f, r.T, r.R, r.B), 1.4f);
}

void PillToggle::OnMouseDown(float, float, const IMouseMod& mod)
{
  if (mod.R)
    return;
  SetValue(GetValue() > 0.5 ? 0.0 : 1.0);
  SetDirty(true);
}

// ---------------------------------------------------------------------------
// SegmentedControl

SegmentedControl::SegmentedControl(const IRECT& bounds, int paramIdx, const char* label, float labelWidth)
: IControl(bounds, paramIdx)
, mLabel(label)
, mLabelWidth(labelWidth)
{
}

void SegmentedControl::OnResize()
{
  mBox = mRECT.GetReducedFromLeft(mLabelWidth);
  SetDirty(false);
}

IRECT SegmentedControl::SegmentRect(int i) const
{
  const IParam* pParam = GetParam();
  const int n = pParam ? pParam->NDisplayTexts() : 1;
  return mBox.SubRectHorizontal(n, i);
}

int SegmentedControl::SegmentAt(float x, float y) const
{
  const IParam* pParam = GetParam();
  if (!pParam || !mBox.Contains(x, y))
    return -1;
  for (int i = 0; i < pParam->NDisplayTexts(); ++i)
    if (SegmentRect(i).Contains(x, y))
      return i;
  return -1;
}

void SegmentedControl::Draw(IGraphics& g)
{
  const IParam* pParam = GetParam();
  if (!pParam)
    return;
  const int n = pParam->NDisplayTexts();
  const int selected = static_cast<int>(std::round(pParam->FromNormalized(GetValue())));

  DrawTracked(g, Text(10.5f, color::kTextDim, font::kSemiBold, EAlign::Far), mLabel.c_str(),
              mRECT.GetFromLeft(mLabelWidth - 10.f), 1.6f);

  const IRECT box = mBox.GetPadded(-0.5f);
  g.FillRoundRect(color::kPanel, box, 6.f);
  for (int i = 0; i < n; ++i)
  {
    const IRECT seg = SegmentRect(i).GetPadded(-2.5f);
    const bool isSel = i == selected;
    if (isSel)
    {
      g.FillRoundRect(color::kPanelRaised, seg, 4.f);
      g.DrawRoundRect(color::kStrokeStrong, seg, 4.f, nullptr, 1.f);
    }
    else if (i == mHover)
    {
      g.FillRoundRect(color::kPanelRaised.WithOpacity(0.6f), seg, 4.f);
    }
    const char* name = pParam->GetDisplayText(i);
    const bool isOff = i == 0;
    char label[16];
    if (!isOff && name)
    {
      // "2x" -> "2×"
      std::snprintf(label, sizeof label, "%.*s\xC3\x97", static_cast<int>(std::strlen(name)) - 1, name);
      name = label;
    }
    const IColor c = isSel ? (isOff ? color::kText : color::kAccent) : (i == mHover ? color::kText : color::kTextDim);
    DrawTracked(g, Text(isOff ? 10.5f : 12.5f, c, font::kSemiBold), isOff ? "OFF" : name, seg, isOff ? 1.2f : 0.5f);
  }
  g.DrawRoundRect(color::kStroke, box, 6.f, nullptr, 1.f);
}

void SegmentedControl::OnMouseDown(float x, float y, const IMouseMod& mod)
{
  if (mod.R)
    return;
  const int seg = SegmentAt(x, y);
  if (seg >= 0)
  {
    SetValue(GetParam()->ToNormalized(seg));
    SetDirty(true);
  }
}

void SegmentedControl::OnMouseOver(float x, float y, const IMouseMod& mod)
{
  const int seg = SegmentAt(x, y);
  if (seg != mHover)
  {
    mHover = seg;
    SetDirty(false);
  }
  IControl::OnMouseOver(x, y, mod);
}

void SegmentedControl::OnMouseOut()
{
  mHover = -1;
  IControl::OnMouseOut();
}

// ---------------------------------------------------------------------------
// ClipMeter

namespace
{
constexpr float kMeterRangeDb = 24.f;
constexpr float kMeterFallDbPerSec = 30.f;
constexpr float kHoldSeconds = 1.5f;
constexpr float kHoldFallDbPerSec = 12.f;
} // namespace

ClipMeter::ClipMeter(const IRECT& bounds)
: IControl(bounds)
{
  SetTooltip("Clipping depth: how far the driven signal exceeds the ceiling. Click the readout to reset.");
}

void ClipMeter::OnResize()
{
  const IRECT inner = mRECT.GetReducedFromTop(34.f).GetReducedFromBottom(62.f);
  mBar = IRECT(mRECT.R - 26.f, inner.T, mRECT.R - 12.f, inner.B);
  mReadout = mRECT.GetFromBottom(56.f).GetPadded(-4.f);
  SetDirty(false);
}

float ClipMeter::Position(float db)
{
  return std::sqrt(std::clamp(db / kMeterRangeDb, 0.f, 1.f));
}

void ClipMeter::Update(float overshootDb, float dt)
{
  const float db = std::max(overshootDb, 0.f);
  const float oldLevel = mLevel, oldHold = mHold, oldMax = mMax;
  const bool oldClipping = mClipping;

  mClipping = overshootDb > 0.01f;
  mLevel = db >= mLevel ? db : std::max(db, mLevel - kMeterFallDbPerSec * dt);
  if (db >= mHold)
  {
    mHold = db;
    mHoldTime = 0.f;
  }
  else
  {
    mHoldTime += dt;
    if (mHoldTime > kHoldSeconds)
      mHold = std::max(db, mHold - kHoldFallDbPerSec * dt);
  }
  mMax = std::max(mMax, db);

  if (std::fabs(mLevel - oldLevel) > 0.01f || std::fabs(mHold - oldHold) > 0.01f || mMax != oldMax || oldClipping != mClipping)
    SetDirty(false);
}

void ClipMeter::ResetMax()
{
  mMax = 0.f;
  mHold = mLevel;
  SetDirty(false);
}

void ClipMeter::OnMouseDown(float, float, const IMouseMod&)
{
  ResetMax();
}

void ClipMeter::Draw(IGraphics& g)
{
  DrawPanel(g, mRECT);

  // Title with a clip indicator
  const IRECT title = mRECT.GetFromTop(30.f).GetVShifted(1.f);
  const IText titleText = Text(10.5f, color::kTextDim, font::kSemiBold);
  const float titleW = MeasureTracked(g, titleText, "CLIP", 1.6f);
  const float ledX = title.MW() - (titleW + 9.f) * 0.5f + 2.5f;
  DrawTracked(g, titleText, "CLIP", title.GetHShifted(4.5f), 1.6f);
  if (mClipping)
    g.FillCircle(color::kAccent.WithOpacity(0.25f), ledX, title.MH(), 5.5f);
  g.FillCircle(mClipping ? color::kAccent : color::kStroke, ledX, title.MH(), 2.5f);

  // Scale
  const float ticks[] = { 0.f, 1.f, 3.f, 6.f, 12.f, 24.f };
  for (float t : ticks)
  {
    const float y = mBar.T + Position(t) * mBar.H();
    g.DrawLine(color::kStrokeStrong, mBar.L - 5.f, y, mBar.L - 2.f, y, nullptr, 1.f);
    char buf[8];
    std::snprintf(buf, sizeof buf, "%d", static_cast<int>(t));
    g.DrawText(Text(10.f, color::kTextFaint, font::kMedium, EAlign::Far), buf, IRECT(mRECT.L + 4.f, y - 6.f, mBar.L - 8.f, y + 6.f));
  }

  // Bar
  g.FillRoundRect(color::kBackground, mBar.GetPadded(1.f), 3.f);
  const float fill = Position(mLevel) * mBar.H();
  if (fill > 0.5f)
  {
    const IRECT f(mBar.L, mBar.T, mBar.R, mBar.T + fill);
    g.PathClear();
    g.PathRoundRect(f, 2.f);
    g.PathFill(IPattern::CreateLinearGradient(mBar.L, mBar.T, mBar.L, mBar.B,
                                              { IColorStop(color::kAccentHot, 0.f), IColorStop(color::kAccent, 0.45f),
                                                IColorStop(color::kAccentDeep, 1.f) }));
  }
  if (mHold > 0.05f)
  {
    const float y = mBar.T + Position(mHold) * mBar.H();
    g.DrawLine(color::kAccentHot, mBar.L - 1.f, y, mBar.R + 1.f, y, nullptr, 2.f);
  }
  g.DrawRoundRect(color::kStroke, mBar.GetPadded(1.f), 3.f, nullptr, 1.f);

  // Max readout (click to reset)
  const bool any = mMax >= 0.05f;
  if (mMouseIsOver)
    g.FillRoundRect(color::kPanelRaised, mReadout, 5.f);
  DrawTracked(g, Text(9.5f, color::kTextFaint, font::kSemiBold), "MAX", mReadout.GetFromTop(16.f).GetVShifted(3.f), 1.4f);
  char buf[16];
  std::snprintf(buf, sizeof buf, "%.1f", any ? mMax : 0.f);
  g.DrawText(Text(19.f, any ? color::kAccent : color::kTextFaint, font::kSemiBold), buf, mReadout.GetReducedFromTop(15.f).GetReducedFromBottom(13.f));
  g.DrawText(Text(10.f, color::kTextFaint, font::kMedium), "dB", mReadout.GetFromBottom(14.f));
}

// ---------------------------------------------------------------------------
// ClipHistory

ClipHistory::ClipHistory(const IRECT& bounds)
: IControl(bounds)
{
  mIgnoreMouse = true;
}

void ClipHistory::OnResize()
{
  mPlot = mRECT.GetReducedFromLeft(12.f).GetReducedFromRight(40.f).GetReducedFromTop(30.f).GetReducedFromBottom(12.f);
  SetDirty(false);
}

float ClipHistory::DbToY(float db) const
{
  const float t = (std::clamp(db, kBottomDb - 2.f, kTopDb + 2.f) - kBottomDb) / (kTopDb - kBottomDb);
  return mPlot.B - t * mPlot.H();
}

void ClipHistory::Push(const dsp::HistoryPoint& p)
{
  mPoints[mWrite] = p;
  mWrite = (mWrite + 1) % kCapacity;
  mCount = std::min(mCount + 1, kCapacity);
  SetDirty(false);
}

void ClipHistory::SetCeilingDb(float db)
{
  if (db != mCeilingDb)
  {
    mCeilingDb = db;
    SetDirty(false);
  }
}

void ClipHistory::Draw(IGraphics& g)
{
  DrawPanel(g, mRECT);
  DrawPanelTitle(g, mRECT, "CLIP HISTORY");

  // Legend
  {
    const IText t = Text(10.f, color::kTextDim, font::kMedium, EAlign::Near);
    float x = mRECT.R - 170.f;
    const float y = mRECT.T + 15.f;
    g.FillRoundRect(color::kSteel.WithOpacity(0.6f), IRECT(x, y - 4.f, x + 8.f, y + 4.f), 1.5f);
    DrawTracked(g, t, "DRIVEN SIGNAL", IRECT(x + 13.f, y - 7.f, x + 100.f, y + 7.f), 0.8f);
    x += 102.f;
    g.FillRoundRect(color::kAccent, IRECT(x, y - 4.f, x + 8.f, y + 4.f), 1.5f);
    DrawTracked(g, t, "CLIPPED", IRECT(x + 13.f, y - 7.f, x + 70.f, y + 7.f), 0.8f);
  }

  // Grid and scale
  const float ceilingY = DbToY(mCeilingDb);
  for (float db = kTopDb - 12.f; db > kBottomDb; db -= 12.f)
  {
    const float y = DbToY(db);
    g.DrawLine(db == 0.f ? color::kStrokeStrong : color::kGrid, mPlot.L, y, mPlot.R, y, nullptr, 1.f);
    if (std::fabs(y - ceilingY) > 11.f)
    {
      char buf[16];
      FormatDb(buf, sizeof buf, db, 0, true);
      g.DrawText(Text(10.f, color::kTextFaint, font::kMedium, EAlign::Near), buf, IRECT(mPlot.R + 7.f, y - 6.f, mRECT.R - 4.f, y + 6.f));
    }
  }

  const int n = std::min(mCount, kVisible);
  const float dx = mPlot.W() / static_cast<float>(kVisible - 1);
  auto pointAt = [&](int i) -> const dsp::HistoryPoint& {
    return mPoints[(mWrite - n + i + kCapacity) % kCapacity];
  };
  auto xAt = [&](int i) { return mPlot.R - static_cast<float>(n - 1 - i) * dx; };

  g.PathClipRegion(mPlot.GetPadded(1.f));
  if (n > 1)
  {
    // Driven signal envelope
    g.PathClear();
    g.PathMoveTo(xAt(0), mPlot.B);
    for (int i = 0; i < n; ++i)
      g.PathLineTo(xAt(i), DbToY(ToDb(pointAt(i).drivenPeak)));
    g.PathLineTo(xAt(n - 1), mPlot.B);
    g.PathClose();
    g.PathFill(IPattern::CreateLinearGradient(0.f, mPlot.T, 0.f, mPlot.B,
                                              { IColorStop(color::kSteel.WithOpacity(0.42f), 0.f), IColorStop(color::kSteel.WithOpacity(0.06f), 1.f) }));

    g.PathClear();
    for (int i = 0; i < n; ++i)
    {
      const float y = DbToY(ToDb(pointAt(i).drivenPeak));
      if (i == 0)
        g.PathMoveTo(xAt(i), y);
      else
        g.PathLineTo(xAt(i), y);
    }
    g.PathStroke(color::kSteel.WithOpacity(0.9f), 1.2f, RoundStroke());

    // Clipped region: between the ceiling and the driven peak
    g.PathClear();
    for (int i = 0; i < n; ++i)
    {
      const auto& p = pointAt(i);
      const float y = DbToY(std::max(ToDb(p.drivenPeak), ToDb(p.ceiling)));
      if (i == 0)
        g.PathMoveTo(xAt(i), y);
      else
        g.PathLineTo(xAt(i), y);
    }
    for (int i = n - 1; i >= 0; --i)
      g.PathLineTo(xAt(i), DbToY(ToDb(pointAt(i).ceiling)));
    g.PathClose();
    g.PathFill(IPattern::CreateLinearGradient(0.f, mPlot.T, 0.f, ceilingY,
                                              { IColorStop(color::kAccentHot, 0.f), IColorStop(color::kAccent, 0.7f) }));

    // Ceiling trace
    g.PathClear();
    for (int i = 0; i < n; ++i)
    {
      const float y = DbToY(ToDb(pointAt(i).ceiling));
      if (i == 0)
        g.PathMoveTo(xAt(i), y);
      else
        g.PathLineTo(xAt(i), y);
    }
    g.PathStroke(color::kAccent.WithOpacity(0.9f), 1.f);
    if (n < kVisible)
      g.DrawLine(color::kAccent.WithOpacity(0.5f), mPlot.L, ceilingY, xAt(0), ceilingY, nullptr, 1.f);
  }
  else
  {
    g.DrawLine(color::kAccent.WithOpacity(0.7f), mPlot.L, ceilingY, mPlot.R, ceilingY, nullptr, 1.f);
  }
  g.PathClipRegion();

  // Ceiling marker on the scale
  char buf[16];
  FormatDb(buf, sizeof buf, mCeilingDb, 1, false);
  g.PathClear();
  g.PathMoveTo(mPlot.R + 1.f, ceilingY);
  g.PathLineTo(mPlot.R + 6.f, ceilingY - 4.f);
  g.PathLineTo(mPlot.R + 6.f, ceilingY + 4.f);
  g.PathClose();
  g.PathFill(color::kAccent);
  g.DrawText(Text(10.f, color::kAccent, font::kSemiBold, EAlign::Near), buf, IRECT(mPlot.R + 8.f, ceilingY - 6.f, mRECT.R - 2.f, ceilingY + 6.f));
}

// ---------------------------------------------------------------------------
// TransferCurve

TransferCurve::TransferCurve(const IRECT& bounds)
: IControl(bounds)
{
  mIgnoreMouse = true;
}

void TransferCurve::OnResize()
{
  const IRECT area = mRECT.GetReducedFromTop(30.f).GetPadded(-14.f);
  const float side = std::min(area.W(), area.H());
  mPlot = area.GetCentredInside(side, side);
  SetDirty(false);
}

float TransferCurve::Shape(float x) const
{
  return std::clamp(x * mDrive, -mCeiling, mCeiling) * mOutput;
}

void TransferCurve::SetCurve(float drive, float ceiling, float output)
{
  if (drive != mDrive || ceiling != mCeiling || output != mOutput)
  {
    mDrive = drive;
    mCeiling = ceiling;
    mOutput = output;
    SetDirty(false);
  }
}

void TransferCurve::SetInputPeak(float peak)
{
  if (std::fabs(peak - mPeak) > 1e-4f)
  {
    mPeak = peak;
    SetDirty(false);
  }
}

void TransferCurve::Draw(IGraphics& g)
{
  DrawPanel(g, mRECT);
  DrawPanelTitle(g, mRECT, "TRANSFER");

  auto X = [&](float x) { return mPlot.L + (x + 1.f) * 0.5f * mPlot.W(); };
  auto Y = [&](float y) { return mPlot.B - (y + 1.f) * 0.5f * mPlot.H(); };

  // Grid
  g.FillRect(color::kBackground.WithOpacity(0.35f), mPlot);
  for (float v : { -0.5f, 0.5f })
  {
    g.DrawLine(color::kGrid, X(v), mPlot.T, X(v), mPlot.B, nullptr, 1.f);
    g.DrawLine(color::kGrid, mPlot.L, Y(v), mPlot.R, Y(v), nullptr, 1.f);
  }
  g.DrawLine(color::kStroke, X(0.f), mPlot.T, X(0.f), mPlot.B, nullptr, 1.f);
  g.DrawLine(color::kStroke, mPlot.L, Y(0.f), mPlot.R, Y(0.f), nullptr, 1.f);
  g.DrawDottedLine(color::kStrokeStrong, X(-1.f), Y(-1.f), X(1.f), Y(1.f), nullptr, 1.f, 3.f);
  g.DrawRect(color::kStroke, mPlot, nullptr, 1.f);

  g.DrawText(Text(9.5f, color::kTextFaint, font::kSemiBold, EAlign::Far), "IN", IRECT(mPlot.R - 30.f, Y(0.f) + 2.f, mPlot.R - 3.f, Y(0.f) + 14.f));
  g.DrawText(Text(9.5f, color::kTextFaint, font::kSemiBold, EAlign::Near), "OUT", IRECT(X(0.f) + 4.f, mPlot.T + 2.f, X(0.f) + 40.f, mPlot.T + 14.f));

  g.PathClipRegion(mPlot);

  // Ceiling guides
  const float level = mCeiling * mOutput;
  g.DrawDottedLine(color::kAccent.WithOpacity(0.45f), mPlot.L, Y(level), mPlot.R, Y(level), nullptr, 1.f, 3.f);
  g.DrawDottedLine(color::kAccent.WithOpacity(0.45f), mPlot.L, Y(-level), mPlot.R, Y(-level), nullptr, 1.f, 3.f);

  // Curve: linear section, then the flat clipped sections in the accent colour
  const float knee = mCeiling / mDrive;
  const float k = std::min(knee, 1.f);
  g.PathClear();
  g.PathMoveTo(X(-k), Y(Shape(-k)));
  g.PathLineTo(X(k), Y(Shape(k)));
  g.PathStroke(color::kSteelBright, 2.f, RoundStroke());
  if (knee < 1.f)
  {
    for (float sgn : { -1.f, 1.f })
    {
      g.PathClear();
      g.PathMoveTo(X(sgn * knee), Y(Shape(sgn * knee)));
      g.PathLineTo(X(sgn), Y(Shape(sgn)));
      g.PathStroke(color::kAccent, 2.5f, RoundStroke());
      g.FillCircle(color::kBackground, X(sgn * knee), Y(Shape(sgn * knee)), 3.5f);
      g.DrawCircle(color::kAccent, X(sgn * knee), Y(Shape(sgn * knee)), 3.5f, nullptr, 1.5f);
    }
  }

  // Live input marker
  if (mPeak > 1e-3f)
  {
    const float x = std::min(mPeak, 1.f);
    const bool clipping = mPeak * mDrive > mCeiling;
    const float px = X(x), py = Y(Shape(x));
    if (clipping)
      g.FillCircle(color::kAccent.WithOpacity(0.22f), px, py, 9.f);
    g.FillCircle(clipping ? color::kAccentHot : color::kSteelBright, px, py, 4.f);
  }
  g.PathClipRegion();
}

// ---------------------------------------------------------------------------
// Header

Header::Header(const IRECT& bounds)
: IControl(bounds)
{
  mIgnoreMouse = true;
}

void Header::Draw(IGraphics& g)
{
  g.DrawLine(color::kStroke, mRECT.L, mRECT.B - 0.5f, mRECT.R, mRECT.B - 0.5f, nullptr, 1.f);

  // Logo mark: a sine wave cut flat by the ceiling
  const IRECT mark = IRECT(mRECT.L + 24.f, mRECT.MH() - 15.f, mRECT.L + 54.f, mRECT.MH() + 15.f);
  g.FillRoundRect(color::kPanelRaised, mark, 7.f);
  g.DrawRoundRect(color::kStrokeStrong, mark.GetPadded(-0.5f), 7.f, nullptr, 1.f);
  {
    const float cx = mark.MW(), cy = mark.MH();
    const float w = 20.f, amp = 10.f, clip = 5.5f;
    const int steps = 48;
    g.PathClear();
    for (int i = 0; i <= steps; ++i)
    {
      const float t = static_cast<float>(i) / steps;
      const float x = cx - w * 0.5f + t * w;
      const float y = cy - std::clamp(amp * std::sin(t * 2.f * kPi), -clip, clip);
      if (i == 0)
        g.PathMoveTo(x, y);
      else
        g.PathLineTo(x, y);
    }
    g.PathStroke(color::kSteelBright, 1.8f, RoundStroke());
    // flat tops in the accent colour
    const float tClip = std::asin(clip / amp) / (2.f * kPi);
    for (float start : { tClip, 0.5f + tClip })
    {
      const float x1 = cx - w * 0.5f + start * w;
      const float x2 = cx - w * 0.5f + (start + 0.5f - 2.f * tClip) * w;
      const float y = start < 0.5f ? cy - clip : cy + clip;
      g.DrawLine(color::kAccent, x1, y, x2, y, nullptr, 2.4f);
    }
  }

  // Wordmark
  const IText iron = Text(22.f, color::kText, font::kBold, EAlign::Near);
  const IText clipText = Text(22.f, color::kAccent, font::kBold, EAlign::Near);
  const float x0 = mark.R + 14.f;
  const float wIron = MeasureTracked(g, iron, "IRON", 2.4f);
  const IRECT line(x0, mRECT.T, mRECT.R, mRECT.B);
  DrawTracked(g, iron, "IRON", line, 2.4f);
  DrawTracked(g, clipText, "CLIP", line.GetReducedFromLeft(wIron + 2.4f), 2.4f);
  const float wClip = MeasureTracked(g, clipText, "CLIP", 2.4f);

  const float sepX = x0 + wIron + 2.4f + wClip + 14.f;
  g.DrawLine(color::kStroke, sepX, mRECT.MH() - 9.f, sepX, mRECT.MH() + 9.f, nullptr, 1.f);
  DrawTracked(g, Text(10.5f, color::kTextFaint, font::kSemiBold, EAlign::Near), "HARD CLIPPER",
              IRECT(sepX + 14.f, mRECT.T, sepX + 200.f, mRECT.B), 1.8f);
}

// ---------------------------------------------------------------------------
// Footer

Footer::Footer(const IRECT& bounds, const char* versionText)
: IControl(bounds)
, mVersion(versionText)
{
  mIgnoreMouse = true;
}

void Footer::SetStatus(int latencySamples, double sampleRate, bool autoGainOn, float autoGainDb)
{
  const float roundedGain = std::round(autoGainDb * 10.f) / 10.f;
  if (latencySamples != mLatency || sampleRate != mSampleRate || autoGainOn != mAutoGainOn || roundedGain != mAutoGainDb)
  {
    mLatency = latencySamples;
    mSampleRate = sampleRate;
    mAutoGainOn = autoGainOn;
    mAutoGainDb = roundedGain;
    SetDirty(false);
  }
}

void Footer::Draw(IGraphics& g)
{
  const IText faint = Text(10.f, color::kTextFaint, font::kSemiBold, EAlign::Near);
  DrawTracked(g, faint, mVersion.c_str(), mRECT, 1.2f);

  char latency[64];
  if (mLatency == 0)
    std::snprintf(latency, sizeof latency, "ZERO LATENCY");
  else
    std::snprintf(latency, sizeof latency, "LATENCY %d SAMPLES \xC2\xB7 %.2f MS", mLatency, 1000.0 * mLatency / std::max(mSampleRate, 1.0));

  IText right = faint;
  right.mAlign = EAlign::Far;
  if (mAutoGainOn)
  {
    char gain[48], db[16];
    FormatDb(db, sizeof db, mAutoGainDb, 1, true);
    std::snprintf(gain, sizeof gain, "AUTO GAIN %s DB", db);
    IText ag = right;
    ag.mFGColor = color::kAccent;
    const float w = MeasureTracked(g, ag, gain, 1.2f);
    DrawTracked(g, ag, gain, mRECT, 1.2f);
    DrawTracked(g, right, latency, mRECT.GetReducedFromRight(w + 24.f), 1.2f);
  }
  else
  {
    DrawTracked(g, right, latency, mRECT, 1.2f);
  }
}

} // namespace ironclip::ui
