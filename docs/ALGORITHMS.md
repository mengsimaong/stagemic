# Algorithms and where they come from

StageMic's audio processing is built from standard, published signal-processing methods. The
methods are public knowledge (taught in textbooks and described in papers); the code that
implements them was written for this project. No code is copied from other audio plugins.
Third-party libraries and their licences are listed in [THIRD_PARTY.md](../THIRD_PARTY.md).

| Where | Method | Published in |
|---|---|---|
| EQ bands, filters everywhere (`DspUtils.h`) | Biquad filter designs: low/high shelf, peaking, high/low pass, band pass | R. Bristow-Johnson, "Cookbook formulae for audio EQ biquad filter coefficients" (the "Audio EQ Cookbook") |
| EQ low cut 24 dB/oct, multiband crossovers | Butterworth and Linkwitz-Riley filters | S. Butterworth, "On the Theory of Filter Amplifiers", 1930; S. Linkwitz, "Active Crossover Networks for Noncoincident Drivers", JAES 1976 |
| Plosive tamer | State-variable filter in the "topology-preserving transform" form | V. Zavalishin, *The Art of VA Filter Design* (free book) |
| Gate, compressor, de-esser, limiter, Auto Level | Envelope followers with attack/release, soft-knee gain computer | Standard dynamics design, e.g. D. Giannoulis, M. Massberg, J. Reiss, "Digital Dynamic Range Compressor Design: A Tutorial and Analysis", JAES 2012 |
| Reverb (`Effects.cpp`) | Input diffusion all-passes into a feedback delay network mixed by a Hadamard matrix | M. Schroeder, "Natural Sounding Artificial Reverberation", 1962; J.-M. Jot, A. Chaigne, "Digital Delay Networks for Designing Artificial Reverberators", AES 1991 |
| Warmth and Air | Waveshaping with oversampling (JUCE's polyphase IIR oversampler) | Standard; oversampling from JUCE (AGPL) |
| Pitch detection (Tune) | YIN: cumulative mean normalised difference function | A. de Cheveigné, H. Kawahara, "YIN, a fundamental frequency estimator for speech and music", JASA 2002 |
| Pitch shifting (Tune) | Pitch-synchronous overlap-add (PSOLA) with Hann-windowed grains | E. Moulines, F. Charpentier, "Pitch-synchronous waveform processing techniques for text-to-speech synthesis using diphones", Speech Communication 1990 |
| Spectrum display | FFT with a Hann window (JUCE's FFT) | Standard; FFT from JUCE (AGPL) |

The artwork (the neon cat, the logo, the cover and headers) is vector drawing written in this
project's code (`src/ui/NeonCat.cpp`, `tools/DevTools.cpp`); no images or fonts from elsewhere
are bundled.
