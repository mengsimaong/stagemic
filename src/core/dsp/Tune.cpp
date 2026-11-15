// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/dsp/Tune.h"

#include "core/Parameters.h"

namespace stagemic::dsp
{

namespace
{
inline float periodToMidi(float period, double sr) noexcept
{
    return 69.0f + 12.0f * std::log2((float)sr / (period * 440.0f));
}
} // namespace

Tune::Tune(juce::AudioProcessorValueTreeState& s)
    : keyParam(param(s, pid::tuneKey))
    , scaleParam(param(s, pid::tuneScale))
    , speedParam(param(s, pid::tuneSpeed))
    , amountParam(param(s, pid::tuneAmount))
    , formantParam(param(s, pid::tuneFormant))
    , transposeParam(param(s, pid::tuneTranspose))
    , voiceParam(param(s, pid::tuneVoice))
{
    for (int i = 0; i < kHistory; ++i)
    {
        history.input[(size_t)i].store(-1.0f);
        history.output[(size_t)i].store(-1.0f);
    }
}

int Tune::nearestNote(float midi, int keyIndex, uint16_t scaleMask) noexcept
{
    const int base = (int)std::floor(midi);
    int best = base;
    float bestDistance = 1.0e9f;
    for (int n = base - 6; n <= base + 7; ++n)
    {
        const int pitchClass = ((n - keyIndex) % 12 + 12) % 12;
        if ((scaleMask & (1 << pitchClass)) == 0)
            continue;
        const float d = std::abs(midi - (float)n);
        if (d < bestDistance)
        {
            bestDistance = d;
            best = n;
        }
    }
    return best;
}

void Tune::prepare(double sampleRate, int numChannels)
{
    sr = sampleRate;
    numCh = std::clamp(numChannels, 1, kMaxChannels);
    delay = (int)std::lround(latencyMs * 0.001 * sr);
    maxPeriod = (int)(sr / 80.0);
    minPeriod = (int)(sr / 1000.0);

    decimation = std::max(1, (int)std::lround(sr / 12000.0));
    const double decRate = sr / decimation;
    decTauMin = std::max(2, (int)(decRate / 1000.0));
    decTauMax = std::min(kDecRing - kWindow - 2, (int)(decRate / 80.0) + 1);
    aa1.c = biquad::lowpass(sr, decRate * 0.21, 0.54119610);
    aa2.c = biquad::lowpass(sr, decRate * 0.21, 1.30656296);

    for (int c = 0; c < kMaxChannels; ++c)
    {
        in[c].assign(kRing, 0.0f);
        out[c].assign(kRing, 0.0f);
    }
    weight.assign(kRing, 0.0f);
    mono.assign(kRing, 0.0f);
    lowMono.assign(kRing, 0.0f);
    dec.assign(kDecRing, 0.0f);
    yin.assign((size_t)decTauMax + 2, 0.0f);
    reset();
}

void Tune::reset() noexcept
{
    for (int c = 0; c < kMaxChannels; ++c)
    {
        std::fill(in[c].begin(), in[c].end(), 0.0f);
        std::fill(out[c].begin(), out[c].end(), 0.0f);
    }
    std::fill(weight.begin(), weight.end(), 0.0f);
    std::fill(mono.begin(), mono.end(), 0.0f);
    std::fill(lowMono.begin(), lowMono.end(), 0.0f);
    std::fill(dec.begin(), dec.end(), 0.0f);
    aa1.reset();
    aa2.reset();
    now = 0;
    decimPhase = hopCounter = 0;
    decWritten = 0;
    track.fill({});
    trackWritten = 0;
    confirmed = pending = 0.0f;
    misses = 0;
    marksWritten = 0;
    lastMarkPos = 0;
    nextGrain = 0.0;
    readOffset = 0.0;
    lastGrainVoiced = false;
    correction = 0.0f;
    currentNote = -1;
    lastInputMidi = lastOutputMidi = -1.0f;
    historyCounter = 0;
}

void Tune::update(bool) noexcept
{
    key = std::clamp((int)std::lround(load(keyParam)), 0, 11);
    scale = std::clamp((int)std::lround(load(scaleParam)), 0, (int)scaleMasks.size() - 1);
    speedMs = std::max(0.0f, load(speedParam));
    amount = std::clamp(load(amountParam) * 0.01f, 0.0f, 1.0f);
    formantRatio = std::pow(2.0f, std::clamp(load(formantParam), -5.0f, 5.0f) / 12.0f);
    transpose = std::clamp(std::round(load(transposeParam)), -12.0f, 12.0f);
    // Voice type: the range the detector looks in. Keeping it to the singer's range stops
    // vocal fry and breath (which can show half the pitch) from reading an octave low.
    switch (std::clamp((int)std::lround(load(voiceParam)), 0, 2))
    {
        case 0: voiceMinHz = 200.0; voiceMaxHz = 1100.0; break; // High (female)
        case 2: voiceMinHz = 80.0;  voiceMaxHz = 500.0;  break; // Low (male)
        default: voiceMinHz = 100.0; voiceMaxHz = 800.0; break; // Mid
    }
}

float Tune::inputAt(int c, double t) const noexcept
{
    const double f = std::floor(t);
    const auto i = (int64_t)f;
    const float frac = (float)(t - f);
    const float a = in[c][(size_t)(i & mask)], b = in[c][(size_t)((i + 1) & mask)];
    return a + frac * (b - a);
}

//==============================================================================
// Pitch detection: YIN on the downsampled voice, refined at full rate.
void Tune::detect() noexcept
{
    const int n = kWindow + decTauMax;
    if (decWritten < n)
        return;
    const int64_t start = decWritten - n;
    auto d = [&](int k) { return dec[(size_t)((start + k) & (kDecRing - 1))]; };

    float energy = 0.0f;
    for (int j = 0; j < kWindow; ++j)
        energy += d(j) * d(j);
    // Only trust a window filled by a steady voice: at a note's start (part silence,
    // part a fast-growing voice) this kind of detector easily reads an octave low.
    float older = 0.0f, newer = 0.0f;
    for (int j = 0; j < n / 2; ++j)
        older += d(j) * d(j);
    for (int j = n / 2; j < n; ++j)
        newer += d(j) * d(j);
    const bool steady = newer < 6.0f * older && older < 6.0f * newer;
    const bool loudEnough = energy / kWindow > 1.0e-6f && steady; // about -60 dBFS

    float period = 0.0f;
    bool voiced = false;
    if (loudEnough)
    {
        // Cumulative mean normalised difference.
        yin[0] = 1.0f;
        float running = 0.0f;
        for (int tau = 1; tau <= decTauMax; ++tau)
        {
            float diff = 0.0f;
            for (int j = 0; j < kWindow; ++j)
            {
                const float e = d(j) - d(j + tau);
                diff += e * e;
            }
            running += diff;
            yin[(size_t)tau] = running > 0.0f ? diff * (float)tau / running : 1.0f;
        }

        // Search only the chosen voice type's range (Voice: High / Mid / Low).
        const double decRate = sr / decimation;
        const int searchMin = std::max(decTauMin, (int)(decRate / voiceMaxHz));
        const int searchMax = std::min(decTauMax, (int)(decRate / voiceMinHz) + 1);
        int best = -1;
        for (int tau = searchMin; tau < searchMax; ++tau)
            if (yin[(size_t)tau] < 0.15f)
            {
                while (tau + 1 < searchMax && yin[(size_t)tau + 1] < yin[(size_t)tau])
                    ++tau;
                best = tau;
                break;
            }
        if (best < 0)
        {
            // No clear dip: accept the deepest one only if it's still fairly clear.
            int deepest = searchMin;
            for (int tau = searchMin; tau < searchMax; ++tau)
                if (yin[(size_t)tau] < yin[(size_t)deepest])
                    deepest = tau;
            if (yin[(size_t)deepest] < 0.3f)
                best = deepest;
        }

        // Octave check: if half this cycle length also fits nearly as well, the voice is
        // really an octave higher (typical in the first moment of a note).
        // (Repeated, for a reading two octaves low. A genuinely low voice doesn't fit at
        // half its cycle length at all, so this only catches real mistakes.)
        while (best > 2 * searchMin)
        {
            const int halfTau = (best + 1) / 2;
            int bestHalf = halfTau;
            for (int t = halfTau - 1; t <= halfTau + 1; ++t)
                if (t > searchMin && t < searchMax && yin[(size_t)t] < yin[(size_t)bestHalf])
                    bestHalf = t;
            if (yin[(size_t)bestHalf] < 0.35f && yin[(size_t)bestHalf] < yin[(size_t)best] + 0.25f)
                best = bestHalf;
            else
                break;
        }

        if (best > decTauMin && best < decTauMax)
        {
            const float y0 = yin[(size_t)best - 1], y1 = yin[(size_t)best], y2 = yin[(size_t)best + 1];
            const float denom = y0 - 2.0f * y1 + y2;
            const float coarse = ((float)best + (std::abs(denom) > 1.0e-9f ? 0.5f * (y0 - y2) / denom : 0.0f)) * (float)decimation;

            // Refine at full rate around the coarse estimate.
            const int window = kWindow * decimation;
            const int lo = std::max(minPeriod, (int)coarse - decimation - 1);
            const int hi = std::min(maxPeriod + decimation, (int)coarse + decimation + 2);
            const int64_t end = now - 1;
            auto rawDiff = [&](int tau) {
                float diff = 0.0f;
                for (int j = 0; j < window; ++j)
                {
                    const float e = lowMono[(size_t)((end - j) & mask)] - lowMono[(size_t)((end - j - tau) & mask)];
                    diff += e * e;
                }
                return diff;
            };
            int bestTau = lo;
            float bestDiff = 1.0e30f, below = 0.0f, above = 0.0f;
            float prev = rawDiff(lo - 1);
            for (int tau = lo; tau <= hi; ++tau)
            {
                const float cur = rawDiff(tau);
                if (cur < bestDiff)
                {
                    bestDiff = cur;
                    bestTau = tau;
                    below = prev;
                }
                prev = cur;
            }
            above = rawDiff(bestTau + 1);
            const float denom2 = below - 2.0f * bestDiff + above;
            period = (float)bestTau + (std::abs(denom2) > 1.0e-12f ? 0.5f * (below - above) / denom2 : 0.0f);
            voiced = period >= (float)(sr / voiceMaxHz) && period <= (float)(sr / voiceMinHz);
        }
    }

    // A single odd reading (a note's first moment, half of two notes in the window,
    // a consonant) must never become a note. A new pitch counts only once two
    // readings in a row agree on it; meanwhile the previous note is held.
    auto semitonesApart = [](float p1, float p2) { return std::abs(12.0f * std::log2(p1 / p2)); };
    // How long to hold through unclear readings: about 5 ms normally. With a one-note
    // scale (the Robot voice) about 55 ms, so breathy or rough moments keep the robot
    // drone instead of letting the real voice through (true silence still ends it).
    const auto mask = scaleMasks[(size_t)scale];
    const bool oneNote = mask != 0 && (mask & (mask - 1)) == 0;
    const int maxMisses = oneNote ? 20 : 2;
    if (!voiced && confirmed > 0.0f && misses < maxMisses)
    {
        // Unclear readings inside a sung note (a breathy moment): hold the
        // note instead of flickering between "voice" and "breath".
        ++misses;
        period = confirmed;
        voiced = true;
    }
    else if (!voiced)
    {
        confirmed = pending = 0.0f;
        misses = 0;
    }
    else if (confirmed > 0.0f && semitonesApart(period, confirmed) < 0.8f)
    {
        confirmed = period; // smooth movement: vibrato, slides
        pending = 0.0f;
        misses = 0;
    }
    else if (pending > 0.0f && semitonesApart(period, pending) < 0.3f)
    {
        confirmed = period; // a jump, now confirmed
        pending = 0.0f;
    }
    else
    {
        pending = period;
        if (confirmed > 0.0f)
            period = confirmed; // hold the previous note for now
        else
            voiced = false;     // not started yet
    }

    // The window describes roughly its own middle.
    auto& point = track[(size_t)(trackWritten % kTrack)];
    point.time = now - (int64_t)(kWindow * decimation) / 2 - (int64_t)period / 2;
    point.period = period;
    point.voiced = voiced;
    ++trackWritten;
}

float Tune::periodAt(int64_t t, bool& voiced) const noexcept
{
    voiced = false;
    if (trackWritten == 0)
        return 0.0f;
    const int newest = trackWritten - 1, oldest = std::max(0, trackWritten - kTrack);
    for (int k = newest; k >= oldest; --k)
    {
        const auto& p = track[(size_t)(k % kTrack)];
        if (p.time <= t || k == oldest)
        {
            voiced = p.voiced;
            return p.period;
        }
    }
    return 0.0f;
}

//==============================================================================
// Analysis marks: one per vocal cycle, on a consistent point of the wave.
void Tune::addMarks() noexcept
{
    const int unvoicedHop = std::max(16, (int)(sr * 0.005));
    for (;;)
    {
        bool voiced = false;
        float period = periodAt(lastMarkPos, voiced);
        if (!voiced)
            period = (float)unvoicedHop;
        const int step = std::max(1, (int)std::lround(period));
        const int search = voiced ? step / 4 : 0;
        const int half = step / 2;
        const int64_t predicted = lastMarkPos + step;
        if (predicted + search + half > now - 1)
            break;

        int64_t pos = predicted;
        double exact = (double)predicted;
        const bool previousVoiced = marksWritten > 0 && marks[(size_t)((marksWritten - 1) % kMarks)].voiced;
        if (voiced && previousVoiced)
        {
            // Find where this cycle best matches the previous one, so the marks sit on
            // the same point of every cycle. (Jumping to the biggest peak instead lets
            // marks hop between two similar peaks on a real voice; the grains then
            // overlap out of step, partly cancel, and crackle.)
            auto similarity = [&](int64_t t) {
                float xy = 0.0f, xx = 0.0f, yy = 0.0f;
                for (int j = -half; j <= half; ++j)
                {
                    const float x = lowMono[(size_t)((lastMarkPos + j) & mask)];
                    const float y = lowMono[(size_t)((t + j) & mask)];
                    xy += x * y;
                    xx += x * x;
                    yy += y * y;
                }
                return xy / std::sqrt(xx * yy + 1.0e-20f);
            };
            float bestScore = -2.0f;
            for (int64_t t = predicted - search; t <= predicted + search; ++t)
                if (const float score = similarity(t); score > bestScore)
                {
                    bestScore = score;
                    pos = t;
                }
            // Sub-sample refinement (a cycle is only ~55 samples at A5, so whole samples
            // would be up to 30 cents off).
            const float y0 = similarity(pos - 1), y2 = similarity(pos + 1);
            const float denom = y0 - 2.0f * bestScore + y2;
            const double frac = std::abs(denom) > 1.0e-9f ? std::clamp(0.5 * (y0 - y2) / denom, -0.5, 0.5) : 0.0;
            const double previousExact = marks[(size_t)((marksWritten - 1) % kMarks)].exact;
            exact = previousExact + (double)(pos - lastMarkPos) + frac;
            pos = (int64_t)std::llround(exact);
        }
        else if (voiced)
        {
            // First cycle of a phrase: anchor on the strongest peak.
            float best = -1.0e30f;
            for (int64_t t = predicted - search; t <= predicted + search; ++t)
                if (const float v = lowMono[(size_t)(t & mask)]; v > best)
                {
                    best = v;
                    pos = t;
                }
        }
        if (!(voiced && previousVoiced))
            exact = (double)pos;
        marks[(size_t)(marksWritten % kMarks)] = {pos, exact, period, voiced};
        ++marksWritten;
        lastMarkPos = pos;
    }
}

int Tune::markFor(double target) const noexcept
{
    // The analysis mark nearest the target whose whole grain has already been heard.
    int best = -1;
    double bestDistance = 1.0e18;
    const int oldest = std::max(0, marksWritten - kMarks + 1);
    for (int k = marksWritten - 1; k >= oldest; --k)
    {
        const auto& m = marks[(size_t)(k % kMarks)];
        if ((double)m.pos < target - 2.0 * maxPeriod)
            break;
        const float reach = m.voiced ? 1.6f * m.period * std::max(1.0f, formantRatio) : 0.0f;
        if ((double)m.pos + reach + 2.0 > (double)now)
            continue;
        const double distance = std::abs((double)m.pos - target);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = k;
        }
    }
    return best;
}

