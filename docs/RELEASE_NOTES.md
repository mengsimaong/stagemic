## Install

1. Download **StageMic-Setup.exe** below.
2. Close OBS Studio.
3. Run the installer. Windows may show "Windows protected your PC" because it's new and not
   code-signed yet: click **More info → Run anyway**.
4. Start OBS → Audio Mixer → three dots next to your mic → **Filters** → **+** → **Paopoa StageMic**.

Updating from an earlier version: just run the new installer. Your filter, its settings and your presets stay.

## New in 0.4.1

* **The window fits small laptop screens** (1366 x 768, or 1080p at 125-150% scaling): it opens shrunk
  to fit, on the screen you're using, and remembers the size you choose.
* **First open welcomes you with "Set up my voice"** (once; "Skip for now" skips it).
* **Set up my voice now really silences a laptop fan or hiss between phrases**: it measures the noise's
  peaks (what the gate actually reacts to) and uses a firmer gate in noisy rooms.
* **Mic on one side only** (common with audio interfaces, e.g. input 1 = left): heard on both sides automatically.
* **Safety:** the output can never pass 0 dBFS, even with Output turned way up and the Limiter off; and a
  glitch from a driver or another filter can no longer break the sound until OBS restarts.
* A damaged or hand-edited preset file can no longer break the sound: settings that aren't valid
  numbers fall back to their defaults.
* Window: lighter on the CPU (cached cat and logo, redraws only what changes); a closed or minimised
  window now costs the audio nothing at all.
* Tune: when it's off, the graph explains what it does and how to switch it on.

## From 0.4.0

* **New name: Paopoa StageMic.**
* **Simple / Advanced mode** (switch in the header). Simple, the new default, shows just the essentials
  with big controls: Voice (Clean-up, Tone, Compress, De-Ess, Warmth), Effects (Reverb, Echo, Chorus)
  and Tune. Advanced is the full view with every module, the EQ graph and the signal chain.
  Switching never changes the sound.
* **Fun voices:** Chipmunk, Deep Monster, Robot, Telephone, Megaphone, Bathroom, Concert Hall,
  Cave Echo and Alien. The preset menu is now grouped: Vocal, Tune, Fun voices, My presets.
* Tune: new **Robot (one note)** scale for the flat robot voice.
* Clearer tabs: the open tab joins its panel, the others sit behind it and light up under the mouse.

## Also in this version (from 0.3.0)

* Tune (pitch correction) with Key, Scale, Voice (High/Mid/Low), Speed, Formant and Pitch; Hard / Pop /
  Melodic / Natural styles. Adds 20 ms of delay only while it's on.
* Set up my voice, the re-orderable signal chain, Tame and Pops in the EQ, De-Esser modes.

Needs Windows 10/11 (64-bit) and OBS Studio 31 or newer.

`StageMic-…-manual.zip` contains the same files for manual installation (advanced users).
