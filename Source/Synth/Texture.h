#pragma once
#include "SampleData.h"
#include "../Core/Dsp.h"
#include "../Core/Params.h"

namespace idgs
{
/** Réglages de l'Osc Texture (copiés une fois par bloc). */
struct TexParams
{
    bool on = false;
    int engine[2] { TexGranular, TexChaos };
    float morph = 0.0f, level = 0.6f, pan = 0.0f, pitch = 0.0f;
    int route = RouteF1, root = 60;
    // granulaire
    float gPos = 0.3f, gSize = 90.0f, gDensity = 25.0f, gSpray = 0.15f, gPitchRand = 0.0f, gPanRand = 0.4f, gReverse = 0.0f, gShape = 0.0f;
    bool gFreeze = false;
    int gSource = 0;
    // spectral
    bool sFreeze = false;
    float sSpeed = 1.0f, sSmear = 0.0f, sShift = 0.0f, sStretch = 1.0f, sHarm = 0.0f, sMorph = 0.0f, sScramble = 0.0f;
    // chaos / résonateur / bruit / data
    int cType = 0;
    float cAmount = 0.5f, cRate = 1.0f;
    int rType = 0, rExciter = 0;
    float rDecay = 0.7f, rBright = 0.5f, rExcite = 0.5f;
    float nColor = 0.0f, nDensity = 1.0f, nCrackle = 0.0f, nFocus = 0.0f;
    float dOffset = 0.0f, dRate = 1.0f;
    int dFormat = 0;
    const SampleSet* sound1 = nullptr;
    const SampleSet* sound2 = nullptr;
    const SampleSet* data = nullptr;
};

/** Empreinte spectrale. */
struct FpParams
{
    bool on = false, loop = true;
    float amount = 0.8f, shift = 0.0f, speed = 1.0f;
    int res = 1;
    const SampleSet* fp = nullptr;
};

/** Nuage de grains (Osc Texture et modes granulaires des Osc A / B). Aucune allocation hors prepare(). */
class GrainCloud
{
public:
    static constexpr int MaxGrains = 24;
    static constexpr int LiveSize = 1 << 16; // ~1,4 s à 48 kHz (flux live des Osc A + B)

    void prepare (double sampleRate, bool withLive = true) { sr = sampleRate; live.assign (withLive ? (size_t) LiveSize : 0, 0.0f); reset(); }
    void reset() { for (auto& g : grains) g.active = false; timer = 1.0f; w = 0; std::fill (live.begin(), live.end(), 0.0f); }
    inline void pushLive (float x) { if (live.empty()) return; live[(size_t) w] = x; w = (w + 1) & (LiveSize - 1); }

    struct Settings { float pos, size, density, spray, pitchRand, panRand, reverse, shape; bool liveSource; };

    inline void tick (const Settings& s, const SampleZone* z, float noteRatio, idg::dsp::Rng& rng, float& outL, float& outR)
    {
        // déclenchement des grains
        timer += s.density / (float) sr;
        if (timer >= 1.0f)
        {
            timer -= 1.0f;
            for (auto& g : grains)
                if (! g.active)
                {
                    spawn (g, s, z, noteRatio, rng);
                    break;
                }
        }
        const float overlap = juce::jmax (1.0f, s.density * s.size * 0.001f);
        const float norm = 1.0f / std::sqrt (overlap);
        float L = 0.0f, R = 0.0f;
        for (auto& g : grains)
        {
            if (! g.active) continue;
            const float t = (float) g.age / (float) g.len;
            const float hann = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * t);
            const float trap = juce::jlimit (0.0f, 1.0f, juce::jmin (t, 1.0f - t) * 10.0f);
            const float env = hann + (trap - hann) * s.shape;
            float l, r;
            if (g.live)
            {
                const int idx = (int) std::floor (g.pos) & (LiveSize - 1);
                l = r = live[(size_t) idx];
            }
            else if (z != nullptr && z->length() > 2)
            {
                const int len = z->length();
                double p = g.pos;
                if (p < 0.0) p += len * std::ceil (-p / len);
                const int i0 = (int) p % len;
                const int i1 = (i0 + 1) % len;
                const float fr = (float) (p - std::floor (p));
                l = z->data.getSample (0, i0) + (z->data.getSample (0, i1) - z->data.getSample (0, i0)) * fr;
                r = z->data.getSample (1, i0) + (z->data.getSample (1, i1) - z->data.getSample (1, i0)) * fr;
            }
            else { l = r = 0.0f; }
            L += l * env * g.gl;
            R += r * env * g.gr;
            g.pos += g.inc;
            if (++g.age >= g.len) g.active = false;
        }
        outL = L * norm;
        outR = R * norm;
    }

private:
    struct Grain { bool active = false, live = false; double pos = 0.0, inc = 1.0; int len = 1, age = 0; float gl = 1.0f, gr = 1.0f; };

