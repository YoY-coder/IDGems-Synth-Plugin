#pragma once
#include "../PluginProcessor.h"
#include "Theme.h"

namespace idgs::ui
{
using idg::ui::Theme;

/** Potard lié à un paramètre (style néon d'IDGems). Double-clic = valeur par défaut,
    La valeur s'affiche au survol. */
class Knob : public juce::Slider
{
public:
    Knob (IDGemsSynthProcessor& p, const juce::String& paramId, const juce::String& label, juce::Colour colour = Theme::accent);
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { hovering = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hovering = false; repaint(); }

private:
    juce::RangedAudioParameter* param = nullptr;
    juce::String label;
    juce::Colour colour;
    bool hovering = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

/** Bouton lié à un paramètre booléen. */
class ParamToggle : public juce::TextButton
{
public:
    ParamToggle (IDGemsSynthProcessor& p, const juce::String& paramId, const juce::String& text, juce::Colour on = Theme::accent);

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
};

/** Liste déroulante liée à un paramètre de choix. */
class ParamCombo : public juce::ComboBox
{
public:
    ParamCombo (IDGemsSynthProcessor& p, const juce::String& paramId);

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
};

/** Vu-mètre stéréo + réduction de gain du limiteur. */
class Meter : public juce::Component
{
public:
    explicit Meter (IDGemsSynthProcessor& p) : proc (p) {}
    void tick();
    void paint (juce::Graphics&) override;

private:
    IDGemsSynthProcessor& proc;
    float level[2] {}, peakHold[2] {}, gr = 1.0f;
    int holdTime[2] {};
};

/** Affichage de la wavetable : frames empilées en perspective, frame courante en surbrillance. */
class WavetableView : public juce::Component
{
public:
    explicit WavetableView (IDGemsSynthProcessor& p) : proc (p) {}
    void tick();
    void paint (juce::Graphics&) override;

private:
    IDGemsSynthProcessor& proc;
    int lastTable = -1;
    float lastPos = -1.0f;
};

/** Courbe de l'enveloppe 1 (DAHDSR + courbes). */
class EnvelopeView : public juce::Component
{
public:
    explicit EnvelopeView (IDGemsSynthProcessor& p) : proc (p) {}
    void tick();
    void paint (juce::Graphics&) override;

private:
    IDGemsSynthProcessor& proc;
    float lastHash = -1.0f;
};

/** Oscilloscope de sortie (version simple ; la version « corrompue » arrive avec la DA complète). */
class Scope : public juce::Component
{
public:
    explicit Scope (IDGemsSynthProcessor& p) : proc (p) {}
    void tick();
    void paint (juce::Graphics&) override;

private:
    IDGemsSynthProcessor& proc;
    std::array<float, 512> buf {};
    int writePos = 0;
};
} // namespace idgs::ui
