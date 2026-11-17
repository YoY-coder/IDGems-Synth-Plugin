#pragma once
#include "ModUI.h"

namespace idgs::ui
{
/** Choix du mode de jeu (Direct / Arpège / Séquenceur / Générateur) en gros boutons. */
class PlayModeButtons : public juce::Component, private juce::Timer
{
public:
    explicit PlayModeButtons (IDGemsSynthProcessor& p) : proc (p) { startTimerHz (10); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override { const int m = (int) proc.getValue ("play_mode"); if (m != last) { last = m; repaint(); } }
    IDGemsSynthProcessor& proc;
    int last = -1;
};

/** Éditeur des pas du séquenceur : notes (glisser vertical), vélocité, gate, probabilité, ratchets, slide, cadenas. */
class NoteSeqEditor : public juce::Component, private juce::Timer
{
public:
    explicit NoteSeqEditor (IDGemsSynthProcessor& p) : proc (p) { startTimerHz (20); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    enum Lane { LNote, LVel, LGate, LProb, LRatchet, LSlide, LLock, NumLanes };
    void timerCallback() override { repaint(); }
    juce::Rectangle<float> laneRect (int lane) const;
    int stepAt (float x) const;
    void edit (const juce::MouseEvent& e, bool first);
    IDGemsSynthProcessor& proc;
    int dragLane = -1, dragStep = -1, startNote = 0;
    float startY = 0.0f;
};

/** Zone à glisser vers Ableton : exporte le motif en fichier MIDI. */
class MidiDragZone : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit MidiDragZone (IDGemsSynthProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    IDGemsSynthProcessor& proc;
    juce::ComboBox bars;
    bool dragging = false;
};

/** Gamme : clavier d'une octave qui montre les notes de la gamme. */
class ScaleView : public juce::Component, private juce::Timer
{
public:
    explicit ScaleView (IDGemsSynthProcessor& p) : proc (p) { startTimerHz (5); }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override { repaint(); }
    IDGemsSynthProcessor& proc;
};

/** Onglet ARPÉ / SÉQ. */
class PlayPage : public juce::Component
{
public:
    explicit PlayPage (IDGemsSynthProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    IDGemsSynthProcessor& proc;
    PlayModeButtons modes;
    MidiDragZone midiDrag;
    NoteSeqEditor seqEditor;
    juce::OwnedArray<juce::Component> seqCtl, arpCtl, genCtl, scaleCtl, sprayCtl, microCtl;
    juce::TextButton seqRandom { "RANDOM" }, seqClear { "EFFACER" }, generate { IDG_T ("GÉNÉRER") };
    ScaleView scaleView;
    std::unique_ptr<DiceButton> dice;
    juce::Rectangle<int> seqArea, arpArea, genArea, scaleArea, sprayArea, microArea, topArea;
};
} // namespace idgs::ui