    void spawn (Grain& g, const Settings& s, const SampleZone* z, float noteRatio, idg::dsp::Rng& rng)
    {
        g.active = true;
        g.age = 0;
        g.len = juce::jmax (8, (int) (s.size * 0.001f * (float) sr));
        const float pr = s.pitchRand > 0.0f ? std::pow (2.0f, rng.bi() * s.pitchRand / 12.0f) : 1.0f;
        const bool rev = rng.uni() < s.reverse;
        const float pan = juce::jlimit (-1.0f, 1.0f, rng.bi() * s.panRand);
        const float a = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        g.gl = std::cos (a) * 1.4142f;
        g.gr = std::sin (a) * 1.4142f;
        g.live = (s.liveSource || z == nullptr) && ! live.empty();
        if (! g.live && z == nullptr) { g.active = false; return; }
        if (g.live)
        {
            // retard dans le flux live : position 0 = le plus récent, 1 = ~1 s en arrière
            const float delay = juce::jlimit (0.0f, 1.0f, s.pos + s.spray * rng.uni() * 0.5f) * (float) (LiveSize - 4 - g.len);
            g.inc = rev ? -1.0 : 1.0;
            g.pos = (double) (w - (int) delay - g.len - 2) + (rev ? g.len : 0);
        }
        else
        {
            const double zsr = z->sampleRate;
            g.inc = (double) (noteRatio * pr) * zsr / sr * (rev ? -1.0 : 1.0);
            const double start = juce::jlimit (0.0, 1.0, (double) s.pos + (double) (rng.bi() * s.spray * 0.3f)) * (z->length() - 1);
            g.pos = start;
        }
    }

    double sr = 44100.0;
    Grain grains[MaxGrains];
    float timer = 1.0f;
    std::vector<float> live;
    int w = 0;
};

/** Resynthèse spectrale (vocodeur de phase simplifié) à partir de l'analyse d'un son. */
class SpectralVoice
{
public:
    static constexpr int N = SpectralData::N, Bins = SpectralData::Bins, Hop = SpectralData::Hop;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        buf.assign ((size_t) N * 2, 0.0f);
        ola.assign ((size_t) N, 0.0f);
        phase.assign ((size_t) Bins, 0.0f);
        sm.assign ((size_t) Bins, 0.0f);
        cur.assign ((size_t) Bins, 0.0f);
        win.assign ((size_t) N, 0.0f);
        for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
        perm.resize ((size_t) Bins);
        juce::Random r (77);
        for (int k = 0; k < Bins; ++k) perm[(size_t) k] = k;
        for (int k = Bins - 1; k > 1; --k) std::swap (perm[(size_t) k], perm[(size_t) (1 + r.nextInt (k))]);
        // calibration du gain de la FFT inverse (indépendant du moteur FFT de la plateforme)
        std::fill (buf.begin(), buf.end(), 0.0f);
        buf[64 * 2] = 1.0f;
        fft.performRealOnlyInverseTransform (buf.data());
        float pk = 1.0e-9f;
        for (int i = 0; i < N; ++i) pk = juce::jmax (pk, std::abs (buf[(size_t) i]));
        ifftGain = 1.0f / pk;
        reset (0.0);
    }

    void reset (double startFrame)
    {
        std::fill (ola.begin(), ola.end(), 0.0f);
        std::fill (sm.begin(), sm.end(), 0.0f);
        for (auto& p : phase) p = 0.0f;
        framePos = startFrame;
        counter = 0;
        readPos = 0;
        first = true;
    }

    struct Settings { bool freeze; float speed, smear, shiftHz, stretch, harm, morph, scramble; };

    inline float tick (const Settings& s, const SpectralData* d1, const SpectralData* d2, float noteRatio, float noteHz, idg::dsp::Rng& rng)
    {
        if (d1 == nullptr || d1->numFrames < 1) return 0.0f;
        if (counter == 0) synthFrame (s, d1, d2, noteRatio, noteHz, rng);
        const float y = ola[(size_t) readPos];
        ola[(size_t) readPos] = 0.0f;
        readPos = (readPos + 1) % N;
        if (++counter >= Hop) counter = 0;
        return y;
    }

