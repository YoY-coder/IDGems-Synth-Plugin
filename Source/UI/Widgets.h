#pragma once
#include "../PluginProcessor.h"
#include "Theme.h"

namespace idgs::ui
{
using idg::ui::Theme;

/** Potard lié à un paramètre (style néon d'IDGems). Double-clic = valeur par défaut, valeur affichée au survol.
    Cible de modulation : on y dépose une source (glisser-déposer), anneaux colorés = modulations,
    Alt / Ctrl + glisser = profondeur de la dernière modulation, clic droit = liste des modulations. */
class Knob : public juce::Slider, public juce::DragAndDropTarget, private juce::Timer
{
public:
    Knob (IDGemsSynthProcessor& p, const juce::String& paramId, const juce::String& label, juce::Colour colour = Theme::accent);
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::String getTooltip() override;
    void setLabel (const juce::String& l) { label = l; repaint(); }

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dropHover = false; repaint(); }
    void itemDropped (const SourceDetails&) override;

    /** Appelé par le timer de l'éditeur : redessine si la modulation / le survol a changé. */
    void tick();
    int getParamIndex() const { return paramIdx; }

private:
    void timerCallback() override { tick(); }
    void showModMenu();
    IDGemsSynthProcessor& proc;
    juce::RangedAudioParameter* param = nullptr;
    int paramIdx = -1, field = -1;
    juce::String label;
    juce::Colour colour;
    bool hovering = false, dropHover = false;
    int depthSlot = -1;
    float depthStart = 0.0f;
    float lastMod = -2.0f;
    int lastVersion = -1, lastHoverSrc = -2, lastDragSrc = -2, lastRandVersion = -1;
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
class OscDisplay : public juce::Component, public juce::DragAndDropTarget
{
public:
    OscDisplay (IDGemsSynthProcessor& p, int oscIndex) : proc (p), osc (oscIndex) {}
    void tick();
    void paint (juce::Graphics&) override;
    void setDropHover (bool h) { dropHover = h; repaint(); }
    void setMessage (const juce::String& m) { message = m; messageTime = juce::Time::getMillisecondCounter(); repaint(); }

    // glisser sur l'affichage : position dans la wavetable (vertical, comme Vital) / départ du sample (horizontal)
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    /** Crayon : dessin direct de la frame courante. */
    void setPencil (bool p) { pencil = p; editBuf.clear(); repaint(); }

    // cible de modulation (position dans la table)
    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.toString().startsWith ("mod:"); }
    void itemDragEnter (const SourceDetails&) override { modHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { modHover = false; repaint(); }
    void itemDropped (const SourceDetails&) override;

private:
    void setPos (float v);
    void paintPencil (juce::Graphics&, juce::Rectangle<float>);
    void drawTo (juce::Point<float> p);
    bool pencil = false, modHover = false;
    std::vector<float> editBuf;
    int editFrame = 0;
    juce::Point<float> lastDraw;
    float dragStartPos = 0.0f;
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
