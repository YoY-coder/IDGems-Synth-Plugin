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
    void setLabel (const juce::String& l) { label = l; repaint(); }

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

/** Affichage d'un oscillateur : wavetable (frames empilées en perspective, frame courante en
    surbrillance) ou forme d'onde du sample (point de départ, zones du multisample). */
class OscDisplay : public juce::Component
{
public:
    OscDisplay (IDGemsSynthProcessor& p, int oscIndex) : proc (p), osc (oscIndex) {}
    void tick();
    void paint (juce::Graphics&) override;
    void setDropHover (bool h) { dropHover = h; repaint(); }
    void setMessage (const juce::String& m) { message = m; messageTime = juce::Time::getMillisecondCounter(); repaint(); }

private:
    void paintTable (juce::Graphics&, juce::Rectangle<float>);
    void paintSample (juce::Graphics&, juce::Rectangle<float>);

    IDGemsSynthProcessor& proc;
    int osc;
    int lastTable = -1, lastMode = -1, lastVersion = -1;
    float lastPos = -1.0f;
    bool dropHover = false;
    juce::String message;
    juce::uint32 messageTime = 0;
    std::vector<float> overview; // crêtes du sample (mise en cache)
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