private:
    float magAt (const std::vector<float>& m, float idx) const
    {
        if (idx < 0.0f || idx >= (float) (Bins - 1)) return 0.0f;
        const int i = (int) idx;
        const float f = idx - (float) i;
        return m[(size_t) i] + (m[(size_t) i + 1] - m[(size_t) i]) * f;
    }

    void synthFrame (const Settings& s, const SpectralData* d1, const SpectralData* d2, float ratio, float noteHz, idg::dsp::Rng& rng)
    {
        // trame courante (interpolée), morph avec le second son
        const int f0 = (int) framePos;
        const float fx = (float) (framePos - f0);
        const float* a = d1->frame (f0);
        const float* b = d1->frame (f0 + 1);
        for (int k = 0; k < Bins; ++k) cur[(size_t) k] = a[k] + (b[k] - a[k]) * fx;
        if (d2 != nullptr && d2->numFrames > 0 && s.morph > 0.0f)
        {
            const int g0 = (int) ((framePos / juce::jmax (1, d1->numFrames)) * d2->numFrames);
            const float* c = d2->frame (g0);
            for (int k = 0; k < Bins; ++k) cur[(size_t) k] += (c[k] - cur[(size_t) k]) * s.morph;
        }
        // étalement temporel
        const float smear = first ? 0.0f : s.smear;
        for (int k = 0; k < Bins; ++k) sm[(size_t) k] = sm[(size_t) k] * smear + cur[(size_t) k] * (1.0f - smear);
        first = false;

        const float binHz = (float) sr / (float) N;
        const float shiftBins = s.shiftHz / binHz;
        const float scale = juce::jmax (0.05f, s.stretch * ratio);
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int k = 1; k < Bins - 1; ++k)
        {
            const float src = ((float) k - shiftBins) / scale;
            float m = magAt (sm, src);
            if (s.scramble > 0.0f) m += (magAt (sm, (float) perm[(size_t) k] / scale) - m) * s.scramble;
            if (s.harm > 0.0f && noteHz > 1.0f)
            {
                const float h = std::pow (std::abs (std::cos (juce::MathConstants<float>::pi * (float) k * binHz / noteHz)), 8.0f);
                m *= 1.0f - s.harm + s.harm * h;
            }
            phase[(size_t) k] += juce::MathConstants<float>::twoPi * (float) k * (float) Hop / (float) N + rng.bi() * 0.4f;
            phase[(size_t) k] -= juce::MathConstants<float>::twoPi * std::floor (phase[(size_t) k] / juce::MathConstants<float>::twoPi);
            const float amp = m * 2.0f;
            buf[(size_t) k * 2] = amp * std::cos (phase[(size_t) k]);
            buf[(size_t) k * 2 + 1] = amp * std::sin (phase[(size_t) k]);
        }
        fft.performRealOnlyInverseTransform (buf.data());
        // fenêtre de synthèse + addition avec recouvrement (Hann², recouvrement 75 % -> somme 1,5)
        for (int i = 0; i < N; ++i)
        {
            const float v = buf[(size_t) i] * ifftGain * win[(size_t) i] * (1.0f / 1.5f);
            ola[(size_t) ((readPos + i) % N)] += std::isfinite (v) ? v : 0.0f;
        }
        if (! s.freeze)
        {
            framePos += s.speed;
            if (framePos >= d1->numFrames - 1) framePos = std::fmod (framePos, (double) juce::jmax (1, d1->numFrames - 1));
        }
    }

    double sr = 44100.0;
    juce::dsp::FFT fft { SpectralData::Order };
    std::vector<float> buf, ola, phase, sm, cur, win;
    std::vector<int> perm;
    float ifftGain = 1.0f;
    double framePos = 0.0;
    int counter = 0, readPos = 0;
    bool first = true;
};

