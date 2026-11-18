#pragma once
#include "ModUI.h"

namespace idgs::ui
{
/** Zone à glisser vers la DAW : le fichier est fourni au moment du glisser (rendu ou dernier enregistrement). */
class FileDragZone : public juce::Component, public juce::SettableTooltipClient
{
public:
    FileDragZone (juce::String text, juce::Colour c, std::function<juce::File()> make) : label (std::move (text)), colour (c), makeFile (std::move (make))
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void setText (const juce::String& t) { label = t; repaint(); }

private:
    juce::String label;
    juce::Colour colour;
    std::function<juce::File()> makeFile;
    bool dragging = false;
};

/** Aperçu du rolling recorder (les dernières secondes de la sortie, en direct). */
class RollingView : public juce::Component, private juce::Timer
{
public:
    explicit RollingView (IDGemsSynthProcessor& p) : proc (p) { startTimerHz (15); }
    void paint (juce::Graphics&) override;
    float seconds = 30.0f;

private:
    void timerCallback() override { repaint(); }
    IDGemsSynthProcessor& proc;
    std::vector<float> peaks;
};

/** Courbes du motion recording (macros enregistrées) + tête de lecture. */
class MotionView : public juce::Component, private juce::Timer
{
public:
    explicit MotionView (IDGemsSynthProcessor& p) : proc (p) { startTimerHz (30); }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override { repaint(); }
    IDGemsSynthProcessor& proc;
};

/** Onglet EXPORT : enregistreur, rolling recorder, boucle Feed, rendu / batch, motion recording. */
class ExportPage : public juce::Component, private juce::Timer
{
public:
    explicit ExportPage (IDGemsSynthProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void (const juce::String&)> flash;

private:
    void timerCallback() override;
    juce::File lastAudio();
    void feed (int slot);
    idgs::RenderSettings settings() const;
    void updateModeControls();

    IDGemsSynthProcessor& proc;
    // enregistreur
    juce::TextButton rec { "REC" }, keepRolling { IDG_T ("GARDER LES DERNIÈRES") }, openRec { "DOSSIER" };
    juce::ComboBox rollingSecs;
    RollingView rolling;
    FileDragZone recDrag;
    juce::OwnedArray<juce::TextButton> feedButtons;
    // rendu
    juce::OwnedArray<juce::TextButton> modeButtons;
    juce::TextButton hq { "HQ" }, chord { "ACCORD" }, normalize { "NORMALISER" }, randomize { "VARIATIONS RANDOM" };
    juce::ComboBox note, length, bars, texLen, count;
    juce::Slider amount;
    juce::TextEditor name;
    FileDragZone renderDrag;
    juce::TextButton batch { "LANCER LE BATCH" }, cancel { "STOP" }, openRenders { "DOSSIER" };
    // motion
    juce::TextButton motionRec { "REC MOTION" }, motionPlay { "LECTURE" }, motionClear { "EFFACER" };
    juce::ComboBox motionBars;
    MotionView motionView;

    int mode = 0;
    juce::Rectangle<int> recArea, renderArea, motionArea, progressArea;
};
} // namespace idgs::ui
