## Install

1. Download **StageMic-Setup.exe** below.
2. Close OBS Studio.
3. Run the installer. Windows may show "Windows protected your PC" because StageMic is new and not
   code-signed yet: click **More info → Run anyway**.
4. Start OBS → Audio Mixer → three dots next to your mic → **Filters** → **+** → **StageMic**.

Updating from v0.2.0: just run the new installer. Your filter and its settings stay.

Full guide: [docs/INSTALL.md](https://github.com/mengsimaong/stagemic/blob/main/docs/INSTALL.md)

## New in 0.3.0

* **Tune (pitch correction)**, the new third tab next to EQ and Multiband:
  * Key + Scale (Chromatic, Major, Minor, Harmonic Minor, Pentatonics, Blues).
  * Style buttons: **Hard** (robotic stepping), **Pop**, **Melodic**, **Natural**, plus Speed and Amount.
  * **Voice: High / Mid / Low**. Pick High for a female voice: it stops vocal fry from being read as a note an octave down.
  * Formant (voice character) and Pitch (transpose), and a live pitch graph.
  * Adds 20 ms of delay while it's on, and only then.
* New presets: **Hard Tune**, **Pop Tune**, **Melodic Tune**.
* **Set up my voice**: an 11-second guided measurement that sets Input Gain, Gate and De-Ess for your mic and room.
  These mic settings then stay the same when you switch presets.
* **Signal chain bar**: drag modules to change their order, click to jump to one, a square to switch it on or off,
  and live activity bars. "Recommended" puts everything back.
* New tools in the EQ: **Tame** (dips the harsh 2-5 kHz "shout" only when a belt gets harsh) and
  **Pops** (removes the thump of "p" and "b").
* De-Esser: Band/Split modes, Auto frequency, Listen.
* Warmth: no more sub-bass thump following the syllables (found by listening to a real voice).
* Presets retuned on a real voice recording: steadier levels (about -14 LUFS) and controlled dynamics.

Needs Windows 10/11 (64-bit) and OBS Studio 31 or newer.

`StageMic-…-manual.zip` contains the same files for manual installation (advanced users).
