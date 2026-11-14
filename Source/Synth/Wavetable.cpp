#include "Wavetable.h"
#include <complex>

namespace idgs
{
namespace
{
using cd = std::complex<double>;

/** FFT radix-2 (non normalisée), signe +1 : x[n] = somme_k X[k] e^{+i 2 pi k n / N}. */
void fftPlus (std::vector<cd>& x)
{
    const size_t n = x.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (x[i], x[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = 2.0 * juce::MathConstants<double>::pi / (double) len;
        const cd wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            cd w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                auto u = x[i + k], v = x[i + k + len / 2] * w;
                x[i + k] = u + v;
                x[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

/** Analyse d'un cycle : X[k] = somme_n x[n] e^{-i 2 pi k n / N}. */
void fftMinus (std::vector<cd>& x)
{
    for (auto& v : x) v = std::conj (v);
    fftPlus (x);
    for (auto& v : x) v = std::conj (v);
}

/** Synthétise un cycle à partir d'un spectre, limité à `maxHarm` harmoniques. */
void synthesise (const Wavetable::Spectrum& s, int maxHarm, float* out)
{
    constexpr int N = Wavetable::Size;
    std::vector<cd> X ((size_t) N, { 0.0, 0.0 });
    for (int k = 1; k <= maxHarm && k < N / 2; ++k)
    {
        // a cos + b sin = Re((a - i b) e^{i w})  ->  X[k] = (a - i b) / 2, X[N-k] = conjugué
        const cd c (0.5 * s.a[(size_t) k], -0.5 * s.b[(size_t) k]);
        X[(size_t) k] = c;
        X[(size_t) (N - k)] = std::conj (c);
    }
    fftPlus (X);
    for (int i = 0; i < N; ++i) out[i] = (float) X[(size_t) i].real();
    out[N] = out[0];
}

/** Spectre d'un cycle brut (Size échantillons). La composante continue est ignorée. */
Wavetable::Spectrum analyse (const float* cycle)
{
    constexpr int N = Wavetable::Size;
    std::vector<cd> X ((size_t) N);
    for (int i = 0; i < N; ++i) X[(size_t) i] = { (double) cycle[i], 0.0 };
    fftMinus (X);
    Wavetable::Spectrum s;
    for (int k = 1; k < N / 2; ++k)
    {
        s.a[(size_t) k] = (float) (2.0 * X[(size_t) k].real() / N);
        s.b[(size_t) k] = (float) (-2.0 * X[(size_t) k].imag() / N);
    }
    return s;
}

using Spec = Wavetable::Spectrum;
constexpr int H = Wavetable::MaxHarmonics;
constexpr double PI = juce::MathConstants<double>::pi;

Spec sineSpec() { Spec s; s.b[1] = 1.0f; return s; }
Spec sawSpec() { Spec s; for (int k = 1; k <= H; ++k) s.b[(size_t) k] = (float) ((k % 2 ? 1.0 : -1.0) / k); return s; }
Spec squareSpec() { Spec s; for (int k = 1; k <= H; k += 2) s.b[(size_t) k] = (float) (1.0 / k); return s; }
Spec triSpec()
{
    Spec s;
    for (int k = 1; k <= H; k += 2) s.b[(size_t) k] = (float) ((((k - 1) / 2) % 2 ? -1.0 : 1.0) / ((double) k * k));
    return s;
}

/** Impulsion de rapport cyclique d (0..1). */
Spec pulseSpec (double d)
{
    Spec s;
    for (int k = 1; k <= H; ++k) s.a[(size_t) k] = (float) (2.0 / (k * PI) * std::sin (k * PI * d));
    return s;
}

/** Scie progressivement éclaircie (« ouverture de filtre » figée dans la table). */
Spec brightSpec (int frame, int numFrames)
{
    Spec s;
    const double cutoff = std::pow (2.0, 1.0 + 9.0 * frame / (double) (numFrames - 1)); // 2 .. 1024 harmoniques
    for (int k = 1; k <= H; ++k)
    {
        const double w = 1.0 / std::sqrt (1.0 + std::pow (k / cutoff, 6.0));
        s.b[(size_t) k] = (float) ((k % 2 ? 1.0 : -1.0) / k * w);
    }
    return s;
}

/** Formant glissant : harmoniques pondérées par deux bosses de résonance qui se déplacent (voyelles). */
Spec formantSpec (int frame, int numFrames)
{
    Spec s;
    const double t = frame / (double) (numFrames - 1);
    const double f1 = 2.0 + 10.0 * t, f2 = 6.0 + 34.0 * t * t;
    for (int k = 1; k <= H; ++k)
    {
        const double g = std::exp (-0.5 * std::pow ((k - f1) / 1.6, 2.0)) + 0.7 * std::exp (-0.5 * std::pow ((k - f2) / 3.0, 2.0));
        s.b[(size_t) k] = (float) (g / std::sqrt ((double) k) + 0.02 / k);
    }
    return s;
}

/** Spectre aléatoire « caractérisé » : pente, densité, parité, bosses de formant, phases. */
Spec randomSpec (juce::Random& r)
{
    Spec s;
    const double slope = 0.3 + r.nextDouble() * 1.8;
    const double density = 0.15 + r.nextDouble() * 0.85;
    const int maxH = 1 << (2 + r.nextInt (9));            // 4 .. 1024 harmoniques
    const int parity = r.nextInt (4);                       // 0 tout, 1 impaires, 2 paires renforcées, 3 tout
    const int bumps = r.nextInt (3);
    double bumpC[2] { 2.0 + r.nextDouble() * 40.0, 4.0 + r.nextDouble() * 120.0 };
    double bumpW[2] { 0.8 + r.nextDouble() * 6.0, 2.0 + r.nextDouble() * 20.0 };
    const bool randomPhase = r.nextFloat() < 0.4f;
    const bool noisy = r.nextFloat() < 0.15f;
    for (int k = 1; k <= juce::jmin (H, maxH); ++k)
    {
        if (k > 1 && r.nextDouble() > density) continue;
        if (parity == 1 && k % 2 == 0) continue;
        double a = std::pow ((double) k, -slope);
        if (parity == 2 && k % 2 == 0) a *= 2.5;
        for (int b = 0; b < bumps; ++b) a += 0.6 * std::exp (-0.5 * std::pow ((k - bumpC[b]) / bumpW[b], 2.0)) / std::sqrt ((double) k);
        if (noisy) a *= 0.3 + r.nextDouble();
        const double ph = randomPhase ? r.nextDouble() * 2.0 * PI : 0.0;
        s.a[(size_t) k] = (float) (a * std::sin (ph));
        s.b[(size_t) k] = (float) (a * std::cos (ph));
    }
    return s;
}

/** Cycle brut tiré au sort dans le domaine temporel : marche aléatoire lissée, puis repliée. */
std::vector<float> randomCycle (juce::Random& r)
{
    constexpr int N = Wavetable::Size;
    std::vector<float> c ((size_t) N);
    const int points = 3 + r.nextInt (24);
    std::vector<float> pts ((size_t) points);
    for (auto& p : pts) p = r.nextFloat() * 2.0f - 1.0f;
    const float fold = 1.0f + r.nextFloat() * 4.0f * (r.nextFloat() < 0.5f ? 1.0f : 0.0f);
    const bool steps = r.nextFloat() < 0.25f;
    for (int i = 0; i < N; ++i)
    {
        const float x = (float) i / (float) N * (float) points;
        const int a = (int) x;
        float f = x - (float) a;
        if (steps) f = 0.0f; else f = f * f * (3.0f - 2.0f * f);
        float v = pts[(size_t) a] + (pts[(size_t) ((a + 1) % points)] - pts[(size_t) a]) * f;
        v = std::sin (v * fold * 1.5707963f);
        c[(size_t) i] = v;
    }
    return c;
}
} // namespace

// =============================================================================
Wavetable::Wavetable (const juce::String& n, const std::vector<Spectrum>& frames)
    : name (n), numFrames (juce::jlimit (1, MaxFrames, (int) frames.size()))
{
    data.assign ((size_t) numFrames * NumMips * (Size + 1), 0.0f);
    pAmp.assign ((size_t) numFrames * NumPartials, 0.0f);
    pPhase.assign ((size_t) numFrames * NumPartials, 0.0f);
    for (int f = 0; f < numFrames; ++f)
    {
        const auto& spec = frames[(size_t) f];
        for (int m = 0; m < NumMips; ++m)
            synthesise (spec, MaxHarmonics >> m, const_cast<float*> (table (f, m)));

        // même normalisation pour toutes les versions d'une frame (crête de la version complète)
        float peak = 0.0f;
        const float* full = table (f, 0);
        for (int i = 0; i < Size; ++i) peak = juce::jmax (peak, std::abs (full[i]));
        const float g = peak > 1.0e-6f ? 1.0f / peak : 0.0f;
        for (int m = 0; m < NumMips; ++m)
        {
            auto* t = const_cast<float*> (table (f, m));
            for (int i = 0; i <= Size; ++i) t[i] *= g;
        }
        // harmoniques pour la synthèse additive : a cos + b sin = A sin (x + phi)
        for (int k = 1; k <= NumPartials; ++k)
        {
            const float a = spec.a[(size_t) k] * g, b = spec.b[(size_t) k] * g;
            pAmp[(size_t) f * NumPartials + (size_t) (k - 1)] = std::sqrt (a * a + b * b);
            pPhase[(size_t) f * NumPartials + (size_t) (k - 1)] = std::atan2 (a, b) / (2.0f * juce::MathConstants<float>::pi);
        }
    }
}

std::unique_ptr<Wavetable> Wavetable::fromFrames (const juce::String& n, const std::vector<float>& cycles)
{
    const int frames = juce::jlimit (1, MaxFrames, (int) (cycles.size() / (size_t) Size));
    std::vector<Spectrum> specs;
    specs.reserve ((size_t) frames);
    for (int f = 0; f < frames; ++f)
    {
        if ((size_t) (f + 1) * Size > cycles.size()) { specs.push_back (sineSpec()); continue; }
        specs.push_back (analyse (cycles.data() + (size_t) f * Size));
    }
    if (specs.empty()) specs.push_back (sineSpec());
    return std::make_unique<Wavetable> (n, specs);
}

std::unique_ptr<Wavetable> Wavetable::fromSample (const juce::String& n, const float* x, int len)
{
    if (x == nullptr || len < 64) return fromFrames (n, std::vector<float> ((size_t) Size, 0.0f));

    // 1. détection de période par autocorrélation normalisée (30 Hz .. 2 kHz à 44.1 kHz)
    const int start = juce::jmin (len / 10, 4096);
    const int win = juce::jmin (len - start, 8192);
    int period = 0;
    if (win > 3000)
    {
        double best = 0.0;
        const int minLag = 22, maxLag = juce::jmin (1470, win / 2);
        for (int lag = minLag; lag < maxLag; ++lag)
        {
            double num = 0.0, e0 = 0.0, e1 = 0.0;
            for (int i = 0; i < win - lag; i += 2)
            {
                const double a = x[start + i], b = x[start + i + lag];
                num += a * b; e0 += a * a; e1 += b * b;
            }
            const double c = num / std::sqrt (e0 * e1 + 1.0e-12);
            if (c > best + 0.02) { best = c; period = lag; }
        }
        if (best < 0.7) period = 0;
    }

    std::vector<float> cycles;
    if (period > 0)
    {
        // 2a. son périodique : un cycle par frame, rééchantillonné à 2048 points (bouclage parfait)
        const int maxFrames = juce::jlimit (1, 64, len / period - 1);
        for (int f = 0; f < maxFrames; ++f)
        {
            const int off = (int) ((double) f / juce::jmax (1, maxFrames - 1) * (double) (len - period - 1));
            for (int i = 0; i < Size; ++i)
            {
                const double p = off + (double) i / Size * period;
                const int a = (int) p;
                const float fr = (float) (p - a);
                cycles.push_back (x[a] + (x[juce::jmin (a + 1, len - 1)] - x[a]) * fr);
            }
        }
    }
    else
    {
        // 2b. son non périodique : fenêtres fixes de 2048 échantillons, fondu au bouclage
        const int maxFrames = juce::jlimit (1, 64, len / Size);
        constexpr int xf = 128;
        for (int f = 0; f < maxFrames; ++f)
        {
            const int off = maxFrames > 1 ? (int) ((double) f / (maxFrames - 1) * (double) juce::jmax (0, len - Size - xf - 1)) + xf : 0;
            for (int i = 0; i < Size; ++i)
            {
                float v = off + i < len ? x[off + i] : 0.0f;
                if (i >= Size - xf && off >= xf)
                {
                    // fin du cycle fondue avec ce qui précède le début : la jonction devient continue
                    const float t = (float) (i - (Size - xf)) / (float) xf;
                    v = v * (1.0f - t) + x[off - xf + (i - (Size - xf))] * t;
                }
                cycles.push_back (v);
            }
        }
    }
    return fromFrames (n, cycles);
}

std::unique_ptr<Wavetable> Wavetable::random (uint32_t seed)
{
    juce::Random r ((juce::int64) seed);
    const int framesChoices[] { 4, 8, 16, 32, 64 };
    const int frames = framesChoices[r.nextInt (5)];
    const int keys = 2 + r.nextInt (3);
    std::vector<Spectrum> keySpecs;
    for (int k = 0; k < keys; ++k)
    {
        if (r.nextFloat() < 0.3f)
        {
            auto c = randomCycle (r);
            keySpecs.push_back (analyse (c.data()));
        }
        else keySpecs.push_back (randomSpec (r));
    }
    const bool scramble = r.nextFloat() < 0.2f; // frames tirées sans continuité (table « cassée »)
    std::vector<Spectrum> out;
    for (int f = 0; f < frames; ++f)
    {
        if (scramble && r.nextFloat() < 0.5f) { out.push_back (randomSpec (r)); continue; }
        const double t = (double) f / juce::jmax (1, frames - 1) * (keys - 1);
        const int a = juce::jmin ((int) t, keys - 2);
        double x = t - a;
        x = x * x * (3.0 - 2.0 * x);
        Spectrum s;
        for (int k = 1; k <= H; ++k)
        {
            s.a[(size_t) k] = (float) (keySpecs[(size_t) a].a[(size_t) k] * (1.0 - x) + keySpecs[(size_t) a + 1].a[(size_t) k] * x);
            s.b[(size_t) k] = (float) (keySpecs[(size_t) a].b[(size_t) k] * (1.0 - x) + keySpecs[(size_t) a + 1].b[(size_t) k] * x);
        }
        out.push_back (s);
    }
    return std::make_unique<Wavetable> ("Random #" + juce::String ((int) (seed % 100000)), out);
}

// =============================================================================
WavetableBank::WavetableBank()
{
    tables.push_back (std::make_unique<Wavetable> ("Basique", std::vector<Spec> { sineSpec(), triSpec(), sawSpec(), squareSpec() }));

    std::vector<Spec> f;
    constexpr int frames = 16;
    for (int i = 0; i < frames; ++i) f.push_back (brightSpec (i, frames));
    tables.push_back (std::make_unique<Wavetable> ("Harmoniques", f));

    f.clear();
    for (int i = 0; i < frames; ++i) f.push_back (pulseSpec (0.5 - 0.47 * i / (double) (frames - 1)));
    tables.push_back (std::make_unique<Wavetable> ("Pulse (PWM)", f));

    f.clear();
    for (int i = 0; i < frames; ++i) f.push_back (formantSpec (i, frames));
    tables.push_back (std::make_unique<Wavetable> ("Formant", f));
}

const WavetableBank& WavetableBank::get()
{
    static const WavetableBank bank; // construit au premier appel (thread de l'interface / chargement)
    return bank;
}

const juce::StringArray& WavetableBank::names()
{
    static const juce::StringArray n { "Basique", "Harmoniques", "Pulse (PWM)", "Formant" };
    return n;
}
} // namespace idgs
