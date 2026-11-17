#pragma once
#include "ModUI.h"

namespace idgs::ui
{
/** Routage graphique : envois sources -> bus, envois bus -> bus (boucles signalées), tranches de bus. */
class RoutingPanel : public juce::Component, private juce::Timer
{
public:
    explicit RoutingPanel (IDGemsSynthProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { repaint(); }
    IDGemsSynthProcessor& proc;
    juce::OwnedArray<Knob> sends;           // [source * 3 + bus]
    juce::OwnedArray<Knob> busSends;        // A->B, A->C, B->A, B->C, C->A, C->B
    juce::OwnedArray<Knob> busVol, busPan;
    juce::OwnedArray<ParamToggle> mute, solo;
    juce::OwnedArray<ModSourceHandle> followers;
    juce::Rectangle<int> srcLabels[NumSendSources], busCols[NumBuses], stripArea, busSendArea;
    float meter[NumBuses] {};
};

/** Grille de pas du séquenceur d'un effet : clic = on/off, glisser vertical = probabilité, clic droit = ratchets. */
class StepGrid : public juce::Component, private juce::Timer
{
public:
    explicit StepGrid (IDGemsSynthProcessor& p) : proc (p) { startTimerHz (20); }
    void setTarget (idg::SeqData* d, juce::Colour c) { data = d; colour = c; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    void timerCallback() override { repaint(); }
    int stepAt (float x) const;
    IDGemsSynthProcessor& proc;
    idg::SeqData* data = nullptr;
    juce::Colour colour;
    int dragStep = -1;
    float dragStartProb = 1.0f;
};

/** Rack d'un bus : liste réordonnable des 27 effets, dry/wet par famille, effet sélectionné + séquenceur. */
class RackPanel : public juce::Component, private juce::Timer
{
public:
    explicit RackPanel (IDGemsSynthProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void selectRack (int r);
    void selectEffect (int fx);
    void rebuildEffectPanel();
    juce::Rectangle<int> tabRect (int i) const;
    juce::Rectangle<int> rowRect (int position) const;
    int positionAt (juce::Point<int> p) const;

    IDGemsSynthProcessor& proc;
    int rack = 0, fx = idg::FxFilter;
    int dragFrom = -1;
    bool dragMoved = false;
    juce::Rectangle<int> listArea, panelArea, seqArea, famArea;
    juce::OwnedArray<Knob> famMix;
    // panneau de l'effet sélectionné
    juce::OwnedArray<juce::Component> fxControls;
    std::unique_ptr<ParamToggle> fxOn, fxPar;
    std::unique_ptr<Knob> fxMix;
    std::unique_ptr<ParamCombo> fxMode, fxTrans;
    // séquenceur
    StepGrid grid;
    juce::ToggleButton seqOn { IDG_T ("Séquenceur") };
    juce::ComboBox seqRate, seqLen;
    juce::Slider seqSwing;
    juce::TextButton seqRandom { "RANDOM" };
};

/** Chaîne de sortie : OTT, soft clip, limiteur, grave mono, passe-haut, gains, sans latence, suréchantillonnage. */
class OutputPanel : public juce::Component
{
public:
    explicit OutputPanel (IDGemsSynthProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::OwnedArray<Knob> knobs;
    std::unique_ptr<ParamToggle> monoOn, zeroLat;
    std::unique_ptr<ParamCombo> destOS;
    juce::Rectangle<int> groups[5];
};

/** Onglet FX. */
class FxPage : public juce::Component
{
public:
    explicit FxPage (IDGemsSynthProcessor&);
    void resized() override;

private:
    RoutingPanel routing;
    RackPanel rackPanel;
    OutputPanel output;
};
} // namespace idgs::ui
