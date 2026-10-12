#include "IronClip.h"
#include "IPlug_include_in_plug_src.h"

#if IPLUG_EDITOR
  #include "ui/Editor.h"
#endif

#include <algorithm>
#include <cmath>

using namespace iplug;
#if IPLUG_EDITOR
using namespace iplug::igraphics;
#endif
namespace dsp = ironclip::dsp;

namespace
{

constexpr int kNumPresets = 1;

// State chunk layout: magic, version, parameters, editor scale.
constexpr int kStateMagic = 0x49724353; // "IrCS"
constexpr int kStateVersion = 1;
constexpr float kMinEditorScale = 0.75f;
constexpr float kMaxEditorScale = 2.0f;

void DisplaySignedDb(double value, WDL_String& str)
{
  // Explicit sign for gains, but plain "0.00" (never "-0.00" or "+0.00").
  if (std::fabs(value) < 0.005)
    str.Set("0.00");
  else
    str.SetFormatted(16, "%+.2f", value);
}

void DisplayDb(double value, WDL_String& str)
{
  if (std::fabs(value) < 0.005)
    value = 0.0;
  str.SetFormatted(16, "%.2f", value);
}

} // namespace

IronClip::IronClip(const InstanceInfo& info)
: iplug::Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  GetParam(kParamDrive)->InitDouble("Drive", 0.0, dsp::ranges::kDriveMinDb, dsp::ranges::kDriveMaxDb, 0.01, "dB", 0, "",
                               IParam::ShapeLinear(), IParam::kUnitDB, DisplaySignedDb);
  GetParam(kParamCeiling)->InitDouble("Ceiling", 0.0, dsp::ranges::kCeilingMinDb, dsp::ranges::kCeilingMaxDb, 0.01, "dBFS", 0, "",
                                 IParam::ShapeLinear(), IParam::kUnitDB, DisplayDb);
  GetParam(kParamOutput)->InitDouble("Output", 0.0, dsp::ranges::kOutputMinDb, dsp::ranges::kOutputMaxDb, 0.01, "dB", 0, "",
                                IParam::ShapeLinear(), IParam::kUnitDB, DisplaySignedDb);
  // Oversampling changes the reported latency, which hosts only pick up
  // outside of playback; it is a setting, not an automation target.
  GetParam(kParamOversampling)->InitEnum("Oversampling", 0, { "Off", "2x", "4x", "8x" }, IParam::kFlagCannotAutomate);
  GetParam(kParamDelta)->InitBool("Delta", false, "", 0, "", "Off", "On");
  GetParam(kParamAutoGain)->InitBool("Auto Gain", false, "", 0, "", "Off", "On");
  GetParam(kParamBypass)->InitBool("Soft Bypass", false, "", 0, "", "Off", "On");

  MakeDefaultPreset("Default", kNumPresets);

#if IPLUG_EDITOR
  mMakeGraphicsFunc = [&]() {
    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, mEditorScale);
  };

  mLayoutFunc = [&](IGraphics* pGraphics) { ironclip::ui::BuildEditor(*pGraphics, *this); };
#endif
}

ironclip::dsp::ClipperParams IronClip::CurrentParams() const
{
  dsp::ClipperParams p;
  p.driveDb = GetParam(kParamDrive)->Value();
  p.ceilingDb = GetParam(kParamCeiling)->Value();
  p.outputDb = GetParam(kParamOutput)->Value();
  p.oversampling = static_cast<dsp::Oversampling>(std::clamp(GetParam(kParamOversampling)->Int(), 0, dsp::kNumOversamplingModes - 1));
  p.delta = GetParam(kParamDelta)->Bool();
  p.autoGain = GetParam(kParamAutoGain)->Bool();
  p.bypass = GetParam(kParamBypass)->Bool();
  return p;
}

int IronClip::RequiredLatency() const
{
  const int mode = std::clamp(GetParam(kParamOversampling)->Int(), 0, dsp::kNumOversamplingModes - 1);
  return dsp::ClipperEngine<sample>::LatencyFor(static_cast<dsp::Oversampling>(mode));
}

void IronClip::UpdateLatency()
{
  const int latency = RequiredLatency();
  if (latency != GetLatency())
    SetLatency(latency);
}

