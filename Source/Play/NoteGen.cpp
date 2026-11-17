#include "NoteGen.h"

namespace idgs
{
// =============================================================================
// NoteSeqData
// =============================================================================
void NoteSeqData::clear()
{
    static const int motif[] { 0, 0, 12, 0, 7, 0, 3, 5, 0, 0, 12, 7, 0, 10, 7, 3 };
    for (int i = 0; i < MaxNoteSteps; ++i)
    {
        on[i] = i % 2 == 0 || i % 7 == 3;
        note[i] = motif[i % 16];
        vel[i] = i % 4 == 0 ? 1.0f : 0.75f;
        gate[i] = 0.6f;
        prob[i] = 1.0f;
        ratchet[i] = 1;
        slide[i] = false;
        lock[i] = false;
    }
    ++version;
}

juce::ValueTree NoteSeqData::toTree() const
{
    juce::ValueTree t ("NOTESEQ");
    juce::String steps;
    for (int i = 0; i < MaxNoteSteps; ++i)
        steps << (on[i].load() ? 1 : 0) << ":" << note[i].load() << ":" << juce::String (vel[i].load(), 3) << ":" << juce::String (gate[i].load(), 3)
              << ":" << juce::String (prob[i].load(), 3) << ":" << ratchet[i].load() << ":" << (slide[i].load() ? 1 : 0) << ":" << (lock[i].load() ? 1 : 0) << ";";
    t.setProperty ("steps", steps, nullptr);
    return t;
}

void NoteSeqData::fromTree (const juce::ValueTree& t)
{
    if (! t.isValid()) { clear(); return; }
    auto items = juce::StringArray::fromTokens (t.getProperty ("steps").toString(), ";", "");
    for (int i = 0; i < MaxNoteSteps && i < items.size(); ++i)
    {
        auto f = juce::StringArray::fromTokens (items[i], ":", "");
        if (f.size() < 8) continue;
        on[i] = f[0] == "1";
        note[i] = juce::jlimit (-36, 36, f[1].getIntValue());
        vel[i] = juce::jlimit (0.05f, 1.0f, f[2].getFloatValue());
        gate[i] = juce::jlimit (0.05f, 1.0f, f[3].getFloatValue());
        prob[i] = juce::jlimit (0.0f, 1.0f, f[4].getFloatValue());
        ratchet[i] = juce::jlimit (1, 4, f[5].getIntValue());
        slide[i] = f[6] == "1";
        lock[i] = f[7] == "1";
    }
    ++version;
}

// =============================================================================
// Générateur de patterns
// =============================================================================
void generatePattern (NoteSeqData& d, int len, int style, float density, int rangeOct, float jump, float accent,
                      const idg::Scale& scale, uint32_t seed)
{
    idg::dsp::Rng r;
    r.seed (seed);
    len = juce::jlimit (1, MaxNoteSteps, len);
    // degrés de gamme -> demi-tons (gamme chromatique si la gamme est éteinte : pentatonique pour rester musical)
    int degrees[64];
    int nd = 0;
    for (int semi = -12 * rangeOct; semi <= 12 * rangeOct && nd < 64; ++semi)
    {
        const int pc = ((semi % 12) + 12) % 12;
        const bool in = scale.enabled ? scale.contains ((pc + scale.key) % 12) : (pc == 0 || pc == 2 || pc == 4 || pc == 7 || pc == 9);
        if (in) degrees[nd++] = semi;
    }
    if (nd == 0) { degrees[0] = 0; nd = 1; }
    int zero = 0;
    for (int i = 0; i < nd; ++i) if (degrees[i] == 0) zero = i;

    // rythme
    bool hit[MaxNoteSteps] {};
    switch (style)
    {
        case GenEuclid:
        {
            const int k = juce::jlimit (1, len, (int) std::round (density * (float) len));
            const int rot = r.range (len);
            for (int i = 0; i < len; ++i) hit[(i + rot) % len] = ((i * k) % len) < k;
            break;
        }
        case GenPoly:
        {
            const int a = 3, b = 4 + r.range (2);
            for (int i = 0; i < len; ++i) hit[i] = (i % a == 0) || (i % b == 0 && r.uni() < density + 0.3f);
            break;
        }
        default:
            for (int i = 0; i < len; ++i) hit[i] = r.uni() < density;
            hit[0] = true;
            break;
    }
    // mélodie
    int pos = zero;
    for (int i = 0; i < len; ++i)
    {
        if (d.lock[i].load()) continue;
        int semi;
        switch (style)
        {
            case GenBroken:
            {
                static const int chord[] { 0, 2, 4, 7, 4, 2 };
                const int deg = chord[i % 6] + (r.uni() < jump ? 7 * (r.uni() < 0.5f ? -1 : 1) : 0);
                semi = degrees[juce::jlimit (0, nd - 1, zero + deg)];
                break;
            }
            default:
            {
                const int stepMax = r.uni() < jump ? 5 : 2;
                pos = juce::jlimit (0, nd - 1, pos + r.range (2 * stepMax + 1) - stepMax);
                semi = degrees[pos];
                break;
            }
        }
        d.on[i] = hit[i];
        d.note[i] = semi;
        d.vel[i] = juce::jlimit (0.1f, 1.0f, (i % 4 == 0 ? 1.0f : 1.0f - accent * 0.6f) + r.bi() * 0.1f);
        d.gate[i] = 0.3f + r.uni() * 0.6f;
        d.prob[i] = r.uni() < 0.15f ? 0.6f : 1.0f;
        d.ratchet[i] = r.uni() < 0.08f ? 2 : 1;
        d.slide[i] = r.uni() < 0.1f;
    }
    ++d.version;
}

// =============================================================================
// NoteGen
// =============================================================================
void NoteGen::prepare (double sampleRate)
{
    sr = sampleRate;
    reset();
}

void NoteGen::reset()
{
    numHeld = numLatched = numPend = 0;
    for (auto& s : sounding) s = 0;
    sprayActive = 0;
    lastStep = INT64_MIN;
    arpIndex = 0;
    cycles = 0;
}

void NoteGen::schedule (int delay, int note, float vel, bool on, bool spray)
{
    if (numPend >= (int) std::size (pend)) return;
    pend[numPend++] = { juce::jmax (0, delay), note, vel, on, spray };
}

void NoteGen::allOff (juce::MidiBuffer& out, int pos)
{
    for (int n = 0; n < 128; ++n)
        if (sounding[n] > 0) { out.addEvent (juce::MidiMessage::noteOff (1, n), pos); sounding[n] = 0; }
    numPend = 0;
    sprayActive = 0;
}

void NoteGen::noteOnWithSpray (juce::MidiBuffer& out, int pos, int note, float vel, int lenSamples, const PlaySettings& s, idg::dsp::Rng& rng)
{
    note = quantize (note, s);
    out.addEvent (juce::MidiMessage::noteOn (1, note, juce::jlimit (0.01f, 1.0f, vel)), pos);
    ++sounding[note];
    if (lenSamples > 0) schedule (pos + lenSamples, note, 0.0f, false);
    if (! s.sprayOn) return;
    // Note Spray : nuage de micro-notes autour de la note
    for (int k = 0; k < s.sprayDensity; ++k)
    {
        if (sprayActive >= s.sprayMax) break;
        const int d = pos + (int) (rng.uni() * s.sprayTime * 0.001f * (float) sr);
        const int pn = quantize (note + (int) std::round (rng.bi() * (float) s.sprayRange), s);
        const float v = juce::jlimit (0.05f, 1.0f, vel * (1.0f - s.sprayVel * rng.uni()));
        schedule (d, pn, v, true, true);
        schedule (d + (int) (s.sprayDur * 0.001f * (float) sr), pn, 0.0f, false, true);
        ++sprayActive;
    }
}

void NoteGen::endOfCycle (const PlaySettings& s, NoteSeqData& seq, idg::dsp::Rng& rng)
{
    ++cycles;
    const int len = juce::jlimit (1, MaxNoteSteps, s.seqLen);
    if (s.seqMutate > 0.0f)
    {
        // mutation progressive (les pas verrouillés ne bougent pas)
        for (int i = 0; i < len; ++i)
        {
            if (seq.lock[i].load()) continue;
            if (rng.uni() < s.seqMutate * 0.35f) seq.note[i] = juce::jlimit (-36, 36, seq.note[i].load() + (rng.uni() < 0.5f ? -1 : 1) * (1 + rng.range (3)));
            if (rng.uni() < s.seqMutate * 0.12f) seq.on[i] = ! seq.on[i].load();
            if (rng.uni() < s.seqMutate * 0.2f) seq.vel[i] = juce::jlimit (0.1f, 1.0f, seq.vel[i].load() + rng.bi() * 0.2f);
        }
        ++seq.version;
    }
    static const int autoCycles[] { 0, 1, 2, 4, 8 };
    const int every = autoCycles[juce::jlimit (0, 4, s.genAuto)];
    if (s.mode == PlayGen && every > 0 && cycles % every == 0)
    {
        const int g = ++genCounter;
        generatePattern (seq, len, s.genStyle, s.genDensity, s.genRange, s.genJump, s.genAccent, s.scale, (uint32_t) g * 2654435761u);
    }
}

void NoteGen::triggerStep (juce::MidiBuffer& out, int pos, int64_t step, double stepSamples, const PlaySettings& s, NoteSeqData& seq, idg::dsp::Rng& rng)
{
    if (s.mode == PlayArp)
    {
        const int* src = numHeld > 0 ? held : latched;
        const int count = numHeld > 0 ? numHeld : (s.arpHold ? numLatched : 0);
        if (count == 0) return;
        // liste des notes de l'arpège (triées sauf « ordre joué »), sur N octaves
        int notes[128];
        int nn = 0;
        int base[32];
        for (int i = 0; i < count; ++i) base[i] = src[i];
        if (s.arpMode != ArpPlayed) std::sort (base, base + count);
        for (int o = 0; o < s.arpOct; ++o)
            for (int i = 0; i < count && nn < 128; ++i) notes[nn++] = base[i] + 12 * o;
        int idx;
        switch (s.arpMode)
        {
            case ArpDown: idx = nn - 1 - (arpIndex % nn); break;
            case ArpUpDown: { const int period = juce::jmax (1, 2 * nn - 2); const int k = arpIndex % period; idx = k < nn ? k : period - k; break; }
            case ArpRandom: idx = rng.range (nn); break;
            default: idx = arpIndex % nn; break;
        }
        ++arpIndex;
        const int note = notes[juce::jlimit (0, nn - 1, idx)];
        noteOnWithSpray (out, pos, note, heldVel[juce::jlimit (0, 127, base[0])] > 0.0f ? heldVel[base[0]] : 0.8f,
                         juce::jmax (1, (int) (stepSamples * s.arpGate)), s, rng);
        return;
    }

    // séquenceur / générateur
    const int len = juce::jlimit (1, MaxNoteSteps, s.seqLen);
    const int idx = (int) (((step % len) + len) % len);
    seq.current.store (idx, std::memory_order_relaxed);
    int baseNote = 60;
    if (s.seqFollow == 0)
    {
        if (numHeld == 0) { if (idx == len - 1) endOfCycle (s, seq, rng); return; }
        baseNote = held[numHeld - 1];
    }
    if (seq.on[idx].load() && rng.uni() < seq.prob[idx].load())
    {
        int note = baseNote + seq.note[idx].load();
        if (s.rndNotes > 0.0f && rng.uni() < s.rndNotes) note = baseNote + (int) std::round (rng.bi() * 12.0f);
        if (s.rndOct > 0.0f && rng.uni() < s.rndOct) note += rng.uni() < 0.5f ? -12 : 12;
        int ratchets = seq.ratchet[idx].load();
        if (s.rndRatchet > 0.0f && rng.uni() < s.rndRatchet) ratchets = 2 + rng.range (3);
        ratchets = juce::jlimit (1, 4, ratchets);
        const double sub = stepSamples / ratchets;
        const bool slide = seq.slide[idx].load();
        for (int r = 0; r < ratchets; ++r)
        {
            const int at = pos + (int) (r * sub);
            const int len2 = slide && r == ratchets - 1 ? (int) (sub * 1.05) : juce::jmax (1, (int) (sub * seq.gate[idx].load()));
            if (r == 0) noteOnWithSpray (out, at, note, seq.vel[idx].load(), len2, s, rng);
            else schedule (at, quantize (note, s), seq.vel[idx].load(), true), schedule (at + len2, quantize (note, s), 0.0f, false);
        }
    }
    if (idx == len - 1) endOfCycle (s, seq, rng);
}

void NoteGen::process (const juce::MidiBuffer& in, juce::MidiBuffer& out, int n, double ppqStart, double bpm, bool hostPlaying,
                       const PlaySettings& s, NoteSeqData& seq, idg::dsp::Rng& rng)
{
    out.clear();
    if (s.mode != lastMode)
    {
        allOff (out, 0);
        lastMode = s.mode;
        lastStep = INT64_MIN;
        arpIndex = 0;
        numLatched = 0;
    }
    const bool generative = s.mode != PlayDirect;

    // ---- MIDI entrant
    for (const auto meta : in)
    {
        const auto m = meta.getMessage();
        const int pos = meta.samplePosition;
        if (m.isNoteOn())
        {
            const int note = m.getNoteNumber();
            heldVel[note] = m.getFloatVelocity();
            bool found = false;
            for (int i = 0; i < numHeld; ++i) found |= held[i] == note;
            if (! found && numHeld < 32) held[numHeld++] = note;
            if (s.arpHold && latchArmed) { numLatched = 0; latchArmed = false; }
            if (s.arpHold) { bool f2 = false; for (int i = 0; i < numLatched; ++i) f2 |= latched[i] == note; if (! f2 && numLatched < 32) latched[numLatched++] = note; }
            if (numHeld == 1 && ! hostPlaying) { clockPpq = 0.0; lastStep = INT64_MIN; arpIndex = 0; } // horloge interne : repart à la première note
            if (! generative) noteOnWithSpray (out, pos, note, m.getFloatVelocity(), 0, s, rng);
            // mémorise l'accord (export MIDI de l'arpège)
            lastChordSize = juce::jmin (8, numHeld);
            for (int i = 0; i < juce::jmin (8, numHeld); ++i) lastChord[i] = held[i];
        }
        else if (m.isNoteOff())
        {
            const int note = m.getNoteNumber();
            for (int i = 0; i < numHeld; ++i)
                if (held[i] == note) { for (int j = i; j < numHeld - 1; ++j) held[j] = held[j + 1]; --numHeld; break; }
            if (numHeld == 0) latchArmed = true;
            if (! generative)
            {
                const int q = quantize (note, s);
                out.addEvent (juce::MidiMessage::noteOff (1, q), pos);
                if (sounding[q] > 0) --sounding[q];
            }
            else if (s.mode == PlayArp && numHeld == 0 && ! s.arpHold) arpIndex = 0;
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            numHeld = numLatched = 0;
            allOff (out, pos);
            out.addEvent (m, pos);
        }
        else out.addEvent (m, pos);
    }

    // ---- horloge et pas (arpège / séquenceur / générateur)
    const double bps = bpm / 60.0 / sr;
    const double ppq0 = hostPlaying ? ppqStart : clockPpq;
    if (generative)
    {
        const double stepBeats = divisionBeats (s.mode == PlayArp ? s.arpRate : s.seqRate);
        const float swing = s.mode == PlayArp ? s.arpSwing : s.seqSwing;
        const double stepSamples = stepBeats / bps;
        auto stepStart = [&] (int64_t k) { return (double) k * stepBeats + ((k & 1) ? swing * stepBeats * 0.33 : 0.0); };
        const double ppqEnd = ppq0 + (double) n * bps;
        int64_t k = (int64_t) std::floor (ppq0 / stepBeats) - 1;
        for (; stepStart (k) < ppqEnd; ++k)
        {
            const double st = stepStart (k);
            if (st < ppq0 || k <= lastStep) continue;
            lastStep = k;
            const int pos = juce::jlimit (0, n - 1, (int) ((st - ppq0) / bps));
            triggerStep (out, pos, k, stepSamples, s, seq, rng);
        }
    }
    clockPpq = ppq0 + (double) n * bps;

    // ---- événements planifiés
    int w = 0;
    for (int i = 0; i < numPend; ++i)
    {
        auto e = pend[i];
        if (e.delay < n)
        {
            if (e.on)
            {
                out.addEvent (juce::MidiMessage::noteOn (1, e.note, juce::jlimit (0.01f, 1.0f, e.vel)), e.delay);
                ++sounding[e.note];
            }
            else
            {
                if (sounding[e.note] > 0)
                {
                    out.addEvent (juce::MidiMessage::noteOff (1, e.note), e.delay);
                    --sounding[e.note];
                }
                if (e.spray && sprayActive > 0) --sprayActive;
            }
        }
        else
        {
            e.delay -= n;
            pend[w++] = e;
        }
    }
    numPend = w;
}
} // namespace idgs
