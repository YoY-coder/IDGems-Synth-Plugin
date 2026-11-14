#pragma once
#include "../Core/JuceIncludes.h"

namespace idgs
{
/** Enveloppe DAHDSR avec courbes réglables (attack, decay, release).
    - Les temps nuls sont remplacés par un minimum très court pour ne jamais produire de clic.
    - Un nouveau déclenchement repart du niveau courant (pas de saut).
    Aucune allocation : utilisable dans le thread audio. */
class Envelope
{
public:
    struct Settings
    {
        float delayMs = 0, attackMs = 2, holdMs = 0, decayMs = 400, sustain = 0.8f, releaseMs = 250;
        float attackCurve = 0, decayCurve = -0.5f, releaseCurve = -0.5f; // -1..1
    };

    enum class Stage { Idle, Delay, Attack, Hold, Decay, Sustain, Release };

    static constexpr float minAttackMs = 1.0f;
    static constexpr float minReleaseMs = 1.5f;

    void prepare (double sampleRate) { sr = sampleRate; reset(); }
    void reset() { stage = Stage::Idle; level = 0.0f; t = 0.0f; }

    void noteOn()
    {
        startLevel = level;
        enter (Stage::Delay);
    }

    void noteOff()
    {
        if (stage == Stage::Idle || stage == Stage::Release) return;
        startLevel = level;
        enter (Stage::Release);
    }

    bool isActive() const { return stage != Stage::Idle; }
    bool isReleasing() const { return stage == Stage::Release; }
    Stage getStage() const { return stage; }
    float getLevel() const { return level; }

    /** Courbe normalisée : c = 0 linéaire, c > 0 démarrage lent, c < 0 démarrage rapide. */
    static float shape (float x, float c)
    {
        if (std::abs (c) < 1.0e-3f) return x;
        const float k = c * 8.0f;
        return (std::exp (k * x) - 1.0f) / (std::exp (k) - 1.0f);
    }

    /** Avance d'un échantillon et renvoie le niveau (0..1). */
    inline float next (const Settings& s)
    {
        switch (stage)
        {
            case Stage::Idle: return 0.0f;

            case Stage::Delay:
                if (advance (s.delayMs)) enter (Stage::Attack);
                return level;

            case Stage::Attack:
            {
                const bool done = advance (juce::jmax (minAttackMs, s.attackMs));
                level = startLevel + (1.0f - startLevel) * shape (t, s.attackCurve);
                if (done) { level = 1.0f; enter (Stage::Hold); }
                return level;
            }

            case Stage::Hold:
                if (advance (s.holdMs)) enter (Stage::Decay);
                return level;

            case Stage::Decay:
            {
                const bool done = advance (juce::jmax (minAttackMs, s.decayMs));
                // courbe inversée : c < 0 = chute rapide puis lente (comportement « analogique »)
                level = 1.0f + (s.sustain - 1.0f) * (1.0f - shape (1.0f - t, -s.decayCurve));
                if (done) enter (Stage::Sustain);
                return level;
            }

            case Stage::Sustain:
                // suit le réglage de sustain en douceur (pas de saut si on tourne le potard)
                level += (s.sustain - level) * sustainCoef;
                return level;

            case Stage::Release:
            {
                const bool done = advance (juce::jmax (minReleaseMs, s.releaseMs));
                level = startLevel * shape (1.0f - t, -s.releaseCurve);
                if (done) { level = 0.0f; stage = Stage::Idle; }
                return level;
            }
        }
        return level;
    }

private:
    void enter (Stage s)
    {
        stage = s;
        t = 0.0f;
        if (s == Stage::Sustain) sustainCoef = 1.0f - std::exp (-1.0f / (0.005f * (float) sr));
    }

    /** Fait progresser t (0..1) sur la durée donnée. Renvoie true quand l'étape est finie. */
    inline bool advance (float ms)
    {
        if (ms <= 0.0f) { t = 1.0f; return true; }
        t += 1000.0f / (ms * (float) sr);
        if (t >= 1.0f) { t = 1.0f; return true; }
        return false;
    }

    double sr = 44100.0;
    Stage stage = Stage::Idle;
    float level = 0.0f, startLevel = 0.0f, t = 0.0f, sustainCoef = 0.001f;
};
} // namespace idgs
