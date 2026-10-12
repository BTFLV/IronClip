#pragma once

class IronClip;

namespace iplug::igraphics
{
class IGraphics;
}

namespace ironclip::ui
{

/** Creates the IronClip editor inside the given graphics context. */
void BuildEditor(iplug::igraphics::IGraphics& g, IronClip& plugin);

} // namespace ironclip::ui
