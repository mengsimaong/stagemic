# Backlog: known issues and planned work

Things to fix or build, most important first. Add what testers report (with the preset, what it
sounded like and roughly when) under the matching item.

## Bugs to fix

### Fun voices don't all work well (reported after v0.4.0)
Not every fun voice sounds right yet. To check one by one on a real voice (render each preset with
`stagemic_render` on a sung and a spoken clip, listen, and measure):

* **Chipmunk / Deep Monster / Alien** (pitch shift through Tune with Amount 0 and Pitch +7 / -7 / -3):
  * Tune only shifts *voiced* sound. Breaths and "s"/"sh"/"t" stay at the original pitch, which can
    sound detached from a strongly shifted voice. Possible fix: shift unvoiced parts too (resample
    them by the same ratio), or a dedicated pitch/formant shifter for voice changing that doesn't
    depend on pitch detection.
  * Large shifts (more than about 5 semitones) stretch the grain method: check for roughness,
    "double" voices and gaps, especially Deep Monster going down (grains spread apart).
  * Formant at its limits (+-5 st) may sound unnatural; check the range and the grain read-out.
  * The presets use Voice: Mid. For high voices, High gives cleaner detection.
* **Robot** (one-note Tune + fast chorus + short delay): wherever pitch detection drops out
  (consonants, breathy or very low/high notes) the real voice slips through. Possible fix: a real
  robot effect (ring modulator / monotone vocoder) that doesn't need detection, or hold the last
  note through short drop-outs.
* **Telephone / Megaphone**: check distortion amount and level on loud singing.
* **Bathroom / Concert Hall / Cave Echo**: check the reverb/echo amount on talking vs singing, and
  that ducking doesn't make them disappear while singing.

### Other
* Windows: confirm window drawing cost with the window open (measured ~4.5% of a core on Linux with
  software drawing); add an option to turn off animations if it's high.
* Stress test (tools/StressBench.cpp, `stagemic_bench <voice.wav>`): run it before each release.
* Before each release also run the tests and `stagemic_bench` built with `-fsanitize=address,undefined`,
  and `STAGEMIC_UI_LIVE=storm` built with `-fsanitize=thread` (all clean in Nov 2026), and check that
  the Windows CI job is green (a Linux-only call in the test program broke it unnoticed for two commits).

## Beginner / low-end laptop review (Nov 2026): not done yet
* Reverb is on in the default preset (Clean Vocal). Fine for singing; for chat and gaming a first-run
  choice ("Mostly singing / Mostly talking") could pick Clean Vocal or Just Talking.
* Labels still use studio words in places (Threshold, Max cut, dB). Simple mode could say
  "Smooth out loud parts" etc., with numbers only in Advanced.
* Auto Level's orange "-6.4 dB" read-out means nothing to a beginner: show a small "turning you down /
  up" arrow instead, or hide it in Simple mode.
* Output meter: a clear "too loud" warning (red, text) when the stream would clip, and "too quiet"
  when the voice sits under -30 dBFS for a while.
* A big "Before / After" button (bypass while held) so beginners can hear what the filter does.
* Tune graph takes the biggest area even when Tune is off and most streamers talk: collapse it to a
  small card when off.
* Window text gets small when shrunk to fit 1366 x 768 at 125% (about 64% size): a compact layout
  for small screens would read better.

## Planned
* Breath & fry control: gently lower breaths, lip noise and creaky onsets between phrases.
* "Set up my voice" also picks Tune's Voice type (High/Mid/Low) from the measured pitch.
* Doubler (thicker, "two singers" voice).
* Compressor modes (opto / fast) and auto make-up.
* Delay tap-tempo; more reverb room types.
* Vocoder (Phase 4).
* VST3 plugin rack: load third-party plugins inside the chain (Phase 3).
* OBS hotkeys for presets and module on/off; A/B compare; undo/redo; loudness (LUFS) meter.
* Release housekeeping: drop the duplicate fixed-name installer from GitHub releases.
