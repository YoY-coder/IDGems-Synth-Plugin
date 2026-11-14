#pragma once
#include "../Core/JuceIncludes.h"

namespace idgs
{
/** Wavetable à bande limitée : chaque frame existe en 11 versions (« mipmaps »), une par octave,
    dont les harmoniques au-dessus de Nyquist ont été retirées. L'oscillateur choisit la bonne
    version selon la hauteur jouée : pas d'aliasing, quelle que soit la note.

    Les tables sont construites hors du thread audio puis jamais modifiées : la lecture est sûre
    depuis n'importe quelle voix, sans verrou. */
class Wavetable
{
public:
    static constexpr int Size = 2048;           // échantillons par cycle
    static constexpr int MaxHarmonics = Size / 2;
    static constexpr int NumMips = 11;          // 1024, 512, ... 1 harmonique

    /** Spectre d'une frame : pour chaque harmonique k (1..1024), coefficients cosinus (a) et sinus (b). */
    struct Spectrum
    {
        std::vector<float> a = std::vector<float> (MaxHarmonics + 1, 0.0f);
        std::vector<float> b = std::vector<float> (MaxHarmonics + 1, 0.0f);
    };

    Wavetable() = default;
    Wavetable (const juce::String& name, const std::vector<Spectrum>& frames);

    const juce::String& getName() const { return name; }
    int getNumFrames() const { return numFrames; }

    /** Pointeur vers une table (Size + 1 échantillons, le dernier = le premier pour l'interpolation). */
    const float* table (int frame, int mip) const
    {
        return data.data() + ((size_t) frame * NumMips + (size_t) mip) * (size_t) (Size + 1);
    }

    /** Version à utiliser pour un incrément de phase donné (cycles par échantillon). */
    static int mipFor (float increment)
    {
        // harmoniques autorisées : 1024 >> mip <= 0.5 / increment
        const float h = 2.0f * increment * (float) MaxHarmonics;
        if (h <= 1.0f) return 0;
        const int m = (int) std::ceil (std::log2 (h));
        return juce::jlimit (0, NumMips - 1, m);
    }

    /** Lecture interpolée d'une frame (phase 0..1). */
    inline float read (int frame, int mip, float phase) const
    {
        const float* t = table (frame, mip);
        const float p = phase * (float) Size;
        const int i = juce::jlimit (0, Size - 1, (int) p);
        const float f = p - (float) i;
        return t[i] + (t[i + 1] - t[i]) * f;
    }

    /** Lecture avec fondu continu entre frames (position 0..1). */
    inline float readMorph (float position, int mip, float phase) const
    {
        const float fp = juce::jlimit (0.0f, 1.0f, position) * (float) (numFrames - 1);
        const int f0 = (int) fp;
        const int f1 = juce::jmin (f0 + 1, numFrames - 1);
        const float x = fp - (float) f0;
        const float a = read (f0, mip, phase);
        return x <= 0.0f ? a : a + (read (f1, mip, phase) - a) * x;
    }

private:
    juce::String name;
    int numFrames = 0;
    std::vector<float> data;
};

/** Banque des wavetables d'usine (construites une seule fois, partagées par toutes les instances). */
class WavetableBank
{
public:
    static const WavetableBank& get();
    static const juce::StringArray& names();

    int size() const { return (int) tables.size(); }
    const Wavetable& operator[] (int i) const { return *tables[(size_t) juce::jlimit (0, size() - 1, i)]; }

private:
    WavetableBank();
    std::vector<std::unique_ptr<Wavetable>> tables;
};
} // namespace idgs
