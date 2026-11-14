#pragma once
#include "../Core/Dsp.h"
#include "../Core/Params.h"

namespace idgs
{
/** Filtre stéréo d'une voix. Les coefficients sont recalculés à cadence de contrôle (setParams),
    le traitement se fait échantillon par échantillon. Aucune allocation hors prepare(). */
class VoiceFilter
{
public:
    static constexpr int CombSize = 8192;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& c : comb) c.assign (CombSize, 0.0f);
        reset();
    }

    void reset()
    {
        s1.reset(); s2.reset();
        for (auto& f : formants) f.reset();
        for (auto& ch : ladder) for (auto& v : ch) v = 0.0f;
        for (auto& c : comb) std::fill (c.begin(), c.end(), 0.0f);
        combW = 0;
        holdPhase = 1.0f;
        held[0] = held[1] = 0.0f;
        first = true;
    }

    /** cutoffHz : déjà multiplié par le keytracking. reso, drive, mix : 0..1. */
    void setParams (int newType, float cutoffHz, float newReso, float newDrive, float newMix)
    {
        if (newType != type) { type = newType; reset(); }
        const float nyq = (float) sr * 0.45f;
        cutoffHz = juce::jlimit (20.0f, nyq, cutoffHz);
        // lissage logarithmique de la coupure (pas de zipper quand on tourne le potard)
        if (first) { smoothCut = cutoffHz; first = false; }
        else smoothCut *= std::pow (cutoffHz / smoothCut, 0.35f);
        reso = newReso;
        driveGain = newDrive > 0.001f ? idg::dsp::dbToGain (newDrive * 30.0f) : 1.0f;
        mix = newMix;
        const float fc = smoothCut;

        switch (type)
        {
            case FiltLP12: case FiltHP12: case FiltBP12: case FiltNotch12:
                s1.set (sr, fc, 0.5f + reso * reso * 24.0f);
                break;
            case FiltLP24: case FiltHP24: case FiltBP24: case FiltNotch24:
                s1.set (sr, fc, 0.54f);
                s2.set (sr, fc, 0.6f + reso * reso * 18.0f);
                break;
            case FiltLadder:
                ladderG = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * fc / (float) sr);
                ladderK = reso * 4.1f;
                break;
            case FiltCombPlus: case FiltCombMinus:
                combDelay = juce::jlimit (2.0f, (float) CombSize - 4.0f, (float) sr / fc);
                combFb = reso * 0.97f * (type == FiltCombMinus ? -1.0f : 1.0f);
                break;
            case FiltFormant:
            {
                // la coupure parcourt les voyelles A - E - I - O - U (de 20 Hz à 20 kHz)
                static const float vowels[5][3] { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 350, 2300, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
                const float t = juce::jlimit (0.0f, 1.0f, std::log (fc / 20.0f) / std::log (1000.0f)) * 4.0f;
                const int a = juce::jmin (3, (int) t);
                const float x = t - (float) a;
                const float q = 4.0f + reso * 20.0f;
                for (int k = 0; k < 3; ++k)
                    formants[k].set (sr, vowels[a][k] + (vowels[a + 1][k] - vowels[a][k]) * x, q);
                break;
            }
            case FiltDestroy:
                holdInc = juce::jlimit (0.0005f, 1.0f, 2.0f * fc / (float) sr);
                levels = std::pow (2.0f, 16.0f - reso * 14.0f);
                break;
            default: break;
        }
    }

    inline void process (float& l, float& r)
    {
        float x[2] { l, r };
        float y[2];
        for (int c = 0; c < 2; ++c)
        {
            float in = x[c];
            if (driveGain > 1.0f) in = idg::dsp::fastTanh (in * driveGain) * (1.0f + 0.15f * std::log (driveGain));
            y[c] = processOne (c, in);
        }
        l = x[0] + (y[0] - x[0]) * mix;
        r = x[1] + (y[1] - x[1]) * mix;
    }

    void sanitize()
    {
        s1.sanitize(); s2.sanitize();
        for (auto& f : formants) f.sanitize();
        for (auto& ch : ladder) for (auto& v : ch) if (! std::isfinite (v)) v = 0.0f;
    }

private:
    inline float processOne (int c, float in)
    {
        switch (type)
        {
            case FiltLP12:    return s1.lowpass (c, in);
            case FiltHP12:    return s1.highpass (c, in);
            case FiltBP12:    return s1.bandpass (c, in) * (0.5f + reso);
            case FiltNotch12: return s1.notch (c, in);
            case FiltLP24:    return s2.lowpass (c, s1.lowpass (c, in));
            case FiltHP24:    return s2.highpass (c, s1.highpass (c, in));
            case FiltBP24:    return s2.bandpass (c, s1.bandpass (c, in)) * (1.0f + reso);
            case FiltNotch24: return s2.notch (c, s1.notch (c, in));
            case FiltLadder:
            {
                auto& s = ladder[c];
                const float u = idg::dsp::fastTanh (in - ladderK * s[3]);
                s[0] += ladderG * (u - s[0]);
                s[1] += ladderG * (s[0] - s[1]);
                s[2] += ladderG * (s[1] - s[2]);
                s[3] += ladderG * (s[2] - s[3]);
                return s[3] * (1.0f + ladderK * 0.6f);
            }
            case FiltCombPlus: case FiltCombMinus:
            {
                auto& buf = comb[c];
                const float rp = (float) combW - combDelay;
                const int i0 = ((int) std::floor (rp)) & (CombSize - 1);
                const float fr = rp - std::floor (rp);
                const float d = buf[(size_t) i0] + (buf[(size_t) ((i0 + 1) & (CombSize - 1))] - buf[(size_t) i0]) * fr;
                const float out = in + combFb * d;
                buf[(size_t) combW] = idg::dsp::fastTanh (out * 0.7f) / 0.7f;
                if (c == 1) combW = (combW + 1) & (CombSize - 1);
                return out * (1.0f - std::abs (combFb) * 0.5f);
            }
            case FiltFormant:
                return (formants[0].bandpass (c, in) + 0.6f * formants[1].bandpass (c, in) + 0.35f * formants[2].bandpass (c, in)) * 1.6f;
            case FiltDestroy:
            {
                if (c == 0)
                {
                    holdPhase += holdInc;
                    sampleNow = holdPhase >= 1.0f;
                    if (sampleNow) holdPhase -= std::floor (holdPhase);
                }
                if (sampleNow) held[c] = std::round (in * levels) / levels;
                return held[c];
            }
            default: return in;
        }
    }

    double sr = 44100.0;
    int type = -1;
    bool first = true;
    float smoothCut = 1000.0f, reso = 0.0f, driveGain = 1.0f, mix = 1.0f;
    idg::dsp::SVF s1, s2, formants[3];
    float ladder[2][4] {};
    float ladderG = 0.1f, ladderK = 0.0f;
    std::vector<float> comb[2];
    int combW = 0;
    float combDelay = 100.0f, combFb = 0.0f;
    float holdPhase = 1.0f, holdInc = 1.0f, levels = 65536.0f, held[2] {};
    bool sampleNow = true;
};
} // namespace idgs
