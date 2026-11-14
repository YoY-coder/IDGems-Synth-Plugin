#include "PluginEditor.h"

using namespace idgs::ui;
using idg::ui::Theme;

namespace
{
/** Page provisoire pour les onglets qui arrivent dans les phases suivantes. */
class ComingSoonPage : public juce::Component
{
public:
    ComingSoonPage (juce::String t, juce::StringArray l) : title (std::move (t)), lines (std::move (l)) {}
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().reduced (12, 10).toFloat();
        Theme::panelFrame (g, r, Theme::textDim, 0.5f);
        Theme::cutText (g, title, r.withTrimmedTop (r.getHeight() * 0.32f).withHeight (40.0f), 30.0f, Theme::accent,
                        juce::Justification::centred, 0.5f, true);
        g.setFont (Theme::mono (13.0f));
        g.setColour (Theme::textDim);
        auto t = r.withTrimmedTop (r.getHeight() * 0.32f + 60.0f);
        for (auto& l : lines) g.drawText (l, t.removeFromTop (22.0f), juce::Justification::centredTop);
    }

private:
    juce::String title;
    juce::StringArray lines;
};
} // namespace

struct IDGemsSynthEditor::Content : public juce::Component
{
    explicit Content (IDGemsSynthProcessor& p)
        : proc (p),
          synth (p),
          fx ("FX // BUS A B C", { IDG_T ("Phase 4 : bus A / B / C + Master, envois, bus → bus,"), IDG_T ("rack d'effets IDGems complet dans chaque bus, chaîne de sortie complète.") }),
          mod ("MODULATION", { IDG_T ("Phase 3 : enveloppes (déblocage jusqu'à 8), 10 LFO au crayon,"), IDG_T ("5 random / S&H, 8 macros, assignation par glisser-déposer.") }),
          arp (IDG_T ("ARPÉ // SÉQUENCEUR"), { IDG_T ("Phase 6 : arpégiateur, séquenceur 32 pas, gammes, mutation,"), IDG_T ("export MIDI en glisser-déposer, Note Spray.") }),
          exp ("EXPORT", { IDG_T ("Phase 8 : enregistreur, rolling recorder, boucle Feed,"), IDG_T ("export WAV en glisser-déposer, batch render, Draft / HQ.") }),
          masterVol (p, idgs::P::masterVol, "Volume"),
          ceiling (p, idgs::P::ceiling, "Plafond", Theme::blue),
          meter (p),
          scope (p),
          keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
    {
        const juce::StringArray names { "SYNTH", "FX", "MODULATION", IDG_T ("ARPÉ / SÉQ"), "EXPORT" };
        for (int i = 0; i < names.size(); ++i)
        {
            auto* b = tabs.add (new juce::TextButton (names[i]));
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonOnColourId, Theme::accent);
            addAndMakeVisible (b);
        }
        pages = { &synth, &fx, &mod, &arp, &exp };
        for (auto* pg : pages) addChildComponent (pg);

        panicBtn.setButtonText ("PANIC");
        panicBtn.setColour (juce::TextButton::buttonColourId, Theme::red.withAlpha (0.25f));
        panicBtn.setTooltip (IDG_T ("Coupe toutes les voix et vide les buffers (sans clic)."));
        panicBtn.onClick = [&p] { p.panic(); };

