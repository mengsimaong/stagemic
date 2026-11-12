# StageMic

**One OBS filter, one window: a clean, fun karaoke mic for streamers and VTubers.**
No DAW, and no stack of ten separate OBS filters.

> **Status: Phase 2 (built-in effects + presets), with a one-click Windows installer.** Next up: sound-quality round tuned on a real voice.

![The StageMic window](docs/images/stagemic-window.png)

## Goals

* **Native OBS plugin.** It appears in OBS's own filter list. It is not a VST2 loaded through OBS's old VST filter.
* **Super lightweight.** Modules that are switched off use no CPU. The full chain uses about 1.3% of one CPU core, and CI checks this on every change.
* **Zero latency.** Every built-in effect works without lookahead buffers, and a test enforces it. The one future exception is pitch correction (Tune, Phase 4), which needs about 5-10 ms while it is switched on, and only then.
* **Safe for live audio.** No clicks when switching effects or presets, and no memory allocation on the audio thread (enforced by tests).

## Features

| | Feature | Phase | Status |
|---|---|---|---|
| ✅ | OBS filter, window, automatic Windows builds | 1 | done |
| ✅ | Gate, EQ, De-Esser, Compressor, **Multiband**, **Saturation**, **Chorus**, Delay, Reverb, Limiter | 2 | in testing |
| ✅ | Presets: 6 factory presets, Save / Save As / Delete, Import / Export `.stagemicpreset` files | 2 | in testing |
| ✅ | One-click Windows installer + simple download page | 2.5 | done |
| | Third-party **VST3** plugin rack: safe scanning, chaining, editor windows, saved in presets | 3 | |
| | Real-time Tune (hard-tune, key/scale) | 4 | |
| | Robotic voice / vocoder | 4 | |
| | Custom dark UI: modular panels, big master meter, preset browser, light/dark toggle | 5 | |

**Why VST3 for the plugin rack (not VST2):** Steinberg stopped licensing the
VST2 SDK in 2018, while VST3 is openly licensed. Almost every current vocal
plugin ships as VST3, and VST3 can skip processing during silence. VST2 loading
could be added later if a must-have plugin only exists as VST2.

## Install

**[Download StageMic-Setup.exe](https://github.com/mengsimaong/stagemic/releases/latest/download/StageMic-Setup.exe)**, close OBS, run it. Details: [docs/INSTALL.md](docs/INSTALL.md).

## Building it yourself

You don't need to: GitHub Actions builds Windows binaries on every push. If you want to:

* **Windows:** Visual Studio 2022 with the C++ workload, and CMake 3.28+.
  ```
  cmake --preset windows-x64
  cmake --build --preset windows-x64
  ```
  The first configure downloads and builds the OBS libraries (about 10 minutes).
* **Linux (development):** `apt install libobs-dev obs-studio` plus JUCE's
  usual dependencies (see `.github/workflows/build.yml`), then
  `cmake --preset linux && cmake --build --preset linux`.
* **Engine and tests only (no OBS):** `cmake --preset engine-only`.

How it fits together: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## License

AGPL-3.0-or-later: free for everyone to use, including on monetized streams.
If you distribute a modified version, you must share its source code too. See
[LICENSE](LICENSE) and [THIRD_PARTY.md](THIRD_PARTY.md).
