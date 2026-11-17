#pragma once
#include "ModUI.h"

namespace idgs::ui
{
/** Emplacement de son à glisser-déposer (Texture son 1 / son 2, empreinte, fichier data bending). */
class SoundSlot : public juce::Component, public juce::FileDragAndDropTarget, public juce::SettableTooltipClient, private juce::Timer
{
public:
    SoundSlot (IDGemsSynthProcessor& p, int slot, const juce::String& title, const juce::String& hint);
    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override { hover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { hover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void timerCallback() override;
    void load (const juce::Array<juce::File>& files);
    IDGemsSynthProcessor& proc;
    int slot;
    juce::String title, hint, message;
    juce::TextButton button { "CHARGER" };
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<float> overview;
    int lastVersion = -1;
    bool hover = false;
};

/** Un moteur de l'Osc Texture : cadre allumé quand le moteur est utilisé (moteur 1 ou 2). */
class EnginePanel : public juce::Component
{
public:
    EnginePanel (IDGemsSynthProcessor& p, int engine);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

private:
    IDGemsSynthProcessor& proc;
    int engine;
    juce::OwnedArray<juce::Component> controls;
    int lastState = 0;
};

/** Affichage de l'empreinte : bandes au fil du temps (sonagramme), tête de lecture. */
class FingerprintView : public juce::Component, private juce::Timer
{
public:
    explicit FingerprintView (IDGemsSynthProcessor& p) : proc (p) { startTimerHz (10); }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    IDGemsSynthProcessor& proc;
    juce::Image image;
    int lastVersion = -1;
};

/** Onglet TEXTURE : Osc Texture (6 moteurs + morph) et empreinte spectrale. */
class TexturePage : public juce::Component, private juce::Timer
{
public:
    explicit TexturePage (IDGemsSynthProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { for (auto* e : engines) e->tick(); repaint (morphArea); }
    IDGemsSynthProcessor& proc;
    ParamToggle on;
    ParamCombo engine1, engine2;
    Knob morph, level, pan, oct, semi, root;
    SoundSlot sound1, sound2, data, fpSlot;
    juce::OwnedArray<EnginePanel> engines;
    std::unique_ptr<DiceButton> dice;
    ParamToggle fpOn, fpLoop;
    ParamCombo fpRes;
    Knob fpAmount, fpShift, fpSpeed;
    FingerprintView fpView;
    juce::Rectangle<int> headArea, slotArea, engineArea, fpArea, morphArea;
};
} // namespace idgs::ui
