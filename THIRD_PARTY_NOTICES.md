# Third-party notices

IronClip itself is released under the [MIT License](LICENSE). The plug-in
binaries also contain code and data from the projects below, all under
permissive licenses. Their full license texts are in [`licenses/`](licenses)
and ship with every release package.

| Component | Used for | License | Text |
| --- | --- | --- | --- |
| [iPlug2](https://github.com/iPlug2/iPlug2) | Plug-in framework, IGraphics UI | zlib-style | [iPlug2.txt](licenses/iPlug2.txt) |
| [WDL](https://www.cockos.com/wdl/) (Cockos) | Utilities used by iPlug2 | zlib-style | [WDL.txt](licenses/WDL.txt) |
| [NanoSVG](https://github.com/memononen/nanosvg) | SVG parsing in IGraphics | zlib | [NanoSVG.txt](licenses/NanoSVG.txt) |
| [VST 3 SDK](https://github.com/steinbergmedia/vst3sdk) (Steinberg) | VST3 format support | MIT | [VST3_SDK.txt](licenses/VST3_SDK.txt) |
| [CLAP](https://github.com/free-audio/clap) | CLAP format support | MIT | [CLAP.txt](licenses/CLAP.txt) |
| [clap-helpers](https://github.com/free-audio/clap-helpers) | CLAP format support | MIT | [CLAP-helpers.txt](licenses/CLAP-helpers.txt) |
| [Skia](https://skia.org) | 2D graphics rendering | BSD-3-Clause | [Skia.txt](licenses/Skia.txt) |
| [libpng](https://github.com/pnggroup/libpng) | PNG codec (via Skia) | libpng License | [libpng.txt](licenses/libpng.txt) |
| [zlib](https://zlib.net) | Compression (via Skia) | zlib | [zlib.txt](licenses/zlib.txt) |
| [Expat](https://github.com/libexpat/libexpat) | XML parsing (via Skia) | MIT | [Expat.txt](licenses/Expat.txt) |
| [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo) | JPEG codec (via Skia) | IJG, BSD-3-Clause, zlib | [libjpeg-turbo.md](licenses/libjpeg-turbo.md), [README.ijg](licenses/libjpeg-turbo-README.ijg) |
| [Wuffs](https://github.com/google/wuffs) | Image decoding (via Skia) | Apache-2.0 / MIT | [Wuffs.txt](licenses/Wuffs.txt) |
| [Barlow](https://github.com/jpt/barlow) (Barlow Semi Condensed) | Interface typeface | SIL Open Font License 1.1 | [Barlow-OFL.txt](licenses/Barlow-OFL.txt) |

The libjpeg-turbo license asks for the following acknowledgement: this
software is based in part on the work of the Independent JPEG Group.

VST is a registered trademark of Steinberg Media Technologies GmbH. Audio Unit
is a trademark of Apple Inc. IronClip is not affiliated with or endorsed by
either company.

## Build-time only

These are used to build or test IronClip but are not part of the shipped
binaries: [CMake](https://cmake.org), [NumPy](https://numpy.org) and
[SciPy](https://scipy.org) (filter design script), and the validators used in
CI ([Steinberg VST3 validator](https://github.com/steinbergmedia/vst3sdk),
[clap-validator](https://github.com/free-audio/clap-validator),
[pluginval](https://github.com/Tracktion/pluginval), Apple `auval`).