void Tune::addGrain(double centre, double source, int left, int right, float readScale) noexcept
{
    // A Hann-shaped grain whose two halves can differ in length (the distance to the
    // previous and to the next vocal cycle), so neighbouring grains add up to exactly 1.
    // Positions are exact (sub-sample): when nothing is shifted, the grain reads
    // exactly the samples it writes, with no interpolation blur.
    const int64_t outputPos = now - 1 - delay;
    const auto first = (int64_t)std::ceil(centre - (double)left), last = (int64_t)std::floor(centre + (double)right);
    for (int64_t t = first; t <= last; ++t)
    {
        if (t <= outputPos)
            continue; // already played
        const double offset = (double)t - centre;
        const double half = offset < 0.0 ? (double)left : (double)right;
        if (std::abs(offset) >= half)
            continue;
        const float w = 0.5f + 0.5f * std::cos((float)(kPi * offset / half));
        for (int c = 0; c < numCh; ++c)
            out[c][(size_t)(t & mask)] += w * inputAt(c, source + offset * readScale);
        weight[(size_t)(t & mask)] += w;
    }
}

void Tune::emitGrain() noexcept
{
    const int unvoicedHalf = std::max(16, (int)(sr * 0.005));
    const int k = markFor(nextGrain);
    const Mark* m = k >= 0 ? &marks[(size_t)(k % kMarks)] : nullptr;

    // A "voiced" cycle that is essentially silent (a note cut off, the detector still
    // holding on) is treated as silence: nothing to tune there.
    bool audible = false;
    if (m != nullptr && m->voiced)
        for (int64_t t = m->pos - (int64_t)m->period; t <= m->pos + (int64_t)m->period && !audible; ++t)
            audible = std::abs(monoAt(t)) > 1.0e-3f;

    if (m == nullptr || !m->voiced || !audible)
    {
        // Breath, "s", silence: rebuild the voice as it is, continuing from wherever the
        // last voiced grain read (so there's no jump), easing back to "no offset".
        // With a formant shift (the fun voices), these get the same shift as the sung
        // parts, so an "s" or a breath sounds like it comes from the same changed voice
        // instead of the original one. At 0 (every other preset) it's an exact copy.
        const double source = nextGrain + readOffset;
        float scale = formantRatio;
        if (scale > 1.0f) // reading faster reaches further ahead: never past what's been heard
            scale = std::max(1.0f, std::min(scale, (float)(((double)(now - 2) - source) / (double)unvoicedHalf)));
        addGrain(nextGrain, source, unvoicedHalf, unvoicedHalf, scale);
        readOffset -= std::clamp(readOffset, -0.1 * unvoicedHalf, 0.1 * unvoicedHalf);
        nextGrain += unvoicedHalf;
        lastGrainVoiced = false;
        lastInputMidi = lastOutputMidi = -1.0f;
        return;
    }

    // Spacing of her actual cycles around this one (falls back to the detected period).
    auto voicedMark = [&](int index) -> const Mark* {
        if (index < std::max(0, marksWritten - kMarks + 1) || index >= marksWritten)
            return nullptr;
        const auto* mk = &marks[(size_t)(index % kMarks)];
        return mk->voiced ? mk : nullptr;
    };
    const Mark* prev = voicedMark(k - 1);
    const Mark* next = voicedMark(k + 1);
    const float period = m->period;
    auto plausible = [&](double d) { return d > 0.5 * period && d < 1.5 * period; };
    const double before = prev != nullptr && plausible(m->exact - prev->exact) ? m->exact - prev->exact : period;
    const double after = next != nullptr && plausible(next->exact - m->exact) ? next->exact - m->exact : period;

    // A phrase starts exactly on her first cycle (with no offset jump from the breath before).
    if (!lastGrainVoiced && m->exact - readOffset - before > (double)(now - 1 - delay) + 1.0)
        nextGrain = m->exact - readOffset;
    readOffset = m->exact - nextGrain;

    // Her actual cycle length here (what the grains are spaced by), not just the
    // detector's estimate, so the correction lands exactly on the note.
    // (The detector decides when they disagree: at a note jump the spacing is unreliable.)
    const double spacing = 0.5 * (before + after);
    const bool agree = std::abs(12.0 * std::log2(spacing / (double)period)) < 0.5;
    const float inputMidi = periodToMidi((float)(agree ? spacing : (double)period), sr);

    // Which note to aim for (with a little hysteresis so it doesn't flip between two).
    const int candidate = nearestNote(inputMidi, key, scaleMasks[(size_t)scale]);
    if (currentNote < 0 || !lastGrainVoiced
        || (candidate != currentNote && std::abs(inputMidi - (float)currentNote) > std::abs(inputMidi - (float)candidate) + 0.1f))
        currentNote = candidate;

    // Each phrase starts from the sung pitch and glides in at the set speed
    // (instantly at 0 ms), like a classic hardware tuner.
    const float wanted = ((float)currentNote - inputMidi) * amount;
    if (!lastGrainVoiced)
        correction = 0.0f;
    if (speedMs < 0.5f)
        correction = wanted;
    else
        correction += (wanted - correction) * (1.0f - std::exp(-((float)after / (float)sr) * 1000.0f / speedMs));

    const float shift = std::clamp(correction + transpose, -12.0f, 12.0f);
    const float ratio = std::pow(2.0f, shift / 12.0f);

    addGrain(nextGrain, m->exact, std::max(2, (int)std::ceil(before)), std::max(2, (int)std::ceil(after)), formantRatio);
    nextGrain += std::max(1.0, after / ratio);
    lastGrainVoiced = true;
    lastInputMidi = inputMidi;
    lastOutputMidi = inputMidi + shift;
}

