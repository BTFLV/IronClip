#pragma once

#include "IGraphics.h"

namespace ironclip::ui
{

using iplug::igraphics::EAlign;
using iplug::igraphics::EVAlign;
using iplug::igraphics::IColor;
using iplug::igraphics::IGraphics;
using iplug::igraphics::IRECT;
using iplug::igraphics::IText;

namespace color
{
// Surfaces
inline const IColor kBackground { 255, 15, 16, 18 };
inline const IColor kPanel { 255, 22, 24, 27 };
inline const IColor kPanelRaised { 255, 30, 33, 37 };
inline const IColor kStroke { 255, 43, 47, 53 };
inline const IColor kStrokeStrong { 255, 64, 70, 78 };
inline const IColor kGrid { 255, 34, 37, 42 };

// Text
inline const IColor kText { 255, 232, 234, 237 };
inline const IColor kTextDim { 255, 140, 147, 156 };
inline const IColor kTextFaint { 255, 92, 99, 108 };

// Signal and accents
inline const IColor kSteel { 255, 132, 144, 158 };
inline const IColor kSteelBright { 255, 200, 209, 219 };
inline const IColor kAccent { 255, 255, 98, 31 };     // molten orange: clipping
inline const IColor kAccentHot { 255, 255, 184, 77 }; // amber highlight
inline const IColor kAccentDeep { 255, 196, 52, 12 };
inline const IColor kBypass { 255, 242, 201, 76 };
} // namespace color

namespace font
{
inline constexpr const char* kMedium = "IronClip-Medium";
inline constexpr const char* kSemiBold = "IronClip-SemiBold";
inline constexpr const char* kBold = "IronClip-Bold";
} // namespace font

inline IText Text(float size, const IColor& c, const char* fontId = font::kSemiBold, EAlign align = EAlign::Center,
                  EVAlign valign = EVAlign::Middle)
{
  return IText(size, c, fontId, align, valign);
}

/** Loads the embedded typefaces into the graphics context. */
void LoadFonts(IGraphics& g);

/** Draws text with extra letter spacing (tracking), aligned within bounds. */
void DrawTracked(IGraphics& g, const IText& text, const char* str, const IRECT& bounds, float tracking);

/** Width of text drawn with DrawTracked. */
float MeasureTracked(IGraphics& g, const IText& text, const char* str, float tracking);

} // namespace ironclip::ui