/** Les 6 moteurs de l'Osc Texture, avec fondu de l'un à l'autre. */
class TextureVoice
{
public:
    static constexpr int ResSize = 4096;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        grains.prepare (sr);
        spec.prepare (sr);
        for (auto& l : lines) l.assign ((size_t) ResSize, 0.0f);
        noiseTone.reset();
        focus.reset();
        dc.prepare (sr);
    }

    void start (const TexParams& p, int note, idg::dsp::Rng& rng)
    {
        grains.reset();
        const SpectralData* d = p.sound1 != nullptr && ! p.sound1->zones.empty() ? p.sound1->zones[0]->spectral.get() : nullptr;
        spec.reset (d != nullptr ? p.gPos * (double) (d->numFrames - 1) : 0.0);
        cx = 0.1 + rng.uni() * 0.01; cy = 0.0; cz = 0.0; cl = 0.4 + rng.uni() * 0.2; cPh = 0.0f; cPrev = 0.0f; cNext = 0.0f;
        hx = 0.1; hy = 0.0;
        for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f);
        for (auto& lp : lineLp) lp = 0.0f;
        lw = 0;
        burst = (int) (0.02 * sr);
        dataPos = 0.0;
        crackle = 0.0f;
        dc.reset();
        juce::ignoreUnused (note);
    }

    /** Cadence de contrôle : hauteur. */
    void control (const TexParams& p, float pitch)
    {
        noteHz = 440.0f * std::pow (2.0f, (pitch + p.pitch - 69.0f) / 12.0f);
        noteRatio = std::pow (2.0f, (pitch + p.pitch - (float) p.root) / 12.0f);
        static const float ratios[3][4] { { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.593f, 2.135f, 2.917f }, { 1.0f, 2.756f, 5.404f, 8.933f } };
        numLines = p.rType == 0 ? 1 : 4;
        for (int k = 0; k < numLines; ++k)
            lineLen[k] = juce::jlimit (2.0f, (float) ResSize - 4.0f, (float) sr / (noteHz * ratios[juce::jlimit (0, 2, p.rType)][k]));
        fb = 0.9f + 0.0995f * std::pow (p.rDecay, 0.35f);
        lpA = 0.05f + 0.93f * p.rBright;
        if (std::abs (p.nColor) > 0.01f)
            noiseTone.set (sr, p.nColor < 0.0f ? 18000.0f * std::pow (0.02f, -p.nColor) : 30.0f * std::pow (200.0f, p.nColor), 0.7071f);
        focus.set (sr, juce::jlimit (20.0f, (float) sr * 0.45f, noteHz), 2.0f + 30.0f * p.nFocus);
    }

    /** Un échantillon. oscA / oscAB : sorties courantes des oscillateurs (excitateur, flux live). */
    inline void tick (const TexParams& p, float oscA, float oscAB, idg::dsp::Rng& rng, float& outL, float& outR)
    {
        if (! p.gFreeze) grains.pushLive (oscAB);
        float l1, r1;
        engine (p.engine[0], p, oscA, rng, l1, r1);
        if (p.morph > 0.001f && p.engine[1] != p.engine[0])
        {
            float l2, r2;
            engine (p.engine[1], p, oscA, rng, l2, r2);
            // fondu à puissance constante d'un moteur à l'autre
            const float a = p.morph * juce::MathConstants<float>::halfPi;
            const float g1 = std::cos (a), g2 = std::sin (a);
            l1 = l1 * g1 + l2 * g2;
            r1 = r1 * g1 + r2 * g2;
        }
        if (! std::isfinite (l1) || ! std::isfinite (r1)) { l1 = r1 = 0.0f; start (p, 60, rng); }
        outL = l1;
        outR = r1;
        last = 0.5f * (l1 + r1);
    }

    float last = 0.0f;