//==============================================================================
void Tune::process(float* const* ch, int numChannels, int n) noexcept
{
    numChannels = std::min(numChannels, numCh);
    const float norm = 1.0f / (float)std::max(1, numChannels);

    for (int i = 0; i < n; ++i)
    {
        const auto w = (size_t)(now & mask);
        float m = 0.0f;
        for (int c = 0; c < numChannels; ++c)
        {
            in[c][w] = ch[c][i];
            m += ch[c][i];
        }
        m *= norm;
        mono[w] = m;
        lowMono[w] = aa2.process(aa1.process(m, 0), 0);
        ++now;

        if (++decimPhase >= decimation)
        {
            decimPhase = 0;
            dec[(size_t)(decWritten & (kDecRing - 1))] = lowMono[w];
            ++decWritten;
            if (++hopCounter >= kHop)
            {
                hopCounter = 0;
                detect();
            }
        }
        addMarks();

        // Lay down every grain that starts by the sample about to be played.
        const int64_t outputPos = now - 1 - delay;
        for (int guard = 0; guard < 8; ++guard)
        {
            const int k2 = markFor(nextGrain);
            const Mark* m2 = k2 >= 0 ? &marks[(size_t)(k2 % kMarks)] : nullptr;
            // Voiced grains are laid down a little early, so a phrase's first grain can
            // still be lined up exactly on her first cycle.
            // Breath grains are laid down 10 ms early too, so when a phrase starts there is
            // always room to line its first grain up with her first cycle.
            const double half = (m2 != nullptr && m2->voiced) ? 2.5 * m2->period : sr * 0.010;
            if (nextGrain - half > (double)outputPos + 1.0)
                break;
            emitGrain();
        }

        if (outputPos < 0)
        {
            for (int c = 0; c < numChannels; ++c)
                ch[c][i] = 0.0f;
            continue;
        }
        const auto r = (size_t)(outputPos & mask);
        const float g = 1.0f / std::max(weight[r], 0.35f);
        for (int c = 0; c < numChannels; ++c)
        {
            ch[c][i] = out[c][r] * g;
            out[c][r] = 0.0f;
        }
        weight[r] = 0.0f;

        if (++historyCounter >= 256)
        {
            historyCounter = 0;
            const int k = history.written.load(std::memory_order_relaxed);
            history.input[(size_t)(k % kHistory)].store(lastGrainVoiced ? lastInputMidi : -1.0f, std::memory_order_relaxed);
            history.output[(size_t)(k % kHistory)].store(lastGrainVoiced ? lastOutputMidi : -1.0f, std::memory_order_relaxed);
            history.written.store(k + 1, std::memory_order_release);
        }
    }
}

} // namespace stagemic::dsp
