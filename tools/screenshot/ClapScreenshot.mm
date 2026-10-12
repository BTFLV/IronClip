// Minimal CLAP host that renders the real IronClip editor to a PNG.
//
// It loads a CLAP build, feeds it a synthetic drum-and-bass loop in real time
// so the meters and history show genuine processing results, opens the editor
// in an off-screen window and captures it. The capture relies on the CPU
// (raster) renderer, configure with -DIRONCLIP_RENDERER=CPU.
//
// Usage: ironclip-screenshot <IronClip.clap> <out.png> [name=value ...]
//   names: drive, ceiling, output (dB), oversampling (0-3), delta, autogain,
//          bypass (0/1), seconds (run time before the capture)

#import <Cocoa/Cocoa.h>

#include <clap/clap.h>
#include <dlfcn.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace
{

constexpr double kSampleRate = 48000.0;
constexpr uint32_t kBlock = 480; // 10 ms

// --- host ---------------------------------------------------------------------

const void* HostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void HostRequestRestart(const clap_host_t*) {}
void HostRequestProcess(const clap_host_t*) {}
void HostRequestCallback(const clap_host_t*) {}

clap_host_t MakeHost()
{
  clap_host_t host {};
  host.clap_version = CLAP_VERSION;
  host.name = "ironclip-screenshot";
  host.vendor = "IronClip";
  host.url = "";
  host.version = "1";
  host.get_extension = HostGetExtension;
  host.request_restart = HostRequestRestart;
  host.request_process = HostRequestProcess;
  host.request_callback = HostRequestCallback;
  return host;
}

// --- events -------------------------------------------------------------------

struct EventList
{
  std::vector<clap_event_param_value_t> events;

  static uint32_t Size(const clap_input_events_t* list)
  {
    return static_cast<uint32_t>(static_cast<EventList*>(list->ctx)->events.size());
  }
  static const clap_event_header_t* Get(const clap_input_events_t* list, uint32_t i)
  {
    return &static_cast<EventList*>(list->ctx)->events[i].header;
  }
  clap_input_events_t AsInput()
  {
    return { this, &EventList::Size, &EventList::Get };
  }

  void AddParam(clap_id id, double value)
  {
    clap_event_param_value_t e {};
    e.header.size = sizeof e;
    e.header.time = 0;
    e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    e.header.type = CLAP_EVENT_PARAM_VALUE;
    e.param_id = id;
    e.note_id = -1;
    e.port_index = -1;
    e.channel = -1;
    e.key = -1;
    e.value = value;
    events.push_back(e);
  }
};

bool OutTryPush(const clap_output_events_t*, const clap_event_header_t*) { return true; }

// --- test signal ----------------------------------------------------------------

/** Kick, snare, hats, bass and a pad at 124 BPM. */
class Loop
{
public:
  void Render(float* l, float* r, uint32_t n)
  {
    const double beat = 60.0 / 124.0;
    for (uint32_t i = 0; i < n; ++i, ++mSample)
    {
      const double t = mSample / kSampleRate;
      const double inBeat = std::fmod(t, beat);
      const int beatIndex = static_cast<int>(t / beat);
      const double inEighth = std::fmod(t, beat * 0.5);

      // kick: pitch-swept sine
      const double kickEnv = std::exp(-inBeat / 0.16);
      mKickPhase += 2.0 * M_PI * (45.0 + 110.0 * std::exp(-inBeat / 0.03)) / kSampleRate;
      if (inBeat < 1.0 / kSampleRate)
        mKickPhase = 0.0;
      const double kick = 0.62 * kickEnv * std::sin(mKickPhase);

      // snare on 2 and 4
      const double noise = mNoise(mRng);
      double snare = 0.0;
      if (beatIndex % 2 == 1)
      {
        const double env = std::exp(-inBeat / 0.09);
        mSnareLp += 0.45 * (noise - mSnareLp);
        snare = env * (0.30 * mSnareLp + 0.22 * std::sin(2.0 * M_PI * 185.0 * inBeat));
      }

      // hats on eighths: high-passed noise
      const double hp = noise - mHatPrev;
      mHatPrev = noise;
      const double hats = 0.07 * std::exp(-inEighth / 0.025) * hp;

      // bass and pad
      const double bassEnv = 1.0 - 0.7 * kickEnv;
      const double bass = 0.16 * bassEnv * (std::sin(2.0 * M_PI * 55.0 * t) + 0.35 * std::sin(2.0 * M_PI * 110.0 * t));
      const double pad = 0.03 * (std::sin(2.0 * M_PI * 220.0 * t) + std::sin(2.0 * M_PI * 277.18 * t) + std::sin(2.0 * M_PI * 329.63 * t));

      const double mono = kick + snare + bass;
      l[i] = static_cast<float>(mono + hats * 0.8 + pad);
      r[i] = static_cast<float>(mono + hats * 1.2 + pad * 0.9);
    }
  }

private:
  uint64_t mSample = 0;
  double mKickPhase = 0.0, mSnareLp = 0.0, mHatPrev = 0.0;
  std::mt19937 mRng { 1234 };
  std::uniform_real_distribution<double> mNoise { -1.0, 1.0 };
};

double Arg(const std::map<std::string, double>& args, const char* name, double fallback)
{
  auto it = args.find(name);
  return it == args.end() ? fallback : it->second;
}

} // namespace

