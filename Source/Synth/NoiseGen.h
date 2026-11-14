#pragma once
#include "../Core/Dsp.h"
#include "../Core/Params.h"

namespace idgs
{
/** Banque de bruits d'une voix (stéréo décorrélé). Tone : -1 passe-bas .. 0 neutre .. +1 passe-haut. */
class NoiseGen
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        reset (1);
    }

    void reset (uint32_t seed)
    {
        rng.seed (seed * 2654435761u + 17u);
        for (auto& p : pink) for (auto& v : p) v = 0.0f;
        tone.reset(); band.reset(); rumble.reset();
        crackle[0] = crackle[1] = 0.0f;
        holdPhase = 1.0f; held[0] = held[1] = 0.0f;
        drift = rng.uni() * 100.0f;
        rain[0] = rain[1] = 0.0f;
    }

    /** Réglages de contrôle : type, tone, fréquence de la note (pour le bruit digital). */
    void control (int newType, float toneAmt, float noteHz, int numSamples)
    {
        type = newType;
        toneVal = toneAmt;
        if (toneAmt < -0.01f) tone.set (sr, 20000.0f * std::pow (0.01f, -toneAmt), 0.7071f);
        else if (toneAmt > 0.01f) tone.set (sr, 20.0f * std::pow (400.0f, toneAmt), 0.7071f);
        holdInc = juce::jlimit (0.0001f, 1.0f, noteHz * 4.0f / (float) sr);
        drift += (float) numSamples / (float) sr;
        if (type == NoiseWind)
            band.set (sr, 300.0f + 2200.0f * idg::dsp::smoothNoise (drift * 0.4, 7u), 6.0f);
        else if (type == NoiseRadio)
            band.set (sr, 1200.0f + 1600.0f * idg::dsp::smoothNoise (drift * 0.15, 3u), 2.5f);
        rumble.setCutoff (sr, 60.0f);
    }

    inline void tick (float& l, float& r)
    {
        float v[2];
        for (int c = 0; c < 2; ++c) v[c] = gen (c);
        if (toneVal < -0.01f) { v[0] = tone.lowpass (0, v[0]); v[1] = tone.lowpass (1, v[1]); }
        else if (toneVal > 0.01f) { v[0] = tone.highpass (0, v[0]); v[1] = tone.highpass (1, v[1]); }
        l = v[0]; r = v[1];
    }

private:
    inline float white() { return rng.bi(); }

    inline float pinkOf (int c, float w)
    {
        auto& b = pink[c]; // filtre de Paul Kellet
        b[0] = 0.99886f * b[0] + w * 0.0555179f;
        b[1] = 0.99332f * b[1] + w * 0.0750759f;
        b[2] = 0.96900f * b[2] + w * 0.1538520f;
        b[3] = 0.86650f * b[3] + w * 0.3104856f;
        b[4] = 0.55000f * b[4] + w * 0.5329522f;
        b[5] = -0.7616f * b[5] - w * 0.0168980f;
        const float out = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362f;
        b[6] = w * 0.115926f;
        return out * 0.11f;
    }

    inline float gen (int c)
    {
        const float w = white();
        switch (type)
        {
            case NoiseWhite: return w * 0.5f;
            case NoisePink:  return pinkOf (c, w);
            case NoiseVinyl:
            {
                // souffle doux + craquements aléatoires + grondement
                float hiss = pinkOf (c, w) * 0.25f;
                if (rng.uni() < 0.00035f) crackle[c] = (rng.uni() < 0.5f ? -1.0f : 1.0f) * (0.4f + 0.6f * rng.uni());
                crackle[c] *= 0.82f;
                const float rmb = rumble.lp (c, w) * 1.5f;
                return hiss + crackle[c] + rmb;
            }
            case NoiseDigital:
            {
                // échantillonneur-bloqueur à la hauteur de la note, quantifié sur 4 bits
                if (c == 0)
                {
                    holdPhase += holdInc;
                    if (holdPhase >= 1.0f)
                    {
                        holdPhase -= std::floor (holdPhase);
                        held[0] = std::round (w * 8.0f) / 8.0f;
                        held[1] = std::round (white() * 8.0f) / 8.0f;
                    }
                }
                return held[c] * 0.5f;
            }
            case NoiseWind: return band.bandpass (c, w) * 1.8f;
            case NoiseRain:
            {
                // gouttes : petites impulsions de bruit à décroissance rapide
                if (rng.uni() < 0.004f) rain[c] = 0.3f + 0.7f * rng.uni();
                rain[c] *= 0.985f;
                return w * rain[c] * 0.8f;
            }
            case NoiseRadio:
            {
                const float flutter = 0.6f + 0.4f * idg::dsp::smoothNoise (drift * 9.0, 11u);
                const float x = band.bandpass (c, w) * 2.2f * flutter;
                return std::round (x * 24.0f) / 24.0f;
            }
            default: return w * 0.5f;
        }
    }

    double sr = 44100.0;
    idg::dsp::Rng rng;
    int type = 0;
    float toneVal = 0.0f;
    float pink[2][7] {};
    idg::dsp::SVF tone, band;
    idg::dsp::OnePole rumble;
    float crackle[2] {}, rain[2] {};
    float holdPhase = 1.0f, holdInc = 0.01f, held[2] {};
    double drift = 0.0;
};
} // namespace idgs
