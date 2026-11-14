#pragma once
#include "Widgets.h"

namespace idgs::ui
{
/** Onglet SYNTH : Osc A, enveloppe 1, voix. Les emplacements des modules à venir sont déjà dessinés. */
class SynthPage : public juce::Component
{
public:
    explicit SynthPage (IDGemsSynthProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

private:
    IDGemsSynthProcessor& proc;

    // Osc A
    ParamToggle oscOn;
    ParamCombo table;
    WavetableView wtView;
    Knob pos, oct, semi, fine, level, pan, phase, phRand;

    // Enveloppe 1
    EnvelopeView envView;
    Knob delay, attack, hold, decay, sustain, release, aCurve, dCurve, rCurve;

    // Voix
    Knob voices, velSens, bendRange;

    juce::Rectangle<int> oscArea, envArea, voiceArea, futureOsc, futureFilter;
};
} // namespace idgs::ui
