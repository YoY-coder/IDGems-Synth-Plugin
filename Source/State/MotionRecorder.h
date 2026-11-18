#pragma once
#include "../Core/Params.h"

class IDGemsSynthProcessor;

namespace idgs
{
/** Motion recording : enregistre les gestes sur les 8 macros pendant N mesures, puis les rejoue en boucle
    (calé sur le temps de l'hôte, ou une horloge interne). Thread de l'interface. */
class MotionRecorder : private juce::Timer
{
public:
    enum State { Idle, Recording, Playing };
    explicit MotionRecorder (IDGemsSynthProcessor& p) : proc (p) { startTimerHz (60); }
    ~MotionRecorder() override { stopTimer(); }

    void record (int bars);
    void play();
    void stop();
    void clear();
    State getState() const { return state; }
    int getBars() const { return bars; }
    bool hasData() const { return ! frames.empty(); }
    float phase() const { return curPhase; }            // 0..1 dans la boucle
    bool macroUsed (int m) const { return used[m]; }
    /** Valeur enregistrée de la macro m à la position x (0..1). */
    float valueAt (int m, float x) const;

    juce::ValueTree toTree() const;
    void fromTree (const juce::ValueTree&);

    /** Test : avance d'un pas avec la position donnée (en noires). */
    void step (double beat) { tick (beat); }

private:
    void timerCallback() override { tick (-1.0); }
    void tick (double beat);
    struct Frame { float t; float v[NumMacros]; };
    IDGemsSynthProcessor& proc;
    State state = Idle;
    int bars = 2;
    double startBeat = 0.0;
    std::vector<Frame> frames;
    float startValues[NumMacros] {};
    bool used[NumMacros] {};
    float curPhase = 0.0f;
};
} // namespace idgs