private:
    inline void engine (int e, const TexParams& p, float oscA, idg::dsp::Rng& rng, float& l, float& r)
    {
        switch (e)
        {
            case TexGranular:
            {
                const SampleZone* z = p.sound1 != nullptr && ! p.sound1->zones.empty() ? p.sound1->zones[0].get() : nullptr;
                const GrainCloud::Settings s { p.gPos, p.gSize, p.gDensity, p.gSpray, p.gPitchRand, p.gPanRand, p.gReverse, p.gShape, p.gSource == 1 };
                grains.tick (s, z, noteRatio, rng, l, r);
                return;
            }
            case TexSpectral:
            {
                const SpectralData* d1 = p.sound1 != nullptr && ! p.sound1->zones.empty() ? p.sound1->zones[0]->spectral.get() : nullptr;
                const SpectralData* d2 = p.sound2 != nullptr && ! p.sound2->zones.empty() ? p.sound2->zones[0]->spectral.get() : nullptr;
                const SpectralVoice::Settings s { p.sFreeze, p.sSpeed, p.sSmear, p.sShift, p.sStretch, p.sHarm, p.sMorph, p.sScramble };
                l = r = spec.tick (s, d1, d2, noteRatio, noteHz, rng);
                return;
            }
            case TexChaos: l = r = chaos (p); return;
            case TexResonator: l = r = resonator (p, oscA, rng); return;
            case TexNoise: l = r = sculptNoise (p, rng); return;
            case TexDataBend: l = r = dataBend (p); return;
            default: l = r = 0.0f; return;
        }
    }

    inline float chaos (const TexParams& p)
    {
        const float inc = noteHz / (float) sr * p.cRate;
        switch (p.cType)
        {
            case 0: // Lorenz : l'attracteur tourne à la hauteur de la note
            {
                const double rho = 20.0 + 40.0 * p.cAmount;
                const double h = juce::jlimit (0.0, 0.02, (double) inc * 1.4);
                for (int k = 0; k < 2; ++k)
                {
                    const double dx = 10.0 * (cy - cx), dy = cx * (rho - cz) - cy, dz = cx * cy - 8.0 / 3.0 * cz;
                    cx += dx * h * 0.5; cy += dy * h * 0.5; cz += dz * h * 0.5;
                }
                if (! std::isfinite (cx)) { cx = 0.1; cy = 0.0; cz = 0.0; }
                return dc.process (0, (float) juce::jlimit (-1.0, 1.0, cx / 22.0));
            }
            case 1: // logistique : itérée à la hauteur de la note, interpolée
            {
                cPh += inc * 2.0f;
                if (cPh >= 1.0f)
                {
                    cPh -= std::floor (cPh);
                    const float rr = 3.4f + 0.6f * p.cAmount;
                    cl = rr * cl * (1.0f - cl);
                    if (! std::isfinite (cl) || cl <= 0.0f || cl >= 1.0f) cl = 0.37f;
                    cPrev = cNext;
                    cNext = cl * 2.0f - 1.0f;
                }
                return dc.process (0, cPrev + (cNext - cPrev) * cPh);
            }
            default: // Hénon
            {
                cPh += inc;
                if (cPh >= 1.0f)
                {
                    cPh -= std::floor (cPh);
                    const double a = 1.05 + 0.35 * p.cAmount, b = 0.3;
                    const double nx = 1.0 - a * hx * hx + hy;
                    hy = b * hx;
                    hx = nx;
                    if (! std::isfinite (hx) || std::abs (hx) > 10.0) { hx = 0.1; hy = 0.0; }
                    cPrev = cNext;
                    cNext = (float) juce::jlimit (-1.0, 1.0, hx / 1.4);
                }
                return dc.process (0, cPrev + (cNext - cPrev) * cPh);
            }
        }
    }

    inline float resonator (const TexParams& p, float oscA, idg::dsp::Rng& rng)
    {
        float exc;
        if (p.rExciter == 0) exc = burst > 0 ? rng.bi() * (float) burst / (float) (0.02 * sr) : 0.0f;
        else if (p.rExciter == 1) exc = rng.bi() * 0.3f;
        else exc = oscA;
        if (burst > 0) --burst;
        exc *= p.rExcite;
        float sum = 0.0f;
        for (int k = 0; k < numLines; ++k)
        {
            auto& buf = lines[(size_t) k];
            const float rp = (float) lw - lineLen[k];
            const int i0 = ((int) std::floor (rp)) & (ResSize - 1);
            const float fr = rp - std::floor (rp);
            const float d = buf[(size_t) i0] + (buf[(size_t) ((i0 + 1) & (ResSize - 1))] - buf[(size_t) i0]) * fr;
            lineLp[k] += (d - lineLp[k]) * lpA;
            const float y = exc + lineLp[k] * fb;
            buf[(size_t) lw] = idg::dsp::fastTanh (y);
            sum += y;
        }
        lw = (lw + 1) & (ResSize - 1);
        return sum / (float) numLines * 0.6f;
    }

    inline float sculptNoise (const TexParams& p, idg::dsp::Rng& rng)
    {
        float v = rng.bi();
        if (p.nDensity < 0.999f) v = rng.uni() < p.nDensity * p.nDensity ? v : 0.0f;
        if (p.nCrackle > 0.0f && rng.uni() < 0.0008f * p.nCrackle) crackle = (rng.uni() < 0.5f ? -1.0f : 1.0f) * (0.5f + 0.5f * rng.uni());
        crackle *= 0.8f;
        v = v * 0.5f + crackle * p.nCrackle;
        if (p.nColor < -0.01f) v = noiseTone.lowpass (0, v);
        else if (p.nColor > 0.01f) v = noiseTone.highpass (0, v);
        if (p.nFocus > 0.0f) v = v + (focus.bandpass (0, v) * (1.0f + 3.0f * p.nFocus) - v) * p.nFocus;
        return v;
    }

    inline float dataBend (const TexParams& p)
    {
        if (p.data == nullptr || p.data->zones.empty()) return 0.0f;
        const auto& z = *p.data->zones[0];
        const int len = p.dFormat == 1 ? z.length() / 2 : z.length();
        if (len < 4) return 0.0f;
        const int start = (int) (p.dOffset * (float) (len - 2));
        const int span = juce::jmax (2, len - start);
        const float* c = z.data.getReadPointer (p.dFormat == 1 ? 1 : 0);
        dataPos += noteRatio * p.dRate;
        if (dataPos >= span) dataPos = std::fmod (dataPos, (double) span);
        return dc.process (1, c[start + (int) dataPos] * 0.7f);
    }

    double sr = 44100.0;
    float noteHz = 261.6f, noteRatio = 1.0f;
    GrainCloud grains;
    SpectralVoice spec;
    // chaos
    double cx = 0.1, cy = 0.0, cz = 0.0, hx = 0.1, hy = 0.0;
    float cl = 0.4f, cPh = 0.0f, cPrev = 0.0f, cNext = 0.0f;
    // résonateur
    std::array<std::vector<float>, 4> lines;
    float lineLen[4] { 100, 100, 100, 100 }, lineLp[4] {};
    int lw = 0, numLines = 1, burst = 0;
    float fb = 0.99f, lpA = 0.5f;
    // bruit / data
    idg::dsp::SVF noiseTone, focus;
    idg::dsp::DCBlocker dc;
    float crackle = 0.0f;
    double dataPos = 0.0;
};