        keyboard.setAvailableRange (24, 108);
        keyboard.setLowestVisibleKey (36);
        keyboard.setKeyWidth (22.0f);
        keyboard.setWantsKeyboardFocus (false);
        keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xff2b2b33));
        keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff070708));
        keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, Theme::line);
        keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, Theme::blue.withAlpha (0.35f));
        keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, Theme::accent.withAlpha (0.8f));
        keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
        keyboard.setColour (juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, Theme::panel);
        keyboard.setColour (juce::MidiKeyboardComponent::upDownButtonArrowColourId, Theme::accent);

        for (juce::Component* c : std::initializer_list<juce::Component*> { &panicBtn, &masterVol, &ceiling, &meter, &scope, &keyboard })
            addAndMakeVisible (c);
    }

    void paint (juce::Graphics& g) override
    {
        if (background.isNull())
        {
            // texture de fond (grain + lignes de balayage) rendue une seule fois
            background = juce::Image (juce::Image::RGB, getWidth(), getHeight(), true);
            juce::Graphics bg (background);
            bg.fillAll (Theme::bg);
            juce::Random rng (1234);
            for (int i = 0; i < 1400; ++i)
            {
                bg.setColour (juce::Colours::white.withAlpha (rng.nextFloat() * 0.035f));
                bg.fillRect ((float) rng.nextInt (getWidth()), (float) rng.nextInt (getHeight()), 1.2f, 1.2f);
            }
            bg.setColour (juce::Colours::black.withAlpha (0.18f));
            for (int y = 0; y < getHeight(); y += 3) bg.drawHorizontalLine (y, 0.0f, (float) getWidth());
        }
        g.drawImageAt (background, 0, 0);

        // barre du haut
        auto bar = juce::Rectangle<float> (0.0f, 0.0f, (float) getWidth(), (float) topH);
        g.setColour (Theme::panel.withAlpha (0.9f));
        g.fillRect (bar);
        g.setColour (Theme::accent.withAlpha (0.5f));
        g.fillRect (0.0f, (float) topH - 1.0f, (float) getWidth(), 1.0f);
        Theme::cutText (g, "IDGEMS", { 16.0f, 10.0f, 150.0f, 36.0f }, 24.0f, Theme::accent, juce::Justification::centredLeft, 0.6f, true);
        Theme::glitchText (g, "SYNTH", { 168.0f, 14.0f, 80.0f, 28.0f }, juce::Justification::centredLeft, Theme::blue, 1.4f);

        // infos moteur
        g.setFont (Theme::mono (10.5f));
        g.setColour (Theme::textDim);
        g.drawText ("VOIX " + juce::String (proc.activeVoices.load()) + "   CPU " + juce::String (juce::roundToInt (proc.cpuLoad.load() * 100.0f)) + " %",
                    infoArea, juce::Justification::centredRight);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (topH);
        top.removeFromLeft (260);
        for (auto* t : tabs) t->setBounds (top.removeFromLeft (t->getButtonText().length() > 6 ? 118 : 84).reduced (3, 14));
        top.removeFromRight (10);
        meter.setBounds (top.removeFromRight (34).reduced (0, 8));
        top.removeFromRight (6);
        ceiling.setBounds (top.removeFromRight (54).reduced (0, 2));
        masterVol.setBounds (top.removeFromRight (54).reduced (0, 2));
        top.removeFromRight (8);
        panicBtn.setBounds (top.removeFromRight (70).reduced (0, 15));
        top.removeFromRight (8);
        scope.setBounds (top.removeFromRight (150).reduced (0, 9));
        infoArea = top.removeFromRight (150).reduced (4, 0);

        keyboard.setBounds (r.removeFromBottom (86).reduced (12, 6));
        for (auto* pg : pages) pg->setBounds (r);
    }

    static constexpr int topH = 58;
    IDGemsSynthProcessor& proc;
    juce::OwnedArray<juce::TextButton> tabs;
    SynthPage synth;
    ComingSoonPage fx, mod, arp, exp;
    std::vector<juce::Component*> pages;
    juce::TextButton panicBtn;
    Knob masterVol, ceiling;
    Meter meter;
    Scope scope;
    juce::MidiKeyboardComponent keyboard;
    juce::Rectangle<int> infoArea;
    juce::Image background;
};

IDGemsSynthEditor::IDGemsSynthEditor (IDGemsSynthProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    content = std::make_unique<Content> (p);
    addAndMakeVisible (*content);
    content->setSize (baseW, baseH);
    for (int i = 0; i < content->tabs.size(); ++i) content->tabs[i]->onClick = [this, i] { selectTab (i); };
    selectTab (0);

    tooltip = std::make_unique<juce::TooltipWindow> (this, 600);

    // redimensionnable : coin en bas à droite, proportions conservées, de 50 % à 200 %
    setResizable (true, true);
    if (auto* c = getConstrainer())
    {
        c->setFixedAspectRatio ((double) baseW / baseH);
        c->setSizeLimits (baseW / 2, baseH / 2, baseW * 2, baseH * 2);
    }
    float scale = 0.85f;
    if (auto* f = proc.appProps.getUserSettings()) scale = (float) f->getDoubleValue ("uiScale", 0.85);
    scale = juce::jlimit (0.5f, 2.0f, scale);
    setSize ((int) (baseW * scale), (int) (baseH * scale));
    startTimerHz (30);
}

IDGemsSynthEditor::~IDGemsSynthEditor()
{
    stopTimer();
    tooltip.reset();
    content.reset();
    setLookAndFeel (nullptr);
}

void IDGemsSynthEditor::paint (juce::Graphics& g) { g.fillAll (Theme::bg); }

void IDGemsSynthEditor::resized()
{
    const float s = (float) getWidth() / (float) baseW;
    content->setTransform (juce::AffineTransform::scale (s));
    if (auto* f = proc.appProps.getUserSettings()) f->setValue ("uiScale", s);
}

void IDGemsSynthEditor::selectTab (int index)
{
    for (int i = 0; i < (int) content->pages.size(); ++i)
    {
        content->pages[(size_t) i]->setVisible (i == index);
        content->tabs[i]->setToggleState (i == index, juce::dontSendNotification);
    }
}

void IDGemsSynthEditor::timerCallback()
{
    content->meter.tick();
    content->scope.tick();
    content->synth.tick();
    content->repaint (content->infoArea);
}
