#pragma once
#include "Wavetable.h"
#include "SampleData.h"
#include "Texture.h"
#include "../Core/Dsp.h"
#include "../Core/Params.h"

namespace idgs
{
/** Réglages d'un oscillateur (copiés une fois par bloc depuis les paramètres). */
struct OscParams
{
    bool on = true;
    int mode = ModeWavetable;
    float pos = 0.0f, level = 0.75f, pan = 0.0f, phase = 0.0f, phaseRand = 0.0f;
    float pitch = 0.0f;               // octave * 12 + demi-tons + cents / 100
    int unison = 1;
    float detune = 0.2f, blend = 0.75f, spread = 0.8f;
    float stretch = 0.0f, shift = 0.0f;
    int warp[2] { WarpOff, WarpOff };
    float warpAmt[2] { 0.0f, 0.0f };
    int warpSrc[2] { SrcOscB, SrcOscB };
    bool loop = true;
    int root = 60;
    int route = RouteF1;
    const Wavetable* table = nullptr;  // jamais nul quand la voix joue
    const SampleSet* samples = nullptr;
    const TexParams* tex = nullptr;    // réglages granulaire / spectral (partagés avec l'Osc Texture)
};

/** Sinus tabulé (synthèse additive). */
struct SineTable
{
    static constexpr int N = 4096;
    float t[N + 1];
    SineTable() { for (int i = 0; i <= N; ++i) t[i] = (float) std::sin (2.0 * juce::MathConstants<double>::pi * i / N); }
    inline float operator() (float phase01) const
    {
        const float p = (phase01 - std::floor (phase01)) * (float) N;
        const int i = (int) p;
        return t[i] + (t[i + 1] - t[i]) * (p - (float) i);
    }
    /** Phase dans [-1, 2) : repli sans floor (plus rapide). */
    inline float wrapped (float p) const
    {
        if (p >= 1.0f) p -= 1.0f;
        if (p < 0.0f) p += 1.0f;
        const float x = p * (float) N;
        const int i = juce::jlimit (0, N - 1, (int) x);
        return t[i] + (t[i + 1] - t[i]) * (x - (float) i);
    }
    static const SineTable& get() { static const SineTable s; return s; }
};

/** Un oscillateur dans une voix : unison, warps, inharmonicité (additif), modes wavetable / sample. */
class OscUnit
{
public:
    static constexpr int MaxUnison = 16;
    static constexpr int MaxPartials = Wavetable::NumPartials;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        posCoef = 1.0f - std::exp (-1.0f / (0.004f * (float) sr));
        gainCoef = posCoef;
        gran.prepare (sr, false);
        spec.prepare (sr);
    }

    /** Début de note : phases (fixe + aléatoire), zone de sample, position de départ. */
    void start (const OscParams& p, int note, idg::dsp::Rng& rng)
    {
        sPos = p.pos;
        additive = false;
        for (int u = 0; u < MaxUnison; ++u)
        {
            float ph = p.phase + p.phaseRand * rng.uni();
            // avec l'unison, chaque voix part d'une phase différente (sinon les voix s'annulent au départ)
            if (u > 0) ph += p.phaseRand > 0.0f ? 0.0f : rng.uni();
            ph_[u] = ph - std::floor (ph);
            gL[u] = gR[u] = 0.0f;
        }
        zone = -1;
        if (p.samples != nullptr && ! p.samples->zones.empty())
        {
            zone = p.mode == ModeMulti ? p.samples->zoneFor (note) : 0;
            const auto& z = *p.samples->zones[(size_t) zone];
            const double startPos = juce::jlimit (0.0, (double) z.length() - 2.0, (double) p.pos * (z.length() - 1));
            for (int u = 0; u < MaxUnison; ++u) sp_[u] = startPos;
        }
        for (int u = 0; u < MaxUnison; ++u) ended[u] = false;
        firstControl = true;
        gran.reset();
        const SpectralData* d = spectralData (p);
        spec.reset (d != nullptr ? p.pos * (double) (d->numFrames - 1) : 0.0);
    }

