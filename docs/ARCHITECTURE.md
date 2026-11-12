# StageMic architecture

## Big picture

```
OBS Studio (Qt UI thread, audio thread, worker threads)
 └─ stagemic.dll  ── src/obs/        thin libobs glue (no DSP)
       │  FilterData per filter
       ▼
    FilterInstance ── src/core/       host shell: planar audio → JUCE buffers, state, window
       │
       ▼
    StageMicProcessor  (juce::AudioProcessor)  ← exact same class in StageMic.vst3
       └─ StageMicEditor (src/ui/) inside an EditorWindow
```

* **`src/core`** knows nothing about OBS. That lets the automated tests
  (`tests/`) and the VST3 build (`src/vst3/`) run the identical engine.
* **`src/obs`** is the only code that includes OBS headers.

## Threads

| OBS callback | Thread it runs on | What StageMic does |
|---|---|---|
| `obs_module_load` / `unload` | OBS UI thread | start / stop the shared JUCE runtime |
| `create` | UI thread (or e.g. obs-websocket) | build `FilterInstance`, prepare at OBS's sample rate, restore state |
| `filter_audio` | OBS audio thread (**real-time**) | `FilterInstance::process`: no locks, no allocation, no logging |
| `video_tick` | graphics thread | one-time "audio is flowing" log line |
| `save` | UI thread | state → base64 string in the scene collection |
| properties button | UI thread | post "open editor" to the JUCE message thread |
| `destroy` | often OBS's **destruction worker thread** | post the instance to the JUCE message thread for deletion |

**JUCE message thread.** On Windows this is OBS's own UI thread. JUCE
posts messages to a hidden Win32 window, and Qt's event loop delivers them,
the same way JUCE plugins work in any Windows host. On Linux (used for
development and CI) JUCE gets its own thread running its event loop.

`runtime::post()` keeps a FIFO queue and guarantees that queued work, such as
deleting a filter's window, still runs at `obs_module_unload`, before JUCE shuts
down. OBS finishes destroying every source before it unloads modules, so
nothing can be left behind.

**Lifetime.** Each filter's `FilterInstance` lives in a `shared_ptr`. UI
tasks hold only `weak_ptr`s, so an "open window" click that races with
filter removal is harmless. The last reference is always dropped on the
message thread, which means windows are always destroyed on the right thread.

## Audio details

* OBS hands us planar float audio at its global sample rate, up to
  `AUDIO_OUTPUT_FRAMES` (1024) frames per call. Mic sources usually send 480.
  Larger blocks are split into chunks.
* StageMic processes the first 1–2 channels (a mono or stereo vocal chain).
  Extra surround channels pass through untouched.
* **Latency:** an OBS filter cannot report latency to OBS. Any latency
  StageMic adds (for example hard-tune, Phase 4) will be shown in the UI so
  you can compensate with the source's **Sync Offset**.

## Built-in chain (Phase 2)

```
in gain → Gate → EQ → De-Esser → Compressor → Multiband → Saturation → Chorus → Delay → Reverb → Limiter → out gain
```

* `src/core/Parameters.*`: every setting (ID, range, default, unit) in one table,
  exposed through JUCE's `AudioProcessorValueTreeState`. The audio thread reads
  plain atomics.
* `src/core/dsp/`: one class per module, all allocation-free after `prepare()`.
  The audio is processed in 64-sample sub-blocks. Parameters glide once per
  sub-block (filter coefficients never jump), and gains ramp per sample.
* `VocalChain` runs the modules in an order stored as one atomic 64-bit word, so
  it can be changed from the UI with no locks (re-ordering UI comes later; the
  order is already saved in state and presets). Switching a module on or off
  crossfades over 15 ms. A module that is fully off is skipped entirely, so it
  uses no CPU.
* **Zero latency:** all filters are IIR (biquads, Linkwitz-Riley crossovers),
  the dynamics have no lookahead, and the limiter uses instant attack, so the
  ceiling is never exceeded. Chorus, delay and reverb only delay the *added*
  effect; the dry voice is untouched.
* **Presets** (`src/core/presets/`) are XML files: `<StageMicPreset>` containing
  the parameter tree and the module order. Settings missing from an older preset
  fall back to their defaults. User presets live in `Documents\StageMic\Presets`.
  Factory presets are defined in code as overrides of the defaults.
* **State saved by OBS** (version 2) = preset content + the selected preset's name.
  Version 1 state from Phase 1 loads as the default sound.

## The window (src/ui)

One compact screen, laid out at a fixed design size (1120 x 764) and scaled as
a whole when resized:

* `PresetBar`: preset browser (previous/next, drop-down), Save, and a menu with
  Save As / Import / Export / Delete / Open Preset Folder.
* `DisplayPanel`: EQ and Multiband tabs, each with its own power switch. The EQ
  has four draggable points (the curve is computed from the same filter maths
  the audio uses). The Multiband view has draggable band splits and thresholds
  with live gain reduction. A live output spectrum sits behind both.
  `SpectrumTap` hands audio to the analyzer lock-free, and only while the
  window is open.
* `Strips`: Gate, De-Ess, Compressor and Saturation strips (faders and knobs),
  plus compact rows for Chorus, Delay, Reverb and Limiter. Every module has an
  LED power switch, and the dynamics modules show live gain reduction.
* `MasterPanel`: big IN/OUT meters with peak hold, input/output gain, status.

## Real-time safety, enforced by tests

`tests/FilterInstanceTests.cpp` replaces the global allocator and fails if
`process()` allocates even once. It also checks bit-exact passthrough across
sample rates, channel counts and odd block sizes.

`scripts/obs-smoke-test.sh` loads the plugin into a real OBS (Linux, on a
virtual screen), plays a test tone through it, and checks the log for
load, audio flow, clean shutdown and zero leaks. CI runs it on every push.

## Reference

The overall approach (one shared JUCE runtime, JUCE on OBS's UI thread,
out-of-process plugin scanning in Phase 3) follows the open-source
[atkAudio OBS plugin](https://github.com/atkAudio/PluginForObsRelease)
(AGPL-3.0). StageMic's code is written independently.