void IronClip::OnIdle()
{
  // Main thread, ~50 Hz, independent of the editor: report latency changes
  // from here rather than from the audio thread.
  UpdateLatency();
}

void IronClip::OnRestoreState()
{
  iplug::Plugin::OnRestoreState();
  UpdateLatency();
}

void IronClip::OnUIClose()
{
#if IPLUG_EDITOR
  // Remember the editor size for the next time it opens and for saved state.
  if (const IGraphics* pGraphics = GetUI())
    mEditorScale = std::clamp(pGraphics->GetDrawScale(), kMinEditorScale, kMaxEditorScale);
#endif
  iplug::Plugin::OnUIClose();
}

void IronClip::OnReset()
{
  // Hosts also reset on transport start; keep the auto gain estimate then and
  // only re-prepare when the sample rate actually changed.
  if (GetSampleRate() != mEngine.SampleRate())
    mEngine.Prepare(GetSampleRate());

  mEngine.SetParameters(CurrentParams());
  mEngine.Reset();
}

void IronClip::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  mEngine.SetParameters(CurrentParams());

  mEngine.Process(inputs, NInChansConnected(), outputs, NOutChansConnected(), nFrames);
}

bool IronClip::SerializeState(IByteChunk& chunk) const
{
  bool ok = chunk.Put(&kStateMagic) > 0;
  ok = ok && chunk.Put(&kStateVersion) > 0;
  ok = ok && SerializeParams(chunk);

  float scale = mEditorScale;
#if IPLUG_EDITOR
  if (const IGraphics* pGraphics = GetUI())
    scale = pGraphics->GetDrawScale();
#endif
  ok = ok && chunk.Put(&scale) > 0;
  return ok;
}

int IronClip::UnserializeState(const IByteChunk& chunk, int startPos)
{
  int magic = 0, version = 0;
  int pos = chunk.Get(&magic, startPos);
  if (pos < 0 || magic != kStateMagic)
    return -1;

  pos = chunk.Get(&version, pos);
  if (pos < 0 || version < 1)
    return -1;

  pos = UnserializeParams(chunk, pos);
  if (pos < 0)
    return pos;

  float scale = 1.f;
  const int scalePos = chunk.Get(&scale, pos);
  if (scalePos >= 0)
  {
    pos = scalePos;
    if (std::isfinite(scale))
    {
      mEditorScale = std::clamp(scale, kMinEditorScale, kMaxEditorScale);
#if IPLUG_EDITOR
      if (IGraphics* pGraphics = GetUI())
        pGraphics->Resize(PLUG_WIDTH, PLUG_HEIGHT, mEditorScale);
#endif
    }
  }
  return pos;
}

// ---------------------------------------------------------------------------
// CLAP adapter corrections
//
// iPlug2's CLAP wrapper (as pinned) deviates from the CLAP specification in a
// few places that clap-validator reports. The fixes live here as overrides so
// the upstream checkout stays untouched.

#if defined(CLAP_API)

#include "host-proxy.hxx" // template definitions for _host calls

namespace
{
const char* ClapPortTypeFor(uint32_t channels)
{
  return channels == 2 ? CLAP_PORT_STEREO : (channels == 1 ? CLAP_PORT_MONO : nullptr);
}
} // namespace

void IronClip::SetLatency(int samples)
{
  // clap_host_latency::changed() may only be called from activate(). While
  // inactive the host reads the new value when it next activates the plug-in;
  // while active it has to restart the plug-in first.
  IPlugProcessor::SetLatency(samples);
  if (_host.canUseLatency() && isActive())
    _host.requestRestart();
}

bool IronClip::stateSave(const clap_ostream* pStream) noexcept
{
  IByteChunk chunk;
  if (!SerializeState(chunk))
    return false;

  // Streams may accept fewer bytes than offered.
  const uint8_t* data = chunk.GetData();
  int64_t remaining = chunk.Size();
  while (remaining > 0)
  {
    const int64_t written = pStream->write(pStream, data, static_cast<uint64_t>(remaining));
    if (written <= 0 || written > remaining)
      return false;
    data += written;
    remaining -= written;
  }
  return true;
}

