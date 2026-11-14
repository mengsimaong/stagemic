# Paopoa StageMic

**One OBS filter, one window: a clean, fun karaoke mic for streamers and VTubers.**
No DAW, and no stack of ten separate OBS filters.

> **Status: Phase 2 (built-in effects + presets), with a one-click Windows installer.** Next up: sound-quality round tuned on a real voice.

![The StageMic window](docs/images/stagemic-window.png)

## Goals

* **Native OBS plugin.** It appears in OBS's own filter list. It is not a VST2 loaded through OBS's old VST filter.
* **Super lightweight.** Modules that are switched off use no CPU. The default sound uses about 1% of one CPU core; even with every module on (including the oversampled Warmth and Air) it's under 4%. CI measures this on every change.
* **Zero latency.** The voice itself is never delayed; every effect works without lookahead buffers (tests enforce it). Warmth and Air (saturation and exciter) are oversampled for clean, alias-free harmonics, which delays only their added colour by about 0.1 ms while they're on. Pitch correction (Tune) adds 20 ms while it is switched on, and only then: it has to hear a cycle or two of the voice before it can correct it. OBS's Sync Offset can line the video up if needed.
* **Safe for live audio.** No clicks when switching effects or presets, and no memory allocation on the audio thread (enforced by tests).

## Features

| | Feature | Phase | Status |
|---|---|---|---|
| ✅ | OBS filter, window, automatic Windows builds | 1 | done |
| ✅ | Gate, EQ, De-Esser, Compressor, **Multiband**, **Saturation**, **Chorus**, Delay, Reverb, Limiter | 2 | in testing |
| 🔧 | Warmth (saturation: Tube / Tape / Console, 4x oversampled) and Air (exciter) | 2.6 | built; tuning on a real voice next |
| 🔧 | De-esser: Band mode (narrow dynamic cut), Split mode, Auto frequency, Listen | 2.6 | built; tuning on a real voice next |
| ✅ | **Re-order the chain** by dragging modules in the chain bar (Limiter stays last), one-click "Recommended order" | 2.6 | done |
| 🔧 | Sound-quality round: Auto Level, parallel effect sends with ducking, new plate-style reverb with pre-delay/filters, smoother compressor, relative de-esser, expander, 24 dB/oct low cut, softer limiter, saturation character | 2.6 | built; tuning on a real voice next |
| ✅ | Presets: 6 factory presets, Save / Save As / Delete, Import / Export `.stagemicpreset` files | 2 | in testing |
| 🔧 | "Set up my voice": 11-second guided measurement sets Input Gain, Gate and De-Ess frequency for your mic (kept across presets); chain bar with family colours, live activity, on/off and click-to-show | 2.7 | built; needs a real-mic check |
| 🔧 | Harshness tamer (EQ point 7): a dynamic band that dips 1.5–6 kHz only while a belt gets harsh, level-independent; off until Max Cut is raised | 2.7 | built; tune on her voice |
| 🔧 | Plosive tamer (EQ "Pops"): a high-pass that sweeps up to 160 Hz only during "p"/"b" thumps; zero latency, sung low notes untouched; off at 0 % | 2.7 | built; tune on her voice |
| ✅ | One-click Windows installer + simple download page | 2.5 | done |
| | Third-party **VST3** plugin rack: safe scanning, chaining, editor windows, saved in presets | 3 | |
| 🔧 | Real-time Tune: pitch correction with key + 7 scales, Speed (0 ms = hard robotic tune), Amount, Formant, Pitch (transpose), live pitch graph, Hard / Pop / Melodic / Natural styles and three Tune presets | 4 | built; needs a real-voice listen |
| | Robotic voice / vocoder | 4 | |
| | Custom dark UI: modular panels, big master meter, preset browser, light/dark toggle | 5 | |

**Why VST3 for the plugin rack (not VST2):** Steinberg stopped licensing the
VST2 SDK in 2018, while VST3 is openly licensed. Almost every current vocal
plugin ships as VST3, and VST3 can skip processing during silence. VST2 loading
could be added later if a must-have plugin only exists as VST2.

## Install

**[Download the installer from itch.io](https://mengsimaong.itch.io/paopoa-stagemic)**, close OBS, run it. Details: [docs/INSTALL.md](docs/INSTALL.md).

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
