#pragma once
#include "Widgets.h"

namespace idgs::ui
{
/** Petit dé : clic = random de sa zone (module, section, rack...), clic droit = cadenas / random léger.
    Se place tout seul dans le coin haut-droit de son hôte. */
class DiceButton : public juce::Component, public juce::SettableTooltipClient, private juce::ComponentListener, private juce::Timer
{
public:
    using Target = RandomEngine::Target;
    DiceButton (IDGemsSynthProcessor& p, std::function<Target()> target, juce::Colour c = Theme::accent);
    ~DiceButton() override;
    /** Ajoute le dé à `host`, à `rightInset` px du bord droit et `top` px du haut. */
    void attachTo (juce::Component& host, int rightInset = 8, int top = 7);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    static void drawDice (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, int face, float alpha = 1.0f);

private:
    void componentMovedOrResized (juce::Component&, bool, bool) override { place(); }
    void timerCallback() override;
    void place();
    bool locked() const;
    void setLocked (bool l);
    juce::String lockKey() const;
    IDGemsSynthProcessor& proc;
    std::function<Target()> target;
    juce::Colour colour;
    juce::Component* host = nullptr;
    int inset = 8, topY = 7, face = 5, lastVersion = -1;
    float flash = 0.0f;
};

/** Pad XY du morph : 8 snapshots placés librement, le curseur mélange les plus proches. */
class MorphPad : public juce::Component, private juce::Timer
{
public:
    explicit MorphPad (IDGemsSynthProcessor& p) : proc (p) { startTimerHz (30); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    static juce::Colour snapColour (int k);

private:
    void timerCallback() override;
    juce::Rectangle<float> padArea() const { return getLocalBounds().toFloat().reduced (6.0f); }
    juce::Point<float> toPx (float x, float y) const;
    juce::Point<float> fromPx (juce::Point<float>) const;
    IDGemsSynthProcessor& proc;
    int dragSnap = -1;
    bool dragCursor = false;
    float lx = -1.0f, ly = -1.0f;
    int lastVersion = -1;
};

/** Historique des tirages : clic = rappel, étoile = favori, molette = défilement. */
class HistoryList : public juce::Component, private juce::Timer
{
public:
    explicit HistoryList (IDGemsSynthProcessor& p) : proc (p) { startTimerHz (10); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;
    static constexpr int rowH = 20;
    IDGemsSynthProcessor& proc;
    int scroll = 0, lastVersion = -1;
};

/** Panneau complet du random (superposé à l'interface). */
class RandomPanel : public juce::Component, private juce::Timer
{
public:
    explicit RandomPanel (IDGemsSynthProcessor& p);
    void open();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    std::function<void (const juce::String&)> flash;

private:
    void timerCallback() override;
    void refreshPersonalities();
    juce::Rectangle<int> box() const;
    IDGemsSynthProcessor& proc;
    MorphPad pad;
    HistoryList history;
    juce::OwnedArray<juce::TextButton> snapButtons;
    juce::OwnedArray<DiceButton> sectionDice;
    juce::OwnedArray<juce::TextButton> sectionLocks;
    juce::TextButton allDice { IDG_T ("RANDOM GÉNÉRAL") }, replay { "REJOUER" }, close { "X" }, reloadPers { IDG_T ("↻") };
    juce::TextEditor seed;
    juce::ComboBox personality;
    std::unique_ptr<Knob> amount, intensity, glide, drift, driftSpeed, mutateAmount;
    std::unique_ptr<ParamCombo> sync, mutateEvery;
    std::unique_ptr<ParamToggle> mutate, guard;
    juce::Rectangle<int> padArea, secArea, setArea, histArea;
    int lastVersion = -1;
    juce::String lastMessage;
};

/** Barre du haut : gros dé (random général), dosage, bouton du panneau. */
class RandomCluster : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit RandomCluster (IDGemsSynthProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    std::function<void()> openPanel;

private:
    void timerCallback() override;
    juce::Rectangle<int> diceArea() const { return getLocalBounds().removeFromLeft (48).reduced (2, 6); }
    IDGemsSynthProcessor& proc;
    Knob amount;
    juce::TextButton more { "..." };
    int face = 5, lastVersion = -1;
    float flash = 0.0f;
};
} // namespace idgs::ui