int main(int argc, char** argv)
{
  @autoreleasepool
  {
    if (argc < 3)
    {
      std::fprintf(stderr, "usage: %s <plugin.clap> <out.png> [name=value ...]\n", argv[0]);
      return 2;
    }
    std::map<std::string, double> args;
    for (int i = 3; i < argc; ++i)
    {
      const char* eq = std::strchr(argv[i], '=');
      if (eq)
        args[std::string(argv[i], static_cast<size_t>(eq - argv[i]))] = std::atof(eq + 1);
    }

    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];

    const std::string bundle = argv[1];
    const std::string binary = bundle + "/Contents/MacOS/" + [[[NSString stringWithUTF8String:bundle.c_str()] lastPathComponent] stringByDeletingPathExtension].UTF8String;
    void* lib = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!lib)
    {
      std::fprintf(stderr, "dlopen failed: %s\n", dlerror());
      return 1;
    }
    auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(lib, "clap_entry"));
    if (!entry || !entry->init(bundle.c_str()))
    {
      std::fprintf(stderr, "invalid CLAP entry\n");
      return 1;
    }
    auto* factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    const clap_plugin_descriptor_t* desc = factory->get_plugin_descriptor(factory, 0);
    static clap_host_t host = MakeHost();
    const clap_plugin_t* plugin = factory->create_plugin(factory, &host, desc->id);
    if (!plugin || !plugin->init(plugin))
    {
      std::fprintf(stderr, "plugin creation failed\n");
      return 1;
    }
    std::printf("loaded %s %s\n", desc->name, desc->version);

    // Parameters: continuous ones are normalised, stepped ones are plain values.
    EventList params;
    params.AddParam(0, (Arg(args, "drive", 9.0) + 12.0) / 36.0);
    params.AddParam(1, (Arg(args, "ceiling", -3.0) + 30.0) / 30.0);
    params.AddParam(2, (Arg(args, "output", 0.0) + 24.0) / 36.0);
    params.AddParam(3, Arg(args, "oversampling", 2.0));
    params.AddParam(4, Arg(args, "delta", 0.0));
    params.AddParam(5, Arg(args, "autogain", 1.0));
    params.AddParam(6, Arg(args, "bypass", 0.0));

    if (!plugin->activate(plugin, kSampleRate, 1, kBlock) || !plugin->start_processing(plugin))
    {
      std::fprintf(stderr, "activation failed\n");
      return 1;
    }

    // Editor in an off-screen window
    auto* gui = static_cast<const clap_plugin_gui_t*>(plugin->get_extension(plugin, CLAP_EXT_GUI));
    if (!gui || !gui->is_api_supported(plugin, CLAP_WINDOW_API_COCOA, false) || !gui->create(plugin, CLAP_WINDOW_API_COCOA, false))
    {
      std::fprintf(stderr, "no cocoa GUI\n");
      return 1;
    }
    uint32_t w = 0, h = 0;
    gui->get_size(plugin, &w, &h);
    NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(-6000, -6000, w, h)
                                                   styleMask:NSWindowStyleMaskBorderless
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    NSView* content = window.contentView;
    clap_window_t parent {};
    parent.api = CLAP_WINDOW_API_COCOA;
    parent.cocoa = (__bridge void*)content;
    gui->set_parent(plugin, &parent);
    gui->show(plugin);
    [window orderBack:nil];

    // Stream audio in real time while the editor runs
    Loop loop;
    std::vector<float> inL(kBlock), inR(kBlock), outL(kBlock), outR(kBlock);
    float* ins[] = { inL.data(), inR.data() };
    float* outs[] = { outL.data(), outR.data() };
    clap_audio_buffer_t inBuf {}, outBuf {};
    inBuf.data32 = ins;
    inBuf.channel_count = 2;
    outBuf.data32 = outs;
    outBuf.channel_count = 2;
    EventList none;
    clap_output_events_t outEvents { nullptr, OutTryPush };
    bool first = true;
    int64_t steady = 0;

    const double seconds = Arg(args, "seconds", 4.6);
    NSDate* start = [NSDate date];
    double processed = 0.0;
    while (true)
    {
      const double elapsed = -[start timeIntervalSinceNow];
      if (elapsed >= seconds)
        break;
      while (processed < elapsed)
      {
        loop.Render(inL.data(), inR.data(), kBlock);
        clap_input_events_t inEvents = first ? params.AsInput() : none.AsInput();
        clap_process_t proc {};
        proc.steady_time = steady;
        proc.frames_count = kBlock;
        proc.audio_inputs = &inBuf;
        proc.audio_outputs = &outBuf;
        proc.audio_inputs_count = 1;
        proc.audio_outputs_count = 1;
        proc.in_events = &inEvents;
        proc.out_events = &outEvents;
        plugin->process(plugin, &proc);
        first = false;
        steady += kBlock;
        processed += kBlock / kSampleRate;
      }
      [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.005]];
    }

    NSBitmapImageRep* rep = [content bitmapImageRepForCachingDisplayInRect:content.bounds];
    [content cacheDisplayInRect:content.bounds toBitmapImageRep:rep];
    NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    const bool ok = [png writeToFile:[NSString stringWithUTF8String:argv[2]] atomically:YES];
    std::printf("%s %ldx%ld -> %s\n", ok ? "captured" : "FAILED", (long)rep.pixelsWide, (long)rep.pixelsHigh, argv[2]);

    gui->destroy(plugin);
    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    entry->deinit();
    return ok ? 0 : 1;
  }
}
