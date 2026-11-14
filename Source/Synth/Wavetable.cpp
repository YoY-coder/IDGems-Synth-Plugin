#include "Wavetable.h"
#include <complex>

namespace idgs
{
namespace
{
/** FFT inverse radix-2 (non normalisée) : x[n] = somme_k X[k] e^{+i 2 pi k n / N}. */
void inverseFFT (std::vector<std::complex<double>>& x)
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
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
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

/** Synthétise un cycle à partir d'un spectre, limité à `maxHarm` harmoniques. */
void synthesise (const Wavetable::Spectrum& s, int maxHarm, float* out)
{
    constexpr int N = Wavetable::Size;
    std::vector<std::complex<double>> X ((size_t) N, { 0.0, 0.0 });
    for (int k = 1; k <= maxHarm && k < N / 2; ++k)
    {
        // a cos + b sin = Re((a - i b) e^{i w})  ->  X[k] = (a - i b) / 2, X[N-k] = conjugué
        const std::complex<double> c (0.5 * s.a[(size_t) k], -0.5 * s.b[(size_t) k]);
        X[(size_t) k] = c;
        X[(size_t) (N - k)] = std::conj (c);
    }
    if (maxHarm >= N / 2) X[(size_t) (N / 2)] = { s.a[(size_t) (N / 2)], 0.0 };
    inverseFFT (X);
    for (int i = 0; i < N; ++i) out[i] = (float) X[(size_t) i].real();
    out[N] = out[0];
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

/** Impulsion de rapport cyclique d (0..1), centrée pour éviter les sauts de phase en morphing. */
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
} // namespace

Wavetable::Wavetable (const juce::String& n, const std::vector<Spectrum>& frames)
    : name (n), numFrames ((int) frames.size())
{
    data.assign ((size_t) numFrames * NumMips * (Size + 1), 0.0f);
    for (int f = 0; f < numFrames; ++f)
    {
        for (int m = 0; m < NumMips; ++m)
            synthesise (frames[(size_t) f], MaxHarmonics >> m, const_cast<float*> (table (f, m)));

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
    }
}

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
