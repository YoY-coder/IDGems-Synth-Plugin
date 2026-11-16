#pragma once
#include "Graphs.h"

namespace idgs::ui
{
/** Pastille d'une source de modulation : à attraper et glisser sur un potard ou un graphique.
    Survol = ses cibles s'allument ; affiche le nombre de cibles et la valeur courante. */
class ModSourceHandle : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    ModSourceHandle (IDGemsSynthProcessor& p, int source);
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void setSource (int s) { src = s; repaint(); }

private:
    void timerCallback() override;
    IDGemsSynthProcessor& proc;
    int src;
    float lastVal = -1.0f;
    int lastCount = -1, lastHoverParam = -2;
    bool dragging = false;
};

/** Éditeur de forme de LFO (repris d'IDGems) : points + courbes, ou crayon sur une grille. */
class LfoEditor : public juce::Component, private juce::Timer
{
public:
    explicit LfoEditor (IDGemsSynthProcessor&);
    void setLfo (int i) { lfo = i; repaint(); }
    void setPencil (bool p) { pencil = p; repaint(); }
    void setGrid (int g) { grid = g; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void timerCallback() override { repaint(); }
    juce::Point<float> toScreen (float x, float y) const;
    juce::Point<float> fromScreen (juce::Point<float>) const;
    int hitPoint (juce::Point<float>) const;
    void publish (const idg::LfoShape& s);
    void pencilAt (juce::Point<float>);

    IDGemsSynthProcessor& proc;
    int lfo = 0;
    bool pencil = false;
    int grid = 16;
    int dragPt = -1, curveSeg = -1;
    float curveStart = 0.0f;
    idg::LfoShape work;
};

/** Panneau LFO : onglets LFO 1-10 (avec pastilles), éditeur, réglages. */
class LfoPanel : public juce::Component
{
public:
    explicit LfoPanel (IDGemsSynthProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void tick();

private:
    void select (int i);
    juce::Rectangle<int> tabRect (int i) const;
    IDGemsSynthProcessor& proc;
    int selected = 0, lastSync = -1;
    LfoEditor editor;
    juce::OwnedArray<ModSourceHandle> handles;
    juce::TextButton pencil { "CRAYON" }, shapes { "FORMES" };
    juce::ComboBox gridBox;
    struct Ctl
    {
        Ctl (IDGemsSynthProcessor& p, int i);
        ParamCombo mode, div;
        ParamToggle sync;
        Knob rate, phase, fade, smooth, stereo;
    };
    juce::OwnedArray<Ctl> ctls;
};

/** Panneau random + sources de jeu (vélocité, note, molette, aftertouch, bend, random par note). */
class SourcesPanel : public juce::Component
{
public:
    explicit SourcesPanel (IDGemsSynthProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    IDGemsSynthProcessor& proc;
    int selected = 0;
    juce::OwnedArray<ModSourceHandle> rndHandles, playHandles;
    struct Ctl
    {
        Ctl (IDGemsSynthProcessor& p, int i);
        ParamCombo style, div;
        ParamToggle sync, retrig;
        Knob rate;
    };
    juce::OwnedArray<Ctl> ctls;
    juce::Rectangle<int> rndArea, playArea, ctlArea;
};

/** Onglet MODULATION : liste de toutes les connexions (profondeur, bipolaire, suppression). */
class ModListPage : public juce::Component, private juce::Timer
{
public:
    explicit ModListPage (IDGemsSynthProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void rebuild();
    IDGemsSynthProcessor& proc;
    struct Row : public juce::Component
    {
        Row (IDGemsSynthProcessor& p, int slot);
        void paint (juce::Graphics&) override;
        void resized() override;
        IDGemsSynthProcessor& proc;
        int slot;
        juce::Slider depth;
        juce::ToggleButton bipolar { "Bipolaire" };
        juce::TextButton remove { "X" };
    };
    juce::Viewport view;
    juce::Component content;
    juce::OwnedArray<Row> rows;
    int lastVersion = -1;
};
} // namespace idgs::ui