bool IronClip::stateLoad(const clap_istream* pStream) noexcept
{
  IByteChunk chunk;
  uint8_t buffer[512];
  int64_t bytesRead = 0;
  while ((bytesRead = pStream->read(pStream, buffer, sizeof buffer)) > 0)
    chunk.PutBytes(buffer, static_cast<int>(bytesRead));
  if (bytesRead < 0)
    return false;

  if (UnserializeState(chunk, 0) < 0)
    return false;

  OnRestoreState();
  // Parameter values changed behind the host's back: ask it to re-read them.
  if (_host.canUseParams())
    _host.paramsRescan(CLAP_PARAM_RESCAN_VALUES);
  return true;
}

bool IronClip::paramsInfo(uint32_t paramIdx, clap_param_info* pInfo) const noexcept
{
  const IParam* pParam = GetParam(static_cast<int>(paramIdx));
  if (!pParam)
    return false;
  const bool isDouble = pParam->Type() == IParam::kTypeDouble;

  clap_param_info_flags flags = CLAP_PARAM_REQUIRES_PROCESS;
  if (!isDouble)
    flags |= CLAP_PARAM_IS_STEPPED;
  if (pParam->GetCanAutomate())
    flags |= CLAP_PARAM_IS_AUTOMATABLE;
  // Let the host's bypass button drive the latency-compensated soft bypass.
  if (paramIdx == kParamBypass)
    flags |= CLAP_PARAM_IS_BYPASS;

  pInfo->id = paramIdx;
  pInfo->flags = flags;
  pInfo->cookie = nullptr;
  strncpy(pInfo->name, pParam->GetName(), CLAP_NAME_SIZE - 1);
  pInfo->name[CLAP_NAME_SIZE - 1] = 0;
  strncpy(pInfo->module, pParam->GetGroup(), CLAP_PATH_SIZE - 1);
  pInfo->module[CLAP_PATH_SIZE - 1] = 0;
  pInfo->min_value = isDouble ? 0.0 : pParam->GetMin();
  pInfo->max_value = isDouble ? 1.0 : pParam->GetMax();
  pInfo->default_value = pParam->GetDefault(isDouble);
  return true;
}

bool IronClip::paramsValueToText(clap_id paramIdx, double value, char* display, uint32_t size) noexcept
{
  const IParam* pParam = GetParam(static_cast<int>(paramIdx));
  if (!pParam || !display || size == 0)
    return false;

  // Continuous parameters are exposed normalised; stepped ones are snapped
  // to a valid step so text and value round-trip consistently.
  const bool isDouble = pParam->Type() == IParam::kTypeDouble;
  WDL_String str;
  pParam->GetDisplay(isDouble ? pParam->FromNormalized(value) : pParam->Constrain(value), false, str);
  if (CStringHasContents(pParam->GetLabel()))
  {
    str.Append(" ");
    str.Append(pParam->GetLabel());
  }

  if (static_cast<uint32_t>(str.GetLength()) >= size)
    return false;
  strcpy(display, str.Get());
  return true;
}

bool IronClip::audioPortsGetConfig(uint32_t index, clap_audio_ports_config* pConfig) const noexcept
{
  if (index >= static_cast<uint32_t>(NIOConfigs()))
    return false;

  const IOConfig* pIO = GetIOConfig(static_cast<int>(index));
  if (!pIO)
    return false;
  const uint32_t inChannels = static_cast<uint32_t>(pIO->NChansOnBusSAFE(ERoute::kInput, 0));
  const uint32_t outChannels = static_cast<uint32_t>(pIO->NChansOnBusSAFE(ERoute::kOutput, 0));

  pConfig->id = index;
  snprintf(pConfig->name, CLAP_NAME_SIZE, "%s", inChannels == 1 ? "Mono" : "Stereo");
  pConfig->input_port_count = static_cast<uint32_t>(pIO->NBuses(ERoute::kInput));
  pConfig->output_port_count = static_cast<uint32_t>(pIO->NBuses(ERoute::kOutput));
  pConfig->has_main_input = pConfig->input_port_count > 0;
  pConfig->main_input_channel_count = pConfig->has_main_input ? inChannels : 0;
  pConfig->main_input_port_type = ClapPortTypeFor(pConfig->main_input_channel_count);
  pConfig->has_main_output = pConfig->output_port_count > 0;
  pConfig->main_output_channel_count = pConfig->has_main_output ? outChannels : 0;
  pConfig->main_output_port_type = ClapPortTypeFor(pConfig->main_output_channel_count);
  return true;
}

#endif
