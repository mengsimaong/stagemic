## Install

1. Download the installer, **StageMic-Setup-(version).exe**, below.
2. Close OBS Studio.
3. Run the installer. Windows may show "Windows protected your PC" because it's new and not
   code-signed yet: click **More info → Run anyway**.
4. Start OBS → Audio Mixer → three dots next to your mic → **Filters** → **+** → **Paopoa StageMic**.

Updating from an earlier version: just run the new installer. Your filter, its settings and your presets stay.

## New in 0.5.0

* **Fun voices sound more like one voice:** on Chipmunk, Deep Monster, Alien and Robot, breaths and
  "s"/"sh"/"t" sounds now get the same voice change as the singing, instead of staying in your
  normal voice. Normal tuning presets are unchanged (bit-identical).
* **"Hold: hear before"** under the meters: hold it to hear your mic without StageMic, let go to hear
  it with. (Your stream hears it too while you hold it.)
* **First time, it asks what you mostly do** on stream: Sing, Talk & game, or ASMR (whisper), and
  starts you on a matching preset along with your mic settings.
* **New ASMR preset:** no noise gate (soft whispers are never cut), no reverb, gentle levelling and
  compression, a light de-esser and firmer pop control for close-mic sounds.
* **EQ points can be switched on and off one by one** (the square on each point's chip, Advanced mode),
  to hear what each one does. Switching glides, with no click.
* **Lighter on the CPU:** the EQ runs more efficiently and skips any band that is flat (0 dB) or off,
  with exactly the same sound. Everyday presets now use 1-6% less CPU than in 0.4.1.
* **Simple mode in plainer words:** "Silence below", "Starts at", "Strength", "Amount", and Auto Level
  says "up 3 dB" / "down 6 dB" / "steady" instead of a bare number.
* **Robot holds its note longer** through breathy moments, so less of the normal voice slips through.
* **Plain-words level advice** under the meters: "Level is good", "Too quiet: try Set up my voice",
  "Mic is clipping: turn its gain down", "Too loud: turn Output down", or "No sound from the mic".

## From 0.4.2

* **New look:** each module family has its own colour (the same as its chip in the chain bar), pink is
  kept for the brand and the main actions; every control reads name, knob, value; Simple mode is three
  even rows.
* **Simple mode's window is lighter:** the Tune graph draws nothing while Tune is off and about a third
  as much while it is on, and the level meters redraw only the part that moved. Nothing in the sound
  engine is slower than 0.4.1 (measured side by side, every module and preset).
* **EQ: seven points.** Two new bell bands, Low Mid and High Mid (each with frequency, gain and
  width), for detailed work. They start at 0 dB, where they are not in the sound at all, so every
  existing preset and saved setup sounds exactly as before. Under the graph there is now one chip per
  point showing where it sits; click a chip, or grab the point on the graph, to get its knobs.
  The Tame point is now number 7.
* **EQ graph fixes:**
  * Where two points sit on the same spot (point 2 starts on the same line as point 1), you now grab
    the point you see on top - the one you used last - instead of the one underneath.
  * The curve now shows the low cut's real steepness (it always drew 12 dB/oct, even at 24 dB/oct).
  * Dragging points or knobs quickly no longer makes a faint crackle: the filters glide to their new
    shape instead of jumping.

## From 0.4.1

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