    static const SpectralData* spectralData (const OscParams& p)
    {
        return p.samples != nullptr && ! p.samples->zones.empty() ? p.samples->zones[0]->spectral.get() : nullptr;
    }

    /** Cadence de contrôle (toutes les 16 échantillons) : hauteurs, unison, choix des mipmaps, partiels. */
    void control (const OscParams& p, float notePitch)
    {
        n = juce::jlimit (1, MaxUnison, p.unison);
        float extra = 0.0f;
        syncRatio = 1.0f;
        for (int w = 0; w < 2; ++w)
        {
            if (p.warp[w] == WarpSync) syncRatio *= 1.0f + p.warpAmt[w] * 15.0f;
            if (p.warp[w] == WarpHardSync) extra += p.warpAmt[w] * 36.0f; // l'esclave monte, la phase est remise à zéro par le maître
        }
        const float basePitch = notePitch + p.pitch + extra;
        noteHzG = 440.0f * std::pow (2.0f, (basePitch - 69.0f) / 12.0f);
        ratioG = std::pow (2.0f, (basePitch - (float) p.root) / 12.0f);
        const float det = p.detune * std::sqrt (p.detune); // ±1 demi-ton max, courbe douce
        float sumSq = 0.0f;
        float gains[MaxUnison];
        for (int u = 0; u < n; ++u)
        {
            const float x = n == 1 ? 0.0f : 2.0f * (float) u / (float) (n - 1) - 1.0f;
            const float pitch = basePitch + x * det;
            const float freq = 440.0f * std::pow (2.0f, (pitch - 69.0f) / 12.0f);
            inc[u] = juce::jlimit (0.0f, 0.49f, freq / (float) sr);
            const bool centre = n == 1 || std::abs (x) <= 1.0f / (float) (n - 1) + 1.0e-4f;
            gains[u] = centre ? 1.0f : p.blend;
            sumSq += gains[u] * gains[u];
            const float pan = juce::jlimit (-1.0f, 1.0f, p.pan + x * p.spread * (n > 1 ? 1.0f : 0.0f));
            const float a = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            tL[u] = std::cos (a);
            tR[u] = std::sin (a);
            if (p.mode != ModeWavetable)
            {
                // lecture de sample : rapport de hauteur par rapport à la note d'origine
                const int zi = (p.samples != nullptr && zone >= 0 && ! p.samples->zones.empty()) ? juce::jmin (zone, (int) p.samples->zones.size() - 1) : -1;
                const int root = (zi >= 0 && p.mode == ModeMulti) ? p.samples->zones[(size_t) zi]->root : p.root;
                const double zsr = zi >= 0 ? p.samples->zones[(size_t) zi]->sampleRate : sr;
                sinc[u] = std::pow (2.0, (pitch - (float) root) / 12.0) * zsr / sr;
            }
        }
        const float norm = 1.0f / std::sqrt (juce::jmax (1.0e-6f, sumSq));
        for (int u = 0; u < n; ++u) { tL[u] *= gains[u] * norm; tR[u] *= gains[u] * norm; }
        // voix d'unison retirées : elles s'éteignent en fondu (pas de coupure nette)
        renderCount = n;
        for (int u = n; u < MaxUnison; ++u)
        {
            tL[u] = tR[u] = 0.0f;
            if (std::abs (gL[u]) + std::abs (gR[u]) > 1.0e-5f) renderCount = u + 1;
            else gL[u] = gR[u] = 0.0f;
        }
        if (firstControl) { for (int u = 0; u < n; ++u) { gL[u] = tL[u]; gR[u] = tR[u]; } firstControl = false; }

        float maxInc = 0.0f;
        for (int u = 0; u < n; ++u) maxInc = juce::jmax (maxInc, inc[u]);
        mip = Wavetable::mipFor (maxInc * syncRatio);

        // inharmonicité : passage en additif dès que stretch / shift quittent zéro
        const bool wantAdditive = p.mode == ModeWavetable && (std::abs (p.stretch) > 0.002f || std::abs (p.shift) > 0.002f);
        if (wantAdditive && ! additive)
        {
            // continuité : chaque partiel reprend la phase équivalente de la lecture de table
            for (int u = 0; u < MaxUnison; ++u)
                for (int k = 1; k <= MaxPartials; ++k) { const float v = ph_[u] * (float) k; pph[u][k - 1] = v - std::floor (v); }
        }
        else if (! wantAdditive && additive)
        {
            for (int u = 0; u < MaxUnison; ++u) ph_[u] = pph[u][0];
        }
        additive = wantAdditive;
        if (additive && p.table != nullptr)
        {
            numPartials = juce::jlimit (8, MaxPartials, 256 / n);
            const float expo = 1.0f + p.stretch * 0.5f;
            const auto& wt = *p.table;
            const float fp = juce::jlimit (0.0f, 1.0f, sPos) * (float) (wt.getNumFrames() - 1);
            const int f0 = (int) fp, f1 = juce::jmin (f0 + 1, wt.getNumFrames() - 1);
            const float x = fp - (float) f0;
            const int nearest = x < 0.5f ? f0 : f1;
            for (int k = 1; k <= numPartials; ++k)
            {
                ratio[k - 1] = std::pow ((float) k, expo) + p.shift;
                amp[k - 1] = wt.partialAmp (f0, k) + (wt.partialAmp (f1, k) - wt.partialAmp (f0, k)) * x;
                phOff[k - 1] = wt.partialPhase (nearest, k);
            }
        }
    }

