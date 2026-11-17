#include "ModEngine.h"

namespace idgs
{
void ModGlobal::init (juce::AudioProcessorValueTreeState& apvts, ModShared& s, const ModTargets& t)
{
    shared = &s;
    targets = &t;
    auto r = [&apvts] (const juce::String& id) { auto* v = apvts.getRawParameterValue (id); jassert (v != nullptr); return v; };
    for (int i = 0; i < NumLfos; ++i)
        lr[i] = { r (lfoId (i, "rate")), r (lfoId (i, "sync")), r (lfoId (i, "div")), r (lfoId (i, "mode")),
                  r (lfoId (i, "phase")), r (lfoId (i, "fade")), r (lfoId (i, "smooth")), r (lfoId (i, "stereo")) };
    for (int i = 0; i < NumRandMods; ++i)
        rr[i] = { r (rndId (i, "rate")), r (rndId (i, "sync")), r (rndId (i, "div")), r (rndId (i, "style")), r (rndId (i, "retrig")) };
    static const char* ek[9] { "delay", "attack", "hold", "decay", "sustain", "release", "acurve", "dcurve", "rcurve" };
    for (int e = 0; e < NumEnvelopes; ++e)
        for (int k = 0; k < 9; ++k) er[e].v[k] = r (envId (e, ek[k]));
    for (int m = 0; m < NumMacros; ++m) macros[m] = r (macroId (m));
    driftRaw = r ("rand_drift");
    driftSpeedRaw = r ("rand_drift_speed");
    chaosRaw = r (macroId (NumMacros - 1));
}

void ModGlobal::reset()
{
    for (auto& ph : lfoPhase) ph = 0.0;
    for (auto& v : lfoSmooth) v = 0.0f;
    for (int i = 0; i < NumRandMods; ++i) { rnd[i] = Rnd(); rnd[i].lx = 0.1 + i * 0.013; }
    rng.seed (0xC0FFEEu);
}

void ModGlobal::process (ModBlock& b, int n, double bpm, double ppq, bool playing, int noteOns, float modWheel, float aftertouch,
                         float bend01, const float* followers)
{
    juce::ignoreUnused (playing);
    const double dt = (double) n / sr;
    const double beatsPerSec = juce::jmax (1.0, bpm) / 60.0;

    // ---- connexions actives (copie compacte, lue une seule fois)
    b.numSlots = 0;
    b.numGlobal = 0;
    b.targets = targets;
    for (auto& u : b.srcUsed) u = false;
    for (auto& u : b.fieldUsed) u = false;
    for (auto& slot : shared->slots)
    {
        const int src = slot.src.load (std::memory_order_acquire);
        if (src < 0 || src >= NumModSrc) continue;
        const int param = slot.param.load (std::memory_order_relaxed);
        const int field = targets->fieldForParam (param);
        if (field < 0)
        {
            const auto gt = targets->globalTarget (param);
            if (gt.first < 0) continue;
            auto& g = b.gslots[b.numGlobal++];
            g.src = src; g.rack = gt.first; g.index = gt.second;
            g.depth = slot.depth.load (std::memory_order_relaxed);
            g.bipolar = slot.bipolar.load (std::memory_order_relaxed);
            b.srcUsed[src] = true;
            continue;
        }
        auto& a = b.slots[b.numSlots++];
        a.src = src; a.field = field; a.param = param;
        a.depth = slot.depth.load (std::memory_order_relaxed);
        a.bipolar = slot.bipolar.load (std::memory_order_relaxed);
        b.srcUsed[src] = true;
        b.fieldUsed[field] = true;
    }
    for (int f = 0; f < NumModFields; ++f)
        if (b.fieldUsed[f]) if (auto* p = targets->parameter (f)) b.base01[f] = p->getValue();

    // ---- drift : dérive lente des champs (dans leur plage de random, cadenas respectés) ; la macro Chaos l'amplifie
    driftNow = juce::jlimit (0.0f, 1.0f, driftRaw->load() * (1.0f + chaosRaw->load()));
    driftTime += dt * (double) driftSpeedRaw->load();
    if (driftNow > 0.0001f)
    {
        for (int f = 0; f < NumModFields; ++f)
        {
            if (! shared->driftOk[f].load (std::memory_order_relaxed)) continue;
            auto* p = targets->parameter (f);
            if (p == nullptr) continue;
            const float n = p->getValue();
            const float lo = shared->fieldLo[f].load (std::memory_order_relaxed), hi = shared->fieldHi[f].load (std::memory_order_relaxed);
            const float nz = idg::dsp::smoothNoise (driftTime + (double) f * 0.37, (uint32_t) (f + 101)) * 2.0f - 1.0f;
            b.base01[f] = juce::jlimit (juce::jmin (lo, n), juce::jmax (hi, n), n + nz * driftNow * 0.25f * (hi - lo));
            b.fieldUsed[f] = true;
        }
    }

    // ---- macros
    for (int m = 0; m < NumMacros; ++m)
        b.global[MSrcMacro0 + m] = macros[m] != nullptr ? juce::jlimit (0.0f, 1.0f, macros[m]->load (std::memory_order_relaxed)) : 0.0f;

    // ---- LFO (partie globale + réglages pour les voix)
    for (int i = 0; i < NumLfos; ++i)
    {
        auto& r = lr[i];
        auto& L = b.lfo[i];
        const bool sync = r.sync->load() > 0.5f;
        const double cycleBeats = divisionBeats ((int) r.div->load());
        const double hz = sync ? beatsPerSec / cycleBeats : (double) r.rate->load();
        L.inc = (float) (hz / sr);
        L.mode = (int) r.mode->load();
        L.phase = r.phase->load();
        L.fadeMs = r.fade->load();
        L.smoothMs = r.smooth->load();
        L.table = shared->tables[i][shared->tableIndex[i].load (std::memory_order_acquire)];

        if (L.mode == LfoNoteGlobal && noteOns > 0) lfoPhase[i] = 0.0;
        double ph;
        if (L.mode == LfoHost) ph = ppq / cycleBeats;
        else { lfoPhase[i] += hz * dt; lfoPhase[i] -= std::floor (lfoPhase[i]); ph = lfoPhase[i]; }
        ph += L.phase;
        ph -= std::floor (ph);
        float v = L.table[juce::jlimit (0, idg::LfoShape::tableSize - 1, (int) (ph * idg::LfoShape::tableSize))];
        const float a = L.smoothMs <= 0.01f ? 0.0f : (float) std::exp (-dt / (L.smoothMs * 0.001));
        lfoSmooth[i] = v + (lfoSmooth[i] - v) * a;
        b.global[MSrcLfo0 + i] = lfoSmooth[i];
        // canal droit décalé (LFO stéréo, appliqué aux effets des bus)
        double phR = ph + (double) r.stereo->load();
        phR -= std::floor (phR);
        b.globalR[MSrcLfo0 + i] = L.table[juce::jlimit (0, idg::LfoShape::tableSize - 1, (int) (phR * idg::LfoShape::tableSize))];
        shared->lfoPhase[i].store ((float) ph, std::memory_order_relaxed);
    }

    // ---- random (Perlin, S&H, lisse, Lorenz)
    for (int i = 0; i < NumRandMods; ++i)
    {
        auto& r = rr[i];
        auto& s = rnd[i];
        const int style = (int) r.style->load();
        double pos;
        if (r.sync->load() > 0.5f) pos = ppq / divisionBeats ((int) r.div->load());
        else { s.t += r.rate->load() * dt; pos = s.t; }
        auto idx = (int64_t) std::floor (pos);
        if (r.retrig->load() > 0.5f && noteOns > 0) { s.t = std::floor (s.t) + 1.0; idx = s.last + 1; pos = (double) idx; }
        const float frac = (float) (pos - (double) idx);
        if (idx != s.last) { s.last = idx; s.from = s.to; s.to = rng.uni(); }
        float v;
        switch (style)
        {
            case RndPerlin: v = idg::dsp::smoothNoise (pos, (uint32_t) (i + 1)); break;
            case RndSampleHold: v = s.to; break;
            case RndSmooth: { const float u = 0.5f - 0.5f * std::cos (idg::dsp::pi * frac); v = s.from + (s.to - s.from) * u; break; }
            default:
            {
                const double h = juce::jlimit (0.0, 0.02, (double) r.rate->load() * dt * 0.5);
                for (int k = 0; k < 4; ++k)
                {
                    const double dx = 10.0 * (s.ly - s.lx), dy = s.lx * (28.0 - s.lz) - s.ly, dz = s.lx * s.ly - 8.0 / 3.0 * s.lz;
                    s.lx += dx * h * 0.25; s.ly += dy * h * 0.25; s.lz += dz * h * 0.25;
                }
                if (! std::isfinite (s.lx)) { s.lx = 0.1; s.ly = 0; s.lz = 0; }
                v = (float) juce::jlimit (0.0, 1.0, 0.5 + s.lx / 45.0);
                break;
            }
        }
        b.global[MSrcRnd0 + i] = juce::jlimit (0.0f, 1.0f, v);
    }

    // ---- jeu
    b.global[MSrcModWheel] = modWheel;
    b.global[MSrcAftertouch] = aftertouch;
    b.global[MSrcPitchBend] = bend01;
    for (int f = 0; f < NumBuses; ++f) b.global[MSrcFollow0 + f] = followers != nullptr ? followers[f] : 0.0f;
    for (int s = 0; s < NumModSrc; ++s) if (! (s >= MSrcLfo0 && s < MSrcRnd0)) b.globalR[s] = b.global[s];

    // ---- enveloppes (réglages pour les voix)
    for (int e = 0; e < NumEnvelopes; ++e)
    {
        auto& s = b.env[e];
        auto* v = er[e].v;
        s.delayMs = v[0]->load(); s.attackMs = v[1]->load(); s.holdMs = v[2]->load(); s.decayMs = v[3]->load();
        s.sustain = v[4]->load(); s.releaseMs = v[5]->load(); s.attackCurve = v[6]->load(); s.decayCurve = v[7]->load();
        s.releaseCurve = v[8]->load();
    }

    for (int s = 0; s < NumModSrc; ++s)
        if (! sourceIsPerVoice (s) || (s >= MSrcLfo0 && s < MSrcRnd0))
            shared->sourceValue[s].store (b.global[s], std::memory_order_relaxed);
}
} // namespace idgs
