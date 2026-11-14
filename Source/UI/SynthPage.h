#pragma once
#include "Widgets.h"

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
    juce::String title;
    juce::Colour colour;
    int titleWidth = 80;
};

/** Oscillateur A ou B : mode, table, dé, chargement, warps, unison, inharmonicité, sample. */
class OscPanel : public Section, public juce::FileDragAndDropTarget
{
public:
    OscPanel (IDGemsSynthProcessor& p, int osc);
    void resized() override;
    void tick();

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override { display.setDropHover (true); }
    void fileDragExit (const juce::StringArray&) override { display.setDropHover (false); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void loadFiles (const juce::Array<juce::File>& files);
    void updateVisibility();
    void paintOverChildren (juce::Graphics&) override;

    IDGemsSynthProcessor& proc;
    int osc;
    ParamToggle on;
    ParamCombo mode, table, route;
    juce::TextButton dice { juce::String (juce::CharPointer_UTF8 ("D\xc3\x89")) }, load { "CHARGER" };
    OscDisplay display;
    Knob pos, level, pan, oct, semi, fine, phase, phRand;
    Knob unison, detune, blend, spread, stretch, shift, root;
    ParamToggle loop;
    ParamCombo warp1, warp1Src, warp2, warp2Src;
    Knob warp1Amt, warp2Amt;
    std::unique_ptr<juce::FileChooser> chooser;
    int lastMode = -1;
    juce::Rectangle<int> warpLabel1, warpLabel2, unisonLabel;
};

class SubPanel : public Section
{
public:
    explicit SubPanel (IDGemsSynthProcessor& p);
    void resized() override;

private:
    ParamToggle on;
    ParamCombo route, shape, oct;
    Knob level, pan;
};

class NoisePanel : public Section
{
public:
    explicit NoisePanel (IDGemsSynthProcessor& p);
    void resized() override;

private:
    ParamToggle on;
    ParamCombo route, type;
    Knob level, pan, tone;
};

class FilterPanel : public Section
{
public:
    FilterPanel (IDGemsSynthProcessor& p, int index);
    void resized() override;

private:
    int index;
    ParamToggle on;
    ParamCombo type;
    std::unique_ptr<ParamCombo> serial; // uniquement sur le filtre 1
    Knob cutoff, reso, drive, key, mix;
};

class EnvPanel : public Section
{
public:
    explicit EnvPanel (IDGemsSynthProcessor& p);
    void resized() override;
    void tick() { view.tick(); }

private:
    EnvelopeView view;
    Knob delay, attack, hold, decay, sustain, release, aCurve, dCurve, rCurve;
};

class VoicePanel : public Section
{
public:
    explicit VoicePanel (IDGemsSynthProcessor& p);
    void resized() override;

private:
    ParamCombo mode;
    ParamToggle legato;
    Knob voices, glide, glideCurve, velSens, bendRange;
};

/** Onglet SYNTH. */
class SynthPage : public juce::Component
{
public:
    explicit SynthPage (IDGemsSynthProcessor& p);
    void resized() override;
    void tick();

private:
    OscPanel oscA, oscB;
    SubPanel sub;
    NoisePanel noise;
    FilterPanel f1, f2;
    EnvPanel env;
    VoicePanel voice;
};
} // namespace idgs::ui
