#pragma once
#include "ModUI.h"

namespace idgs::ui
{
/** Panneau avec cadre et titre façon IDGems. Les commandes du titre se placent dans header(). */
class Section : public juce::Component
{
public:
    Section (const juce::String& t, juce::Colour c) : title (t), colour (c) {}
    void paint (juce::Graphics& g) override
    {
        Theme::panelFrame (g, getLocalBounds().toFloat().reduced (1.0f), colour);
        Theme::sectionTitle (g, title, getLocalBounds().toFloat().reduced (10.0f, 7.0f), colour);
    }
    juce::Rectangle<int> header() const { return getLocalBounds().reduced (10, 6).withHeight (22).withTrimmedLeft (titleWidth); }
    juce::Rectangle<int> body() const { return getLocalBounds().reduced (10, 6).withTrimmedTop (28); }

protected:
    /** Petit titre de bloc à l'intérieur d'un panneau. */
    static void blockTitle (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& t)
    {
        g.setFont (Theme::mono (9.0f, true));
        g.setColour (Theme::textDim);
        g.drawText (t, r.withHeight (12), juce::Justification::centred);
        g.setColour (Theme::line.withAlpha (0.6f));
        g.drawVerticalLine (r.getX() - 3, (float) r.getY(), (float) r.getBottom());
    }
    juce::String title;
    juce::Colour colour;
    int titleWidth = 80;
};

/** Ligne d'oscillateur façon Vital : hauteur / niveau | table | unison | warp 1 | warp 2 | phase & inharmonicité. */
class OscRow : public Section, public juce::FileDragAndDropTarget
{
public:
    OscRow (IDGemsSynthProcessor& p, int osc);
    void resized() override;
    void paintOverChildren (juce::Graphics&) override;
    void tick();

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override { display.setDropHover (true); }
    void fileDragExit (const juce::StringArray&) override { display.setDropHover (false); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    juce::TextButton pencil { "CRAYON" };
    void loadFiles (const juce::Array<juce::File>& files);
    void updateVisibility();

    IDGemsSynthProcessor& proc;
    int osc;
    ParamToggle on;
    ParamCombo mode, table;
    juce::TextButton dice, load { "CHARGER" };
    OscDisplay display;
    Knob oct, semi, fine, level, pan;
    Knob unison, detune, blend, spread;
    ParamCombo warp1, warp1Src, warp2, warp2Src;
    Knob warp1Amt, warp2Amt;
    Knob phase, phRand, stretch, shift, root;
    ParamToggle loop;
    std::unique_ptr<juce::FileChooser> chooser;
    int lastMode = -1;
    juce::Rectangle<int> blocks[4];
};

/** Rangée Sub + Noise (compacte). */
class SubNoiseRow : public Section
{
public:
    explicit SubNoiseRow (IDGemsSynthProcessor& p);
    void resized() override;
    void paintOverChildren (juce::Graphics&) override;
    void tick() { subView.tick(); noiseTypes.tick(); }

private:
    ParamToggle subOn, noiseOn;
    ParamCombo subShape, subOct;
    SubView subView;
    Knob subLevel, subPan;
    NoiseTypeSelector noiseTypes;
    Knob noiseLevel, noisePan, noiseTone;
    int split = 0;
};

/** Boutons de routage d'un filtre (façon Vital) : quelles sources entrent dans ce filtre, + chaînage F1 -> F2. */
class RouteButtons : public juce::Component, public juce::TooltipClient
{
public:
    RouteButtons (IDGemsSynthProcessor& p, int filterIndex) : proc (p), filter (filterIndex) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent& e) override { hover = cellAt (e.position.x); repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    juce::String getTooltip() override;
    void tick();

private:
    static constexpr int NumCells = NumSendSources + 1; // Osc A, Osc B, Sub, Noise, Texture, chaînage
    int cellAt (float x) const { return juce::jlimit (0, NumCells - 1, (int) (x / ((float) getWidth() / NumCells))); }
    bool sourceIn (int src) const;
    void toggleSource (int src);
    IDGemsSynthProcessor& proc;
    int filter, hover = -1;
    float lastHash = -1.0f;
};

class FilterPanel : public Section
{
public:
    FilterPanel (IDGemsSynthProcessor& p, int index);
    void resized() override;
    void tick() { view.tick(); routes.tick(); }

private:
    int index;
    FilterView view;
    RouteButtons routes;
    ParamToggle on;
    ParamCombo type;
    Knob cutoff, reso, drive, mix, key;
};

/** Enveloppes avec un onglet chacune (ENV 1 = volume ; ENV 5 à 8 se débloquent en phase 3). */
class EnvSection : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit EnvSection (IDGemsSynthProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void tick();

private:
    struct Tab : public juce::Component
    {
        Tab (IDGemsSynthProcessor& p, int e);
        void resized() override;
        void paintOverChildren (juce::Graphics&) override;
        int env;
        EnvelopeEditor editor;
        Knob delay, attack, hold, decay, sustain, release;
    };
    juce::Rectangle<int> tabArea() const { return getLocalBounds().reduced (1).removeFromLeft (72); }
    juce::Rectangle<int> tabRect (int i) const;
    void updateVisibleCount();
    IDGemsSynthProcessor& proc;
    juce::OwnedArray<Tab> tabs;
    juce::OwnedArray<ModSourceHandle> handles;
    int selected = 0, visibleCount = VisibleEnvelopes;
};

/** Zones réservées aux modules de la phase 3 (dessinées dans la DA, sans commandes). */
class FuturePanel : public Section
{
public:
    FuturePanel (const juce::String& title, juce::StringArray tabNames, juce::StringArray cells, juce::String note);
    void paint (juce::Graphics&) override;

private:
    juce::StringArray tabNames, cells;
    juce::String note;
};

class VoicePanel : public Section
{
public:
    explicit VoicePanel (IDGemsSynthProcessor& p);
    void resized() override;

private:
    ParamCombo mode;
    ParamToggle legato;
    Knob voices, bendRange, velSens, glide, glideCurve;
};

/** Onglet SYNTH (disposition inspirée de Vital). */
class SynthPage : public juce::Component
{
public:
    explicit SynthPage (IDGemsSynthProcessor& p);
    void resized() override;
    void tick();

private:
    OscRow oscA, oscB;
    SubNoiseRow subNoise;
    FilterPanel f1, f2;
    EnvSection envs;
    LfoPanel lfo;
    SourcesPanel sources;
    VoicePanel voice;
    juce::OwnedArray<DiceButton> dice;
};

/** Colonne des 8 macros (toujours visible, à gauche). */
class MacroColumn : public juce::Component
{
public:
    explicit MacroColumn (IDGemsSynthProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<ModSourceHandle> handles;
};
} // namespace idgs::ui