    /** Un échantillon. mod[] : dernières valeurs des sources (cross-mod), syncIn : le maître a bouclé. */
    inline void tick (const OscParams& p, const float* mod, bool syncIn, float& outL, float& outR)
    {
        sPos += (p.pos - sPos) * posCoef;
        float L = 0.0f, R = 0.0f;
        wrapped = false;
        const bool hardSync = syncIn && (p.warp[0] == WarpHardSync || p.warp[1] == WarpHardSync);
        const int count = renderCount;

        // gain commun des warps AM / RM (même valeur pour toutes les voix d'unison)
        float gw = 1.0f;
        for (int w = 0; w < 2; ++w) applyGainWarp (p.warp[w], p.warpAmt[w], mod[p.warpSrc[w]], gw);

        for (int u = 0; u < count; ++u)
        {
            gL[u] += (tL[u] - gL[u]) * gainCoef;
            gR[u] += (tR[u] - gR[u]) * gainCoef;
            ph_[u] += inc[u];
            if (ph_[u] >= 1.0f) { ph_[u] -= 1.0f; if (u == 0) wrapped = true; }
            if (hardSync) ph_[u] = 0.0f;
        }

        if (p.mode == ModeGranular || p.mode == ModeSpectral)
        {
            // granulaire / spectral : un seul flux (pas d'unison), réglages fins dans l'onglet Texture
            static const TexParams defaults;
            const TexParams& t = p.tex != nullptr ? *p.tex : defaults;
            float l = 0.0f, r = 0.0f;
            if (p.mode == ModeGranular)
            {
                const SampleZone* z = p.samples != nullptr && ! p.samples->zones.empty() ? p.samples->zones[0].get() : nullptr;
                const GrainCloud::Settings gs { sPos, t.gSize, t.gDensity, t.gSpray, t.gPitchRand, t.gPanRand, t.gReverse, t.gShape, false };
                gran.tick (gs, z, ratioG, rngG, l, r);
            }
            else
            {
                const SpectralVoice::Settings ss { t.sFreeze, t.sSpeed, t.sSmear, t.sShift, t.sStretch, t.sHarm, 0.0f, t.sScramble };
                l = r = spec.tick (ss, spectralData (p), nullptr, ratioG, noteHzG, rngG);
            }
            const float pa = (juce::jlimit (-1.0f, 1.0f, p.pan) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            L = l * gw * std::cos (pa) * 1.4142f;
            R = r * gw * std::sin (pa) * 1.4142f;
        }
        else if (p.mode == ModeWavetable && p.table != nullptr)
        {
            if (additive)
            {
                const SineTable& sine = SineTable::get();
                for (int u = 0; u < count; ++u)
                {
                    float s = 0.0f;
                    auto* ph = pph[u];
                    const float iu = inc[u];
                    for (int k = 0; k < numPartials; ++k)
                    {
                        const float pi = iu * ratio[k];
                        float x = ph[k] + pi;
                        if (x >= 1.0f) x -= 1.0f;
                        ph[k] = x;
                        if (pi < 0.45f) s += amp[k] * sine.wrapped (x + phOff[k]);
                    }
                    if (hardSync) for (int k = 0; k < numPartials; ++k) ph[k] = 0.0f;
                    L += s * gL[u];
                    R += s * gR[u];
                }
                L *= gw; R *= gw;
            }
            else
            {
                // frames voisines calculées une fois pour toutes les voix d'unison
                const auto& wt = *p.table;
                const float fp = juce::jlimit (0.0f, 1.0f, sPos) * (float) (wt.getNumFrames() - 1);
                const int f0 = (int) fp;
                const int f1 = juce::jmin (f0 + 1, wt.getNumFrames() - 1);
                const float fx = fp - (float) f0;
                const bool phaseWarp = (p.warp[0] != WarpOff && p.warp[0] != WarpAM && p.warp[0] != WarpRM && p.warp[0] != WarpHardSync)
                                    || (p.warp[1] != WarpOff && p.warp[1] != WarpAM && p.warp[1] != WarpRM && p.warp[1] != WarpHardSync);
                for (int u = 0; u < count; ++u)
                {
                    float ph = ph_[u];
                    float gain = 1.0f;
                    if (phaseWarp)
                        for (int w = 0; w < 2; ++w) ph = warpPhase (p.warp[w], p.warpAmt[w], ph, mod[p.warpSrc[w]], gain);
                    const float a = wt.read (f0, mip, ph);
                    const float v = (fx > 0.0f ? a + (wt.read (f1, mip, ph) - a) * fx : a) * gain;
                    L += v * gL[u];
                    R += v * gR[u];
                }
                L *= gw; R *= gw;
            }
        }
        else if (p.mode != ModeWavetable && p.samples != nullptr && zone >= 0 && zone < (int) p.samples->zones.size())
        {
            const auto& z = *p.samples->zones[(size_t) zone];
            for (int u = 0; u < count; ++u)
            {
                float vL, vR;
                readSample (z, p, u, vL, vR);
                L += vL * gL[u];
                R += vR * gR[u];
            }
            L *= gw; R *= gw;
        }
        outL = L;
        outR = R;
        last = 0.5f * (L + R);
    }

    float last = 0.0f;     // dernière sortie mono (source de cross-modulation)
    bool wrapped = false;  // la voix d'unison centrale vient de boucler (source de hard sync)

public:
    /** Warps qui agissent sur la phase de lecture (et gain pour PWM / flip / AM / RM). */
    static inline float warpPhase (int mode, float amt, float ph, float mod, float& gain)
    {
        if (amt <= 0.0f && mode != WarpOff) return ph;
        switch (mode)
        {
            case WarpOff: return ph;
            case WarpSync: { const float v = ph * (1.0f + amt * 15.0f); return v - std::floor (v); }
            case WarpBendPlus: case WarpBendMinus:
            {
                const float d = 2.0f * ph - 1.0f;
                const float e = mode == WarpBendPlus ? 1.0f + amt * 3.0f : 1.0f / (1.0f + amt * 3.0f);
                return 0.5f + 0.5f * std::copysign (std::pow (std::abs (d), e), d);
            }
            case WarpPWM:
            {
                const float w = 1.0f - amt * 0.95f;
                if (ph >= w) { gain = 0.0f; return 0.0f; }
                return ph / w;
            }
            case WarpAsymPlus: case WarpAsymMinus:
            {
                const float m = 0.5f + (mode == WarpAsymPlus ? 1.0f : -1.0f) * amt * 0.45f;
                return ph < m ? 0.5f * ph / m : 0.5f + 0.5f * (ph - m) / (1.0f - m);
            }
            case WarpFlip: if (ph > 1.0f - amt) gain = -gain; return ph;
            case WarpMirror:
            {
                const float m = ph < 0.5f ? 2.0f * ph : 2.0f - 2.0f * ph;
                const float v = ph + (m - ph) * amt;
                return v - std::floor (v);
            }
            case WarpQuantize:
            {
                const float steps = std::pow (2.0f, 1.0f + (1.0f - amt) * 7.0f);
                return std::floor (ph * steps) / steps;
            }
            case WarpRemap:
            {
                const float v = ph + amt * 0.22f * std::sin (juce::MathConstants<float>::twoPi * ph * 2.0f);
                return v - std::floor (v);
            }
            case WarpFM: { const float v = ph + amt * 2.0f * mod; return v - std::floor (v); }
            default: return ph; // AM / RM / hard sync : traités hors de la lecture de phase
        }
    }

    /** Warps applicables hors lecture de table (additif, sample) : AM, RM, flip de polarité. */
    static inline void applyGainWarp (int mode, float amt, float mod, float& gain)
    {
        if (mode == WarpAM) gain *= (1.0f - amt * 0.5f) + amt * 0.5f * mod;
        else if (mode == WarpRM) gain *= (1.0f - amt) + amt * mod;
    }

private:
    /** Lecture de sample (interpolation d'Hermite), boucle avec fondu enchaîné, fin sans clic. */
    inline void readSample (const SampleZone& z, const OscParams& p, int u, float& l, float& r)
    {
        const int len = z.length();
        if (len < 4 || ended[u]) { l = r = 0.0f; return; }
        double& pos = sp_[u];
        const double startPos = juce::jlimit (0.0, (double) len - 2.0, (double) p.pos * (len - 1));
        const double loopStart = juce::jmin (startPos, (double) len * 0.9);
        const double loopLen = (double) len - 1.0 - loopStart;
        const int xf = (int) juce::jmin (512.0, loopLen * 0.25);

        l = hermite (z.data.getReadPointer (0), len, pos);
        r = hermite (z.data.getReadPointer (1), len, pos);

        const double end = (double) len - 1.0;
        if (p.loop && loopLen > 64.0)
        {
            const double xfStart = end - xf;
            if (pos >= xfStart)
            {
                // fondu enchaîné entre la fin et le début de la boucle
                const float t = (float) ((pos - xfStart) / xf);
                const double other = loopStart + (pos - xfStart);
                l = l * (1.0f - t) + hermite (z.data.getReadPointer (0), len, other) * t;
                r = r * (1.0f - t) + hermite (z.data.getReadPointer (1), len, other) * t;
            }
            pos += sinc[u];
            if (pos >= end) pos = loopStart + xf + (pos - end);
        }
        else
        {
            // fin du sample : fondu de 64 échantillons
            const double fadeStart = end - 64.0 * juce::jmax (1.0, sinc[u]);
            if (pos > fadeStart)
            {
                const float t = (float) juce::jlimit (0.0, 1.0, (end - pos) / (end - fadeStart));
                l *= t; r *= t;
            }
            pos += sinc[u];
            if (pos >= end) ended[u] = true;
        }
    }

    static inline float hermite (const float* d, int len, double pos)
    {
        const int i = (int) pos;
        const float f = (float) (pos - i);
        auto at = [&] (int k) { return d[juce::jlimit (0, len - 1, k)]; };
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    double sr = 44100.0;
    GrainCloud gran;
    SpectralVoice spec;
    idg::dsp::Rng rngG;
    float noteHzG = 261.6f, ratioG = 1.0f;
    int n = 1, renderCount = 1, mip = 0, zone = -1, numPartials = 32;
    float syncRatio = 1.0f, sPos = 0.0f, posCoef = 0.01f, gainCoef = 0.01f;
    bool additive = false, firstControl = true;
    float ph_[MaxUnison] {}, inc[MaxUnison] {}, tL[MaxUnison] {}, tR[MaxUnison] {}, gL[MaxUnison] {}, gR[MaxUnison] {};
    double sp_[MaxUnison] {}, sinc[MaxUnison] {};
    bool ended[MaxUnison] {};
    float pph[MaxUnison][MaxPartials] {};
    float ratio[MaxPartials] {}, amp[MaxPartials] {}, phOff[MaxPartials] {};
};
} // namespace idgs
