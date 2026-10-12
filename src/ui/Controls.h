#pragma once

#include <array>
#include <string>

#include "IControl.h"
#include "Theme.h"
#include "dsp/Telemetry.h"

namespace ironclip::ui
{

using iplug::igraphics::IControl;
using iplug::igraphics::IKnobControlBase;
using iplug::igraphics::IMouseMod;

/** Raises the host gesture around a value change made outside a mouse drag
 *  (double-click reset, scroll wheel, typed value). */
void SetValueWithGesture(IControl& control, double normalizedValue);

/** Large rotary control: label, arc knob, and a clickable value readout for
 *  typed entry. Drag vertically; Shift or Ctrl/Cmd for fine adjustment;
 *  double-click resets. */
class ParamKnob final : public IKnobControlBase
{
public:
  ParamKnob(const IRECT& bounds, int paramIdx, const char* label, double originDb);

  void Draw(IGraphics& g) override;
  void OnResize() override;
  void OnMouseDown(float x, float y, const IMouseMod& mod) override;
  void OnMouseUp(float x, float y, const IMouseMod& mod) override;
  void OnMouseDblClick(float x, float y, const IMouseMod& mod) override;
  void OnMouseWheel(float x, float y, const IMouseMod& mod, float d) override;
  void SetValueFromUserInput(double value, int valIdx = 0) override;

protected:
  IRECT GetKnobDragBounds() override { return mKnobRect; }

private:
  std::string mLabel;
  double mOriginDb;
  IRECT mLabelRect, mKnobRect, mValueRect;
  bool mDragging = false;
};

/** Pill-shaped on/off switch bound to a boolean parameter. */
class PillToggle final : public IControl
{
public:
  PillToggle(const IRECT& bounds, int paramIdx, const char* label, const IColor& onColor);

  void Draw(IGraphics& g) override;
  void OnMouseDown(float x, float y, const IMouseMod& mod) override;
  void OnMouseDblClick(float x, float y, const IMouseMod& mod) override { OnMouseDown(x, y, mod); }

private:
  std::string mLabel;
  IColor mOnColor;
};

/** Labelled segmented selector bound to an enum parameter. */
class SegmentedControl final : public IControl
{
public:
  SegmentedControl(const IRECT& bounds, int paramIdx, const char* label, float labelWidth);

  void Draw(IGraphics& g) override;
  void OnMouseDown(float x, float y, const IMouseMod& mod) override;
  void OnMouseDblClick(float x, float y, const IMouseMod& mod) override { OnMouseDown(x, y, mod); }
  void OnMouseOver(float x, float y, const IMouseMod& mod) override;
  void OnMouseOut() override;
  void OnResize() override;

private:
  int SegmentAt(float x, float y) const;
  IRECT SegmentRect(int i) const;

  std::string mLabel;
  float mLabelWidth;
  IRECT mBox;
  int mHover = -1;
};

/** Vertical clip meter: how far the driven signal exceeds the ceiling, with
 *  peak hold and a resettable maximum readout. */
class ClipMeter final : public IControl
{
public:
  explicit ClipMeter(const IRECT& bounds);

  void Draw(IGraphics& g) override;
  void OnMouseDown(float x, float y, const IMouseMod& mod) override;
  void OnResize() override;

  /** Feeds the overshoot above the ceiling in dB (<= 0: no clipping). */
  void Update(float overshootDb, float dt);
  void ResetMax();

private:
  static float Position(float db);

  IRECT mBar, mReadout;
  float mLevel = 0.f; // displayed bar level, dB
  float mHold = 0.f;  // peak hold marker, dB
  float mHoldTime = 0.f;
  float mMax = 0.f;   // largest overshoot since reset, dB
  bool mClipping = false;
};

/** Scrolling history of the driven signal's peak level against the ceiling;
 *  the area above the ceiling is exactly what the clipper removes. */
class ClipHistory final : public IControl
{
public:
  explicit ClipHistory(const IRECT& bounds);

  void Draw(IGraphics& g) override;
  void OnResize() override;

  void Push(const dsp::HistoryPoint& p);
  void SetCeilingDb(float db);

private:
  static constexpr int kCapacity = 1024;
  static constexpr int kVisible = 400; // slices across the width (~4 s)
  static constexpr float kTopDb = 24.f;
  static constexpr float kBottomDb = -36.f;

  float DbToY(float db) const;

  IRECT mPlot;
  std::array<dsp::HistoryPoint, kCapacity> mPoints {};
  int mWrite = 0;
  int mCount = 0;
  float mCeilingDb = 0.f;
};

/** Static transfer curve of the current settings with a live input marker. */
class TransferCurve final : public IControl
{
public:
  explicit TransferCurve(const IRECT& bounds);

  void Draw(IGraphics& g) override;
  void OnResize() override;

  void SetCurve(float drive, float ceiling, float output);
  void SetInputPeak(float peak);

private:
  float Shape(float x) const;

  IRECT mPlot;
  float mDrive = 1.f, mCeiling = 1.f, mOutput = 1.f;
  float mPeak = 0.f;
};

/** Logo mark and wordmark. */
class Header final : public IControl
{
public:
  explicit Header(const IRECT& bounds);
  void Draw(IGraphics& g) override;
};

/** Version, latency and auto gain readouts. */
class Footer final : public IControl
{
public:
  Footer(const IRECT& bounds, const char* versionText);
  void Draw(IGraphics& g) override;

  void SetStatus(int latencySamples, double sampleRate, bool autoGainOn, float autoGainDb);

private:
  std::string mVersion;
  int mLatency = 0;
  double mSampleRate = 48000.0;
  bool mAutoGainOn = false;
  float mAutoGainDb = 0.f;
};

} // namespace ironclip::ui
