#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "dsp/ClipperEngine.h"

// Parameter indices are part of the saved state and host automation data:
// never reorder them, only append.
enum EParams
{
  kParamDrive = 0,
  kParamCeiling,
  kParamOutput,
  kParamOversampling,
  kParamDelta,
  kParamAutoGain,
  kParamBypass,
  kNumParams
};

class IronClip final : public iplug::Plugin
{
public:
  explicit IronClip(const iplug::InstanceInfo& info);

  void ProcessBlock(iplug::sample** inputs, iplug::sample** outputs, int nFrames) override;
  void OnReset() override;
  void OnIdle() override;
  void OnRestoreState() override;
  void OnUIClose() override;

  bool SerializeState(iplug::IByteChunk& chunk) const override;
  int UnserializeState(const iplug::IByteChunk& chunk, int startPos) override;

  /** Lock-free audio-thread data for the editor (meters, history). */
  ironclip::dsp::Telemetry& GetTelemetry() { return mEngine.GetTelemetry(); }

  /** Latency the host should compensate for the selected oversampling mode. */
  int RequiredLatency() const;

#if defined(CLAP_API)
  // Corrections to iPlug2's CLAP adapter (see IronClip.cpp).
  void SetLatency(int samples) override;

private:
  bool stateSave(const clap_ostream* pStream) noexcept override;
  bool stateLoad(const clap_istream* pStream) noexcept override;
  bool paramsInfo(uint32_t paramIdx, clap_param_info* pInfo) const noexcept override;
  bool paramsValueToText(clap_id paramIdx, double value, char* display, uint32_t size) noexcept override;
  bool audioPortsGetConfig(uint32_t index, clap_audio_ports_config* pConfig) const noexcept override;
#endif

private:
  ironclip::dsp::ClipperParams CurrentParams() const;
  void UpdateLatency();

  ironclip::dsp::ClipperEngine<iplug::sample> mEngine;
  float mEditorScale = 1.f;
};
