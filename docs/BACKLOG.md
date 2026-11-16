# Backlog: known issues and planned work

## The performance rule (every change is held to this)
* **Simple mode is the light one**: for a beginner on a low-end PC. Nothing may make it heavier.
* **Advanced mode is the power-user one**: anything that costs more lives there, or is off by default
  and costs nothing until it is used (the two extra EQ bells are skipped entirely at 0 dB; filters
  only glide while a setting is moving; the spectrum only runs while it is on screen).
* **Never slower than the last release.** Before releasing, build the last release's tag next to the
  new code and compare on the same machine:
  * audio, the reliable way: count instructions with valgrind, which gives the same number every run
    (timing on a shared machine swings +-20% between identical runs). For each preset, on both builds:
    `valgrind --tool=callgrind --callgrind-out-file=/dev/null stagemic_render <8 s clip> out "<preset>"`,
    minus a run with every module off (the program's own start-up). `callgrind_annotate` on one preset
    shows which function grew;
  * audio, quick look: `stagemic_bench <voice.wav> 6 cost` on both, run alternately (each module alone
    and every preset, best of 5);
  * window: `stagemic_devtools ui-live simple` (with `clean` and without) and
    `stagemic_devtools ui-live` on both builds.
  Anything slower gets fixed or made optional before it ships. (After 0.4.2: every preset 0.2-1.3%
  *fewer* instructions than 0.4.1, after the EQ's filter loop was rewritten to keep each band in
  registers.) (Nov 2026, against 0.4.1: audio within
  noise on every module and preset; Simple window equal with Tune off and about a third lighter with
  Tune on; Advanced window not slower.)

Things to fix or build, most important first. Add what testers report (with the preset, what it
sounded like and roughly when) under the matching item.

## Bugs to fix

### Fun voices don't all work well (reported after v0.4.0)
Not every fun voice sounds right yet. To check one by one on a real voice (render each preset with
`stagemic_render` on a sung and a spoken clip, listen, and measure):

* **Chipmunk / Deep Monster / Alien** (pitch shift through Tune with Amount 0 and Pitch +7 / -7 / -3):
  * Done (after 0.4.2): breaths and "s"/"sh"/"t" now get the same formant shift as the voice
    (measured on a real voice: Chipmunk x1.23 brighter, Deep Monster x0.65, Alien x1.17; no new clicks).
  * Large shifts (more than about 5 semitones) stretch the grain method: check for roughness,
    "double" voices and gaps, especially Deep Monster going down (grains spread apart).
  * Formant at its limits (+-5 st) may sound unnatural; check the range and the grain read-out.
  * The presets use Voice: Mid. For high voices, High gives cleaner detection.
* **Robot** (partly done after 0.4.2: holds its note ~55 ms through unclear readings; off-note
  moments 12.0% -> 11.1%; about half of the rest are at phrase starts/ends)
  (one-note Tune + fast chorus + short delay): wherever pitch detection drops out
  (consonants, breathy or very low/high notes) the real voice slips through. Possible fix: a real
  robot effect (ring modulator / monotone vocoder) that doesn't need detection, or hold the last
  note through short drop-outs.
* Checked (after 0.4.2): Telephone, Megaphone, Bathroom, Concert Hall and Cave Echo come out within
  about 1 dB of Clean Vocal on singing, peaks under the limiter, not over-squashed. At a talking-like
  level Telephone/Megaphone are ~5 dB louder (their heavy compression; part of the effect).
  Still to do with a real speech recording.

### Other
* Windows: confirm window drawing cost with the window open (measured ~4.5% of a core on Linux with
  software drawing); add an option to turn off animations if it's high.
* Stress test (tools/StressBench.cpp, `stagemic_bench <voice.wav>`): run it before each release.
* Before each release also run the tests and `stagemic_bench` built with `-fsanitize=address,undefined`,
  and `stagemic_devtools ui-live storm` built with `-fsanitize=thread` (all clean in Nov 2026), and check that
  the Windows CI job is green (a Linux-only call in the test program broke it unnoticed for two commits).

## Beginner / low-end laptop review (Nov 2026): not done yet
* Reverb is on in the default preset (Clean Vocal). Fine for singing; for chat and gaming a first-run
  choice ("Mostly singing / Mostly talking") could pick Clean Vocal or Just Talking.
* Done (after 0.4.2): plainer Simple-mode labels. (Numbers still show; hiding them is an option.)
* Done (after 0.4.2): Auto Level read-out in words ("down 6 dB", "up 3 dB", "steady").
* Done (after 0.4.2): plain-words level advice under the meters (clipping, too loud, too quiet,
  no sound, good).
* Done (after 0.4.2): "Hold: hear before" button.
* Tune graph takes the biggest area even when Tune is off and most streamers talk: collapse it to a
  small card when off. (Since 0.4.2 it explains itself and draws nothing while off; the space remains.)
* ASMR: a preset for whispering (gentle gate that never cuts soft whispers, no reverb, softer de-ess),
  and "Set up my voice" should cope with whisper-level voices in a quiet room.
* Window text gets small when shrunk to fit 1366 x 768 at 125% (about 64% size): a compact layout
  for small screens would read better.

## Visual update, stage 2
* Advanced panels sized to their content (Warmth is half empty).
* Slimmer level meters with a clear red "too loud" zone (also cheaper to draw).
* Smoother spectrum (fewer points, smoothed curve: looks better and costs less).

## Planned
* Reverb pre-delay: a shorter default or a "tight" option (30 ms can feel late on a loud reverb).
* EQ: width control for the low and high shelves.
* Done (after 0.4.2): EQ points switch on/off individually; flat or switched-off bands are skipped.
* Optional "light quality" for Warmth, Air and Tune, the three heaviest modules (only if a real
  low-end PC needs it; it changes the sound).
* Square icon (512 and 256 px) for itch.io and the OBS forum; an OBS forum listing.
* Breath & fry control: gently lower breaths, lip noise and creaky onsets between phrases.
* "Set up my voice" also picks Tune's Voice type (High/Mid/Low) from the measured pitch.
* Doubler (thicker, "two singers" voice).
* Compressor modes (opto / fast) and auto make-up.
* Delay tap-tempo; more reverb room types.
* Vocoder.
* VST3 plugin rack: load third-party plugins inside the chain.
* OBS hotkeys for presets and module on/off; A/B compare; undo/redo; loudness (LUFS) meter.
* Done (after 0.5.0): releases no longer carry a duplicate fixed-name installer, and now include
  PaopoaStageMic-(version)-source.zip.