/** Empreinte spectrale appliquée à la voix : banc de passe-bandes dont les gains suivent l'empreinte. */
class FingerprintFilter
{
public:
    static constexpr int MaxBands = FingerprintData::NumBands;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (int b = 0; b < MaxBands; ++b) bp[b].set (sr, FingerprintData::bandFreq (b), 5.0f);
        reset();
    }
    void reset() { for (auto& f : bp) f.reset(); pos = 0.0; for (auto& g : gain) g = 0.0f; }

    /** Cadence de contrôle : position de lecture et gains des bandes. */
    void control (const FpParams& p, int numSamples)
    {
        active = p.on && p.fp != nullptr && ! p.fp->zones.empty() && p.fp->zones[0]->fingerprint != nullptr;
        if (! active) return;
        const auto& d = *p.fp->zones[0]->fingerprint;
        pos += p.speed * (double) numSamples / sr * FingerprintData::FrameRate;
        if (pos >= d.numFrames - 1) pos = p.loop ? std::fmod (pos, (double) juce::jmax (1, d.numFrames - 1)) : (double) (d.numFrames - 1);
        const int f = (int) pos;
        const int group = p.res == 0 ? 4 : (p.res == 1 ? 2 : 1); // 8, 16 ou 32 bandes
        for (int b = 0; b < MaxBands; ++b)
        {
            // résolution : moyenne des bandes voisines ; décalage des formants : lecture décalée
            const int src = juce::jlimit (0, MaxBands - 1, b - (int) std::round (p.shift));
            const int g0 = (src / group) * group;
            float e = 0.0f;
            for (int k = 0; k < group; ++k) e += d.env[(size_t) f * MaxBands + (size_t) juce::jmin (MaxBands - 1, g0 + k)];
            e /= (float) group;
            gain[b] += (e - gain[b]) * 0.5f;
        }
        amount = p.amount;
    }

    inline void process (float& l, float& r)
    {
        if (! active) return;
        float wl = 0.0f, wr = 0.0f;
        for (int b = 0; b < MaxBands; ++b)
        {
            const float g = gain[b] * 2.5f;
            wl += bp[b].bandpass (0, l) * g;
            wr += bp[b].bandpass (1, r) * g;
        }
        l += (wl - l) * amount;
        r += (wr - r) * amount;
    }

private:
    double sr = 44100.0, pos = 0.0;
    idg::dsp::SVF bp[MaxBands];
    float gain[MaxBands] {};
    float amount = 0.0f;
    bool active = false;
};
} // namespace idgs
