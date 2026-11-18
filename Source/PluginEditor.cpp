#include "PluginEditor.h"

using namespace idgs::ui;
using idg::ui::Theme;


struct IDGemsSynthEditor::Content : public juce::Component, public juce::DragAndDropContainer
{
    explicit Content (IDGemsSynthProcessor& p)
        : proc (p),
          synth (p),
          macros (p),
          tex (p),
          fx (p),
          mod (p),
          arp (p),
          exp (p),
          masterVol (p, idgs::P::masterVol, "Volume"),
          ceiling (p, idgs::P::ceiling, "Plafond", Theme::blue),
          meter (p),
          scope (p),
          keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard),
          browser (p),
          randomCluster (p),
          randomPanel (p)
    {
        randomCluster.openPanel = [this] { randomPanel.open(); };
        randomPanel.flash = [this] (const juce::String& m) { flash (m); };
        exp.flash = [this] (const juce::String& m) { flash (m); };
        addAndMakeVisible (randomCluster);
        // undo / redo / A-B
        undoBtn.setTooltip (IDG_T ("Annuler (Ctrl/Cmd + Z)"));
        redoBtn.setTooltip (IDG_T ("Rétablir (Ctrl/Cmd + Maj + Z, ou Ctrl + Y)"));
        aBtn.setTooltip (IDG_T ("Comparaison A / B : deux versions du son, bascule instantanée (Ctrl/Cmd + B).\nClic droit : copier vers l'autre."));
        bBtn.setTooltip (aBtn.getTooltip());
        undoBtn.onClick = [&p] { p.undoHistory().undo(); };
        redoBtn.onClick = [&p] { p.undoHistory().redo(); };
        aBtn.onClick = [&p] { p.undoHistory().selectSlot (0); };
        bBtn.onClick = [&p] { p.undoHistory().selectSlot (1); };
        for (auto* b : { &aBtn, &bBtn })
        {
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonOnColourId, Theme::blue.withAlpha (0.7f));
            b->addMouseListener (this, false);
        }
        for (auto* b : { &undoBtn, &redoBtn, &aBtn, &bBtn }) addAndMakeVisible (b);
        // ---- barre de presets
        prevBtn.setTooltip (IDG_T ("Preset précédent"));
        nextBtn.setTooltip (IDG_T ("Preset suivant"));
        presetName.setTooltip (IDG_T ("Ouvrir le navigateur de presets (catégories, recherche, favoris)"));
        saveBtn.setTooltip (IDG_T ("Sauvegarder le son actuel comme preset"));
        prevBtn.onClick = [&p] { p.presets->next (-1); };
        nextBtn.onClick = [&p] { p.presets->next (1); };
        presetName.onClick = [this] { browser.open(); };
        saveBtn.onClick = [this] { browser.openSave(); };
        presetName.setColour (juce::TextButton::buttonColourId, Theme::bg);
        for (auto* b : { &prevBtn, &nextBtn, &presetName, &saveBtn }) addAndMakeVisible (b);
        browser.flash = [this] (const juce::String& m) { flash (m); };
        addAndMakeVisible (macros);
        addChildComponent (browser);
        addChildComponent (randomPanel);
        refreshPresetName();

        const juce::StringArray names { "SYNTH", "TEXTURE", "FX", "MODULATION", IDG_T ("ARPÉ / SÉQ"), "EXPORT" };
        for (int i = 0; i < names.size(); ++i)
        {
            auto* b = tabs.add (new juce::TextButton (names[i]));
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonOnColourId, Theme::accent);
            addAndMakeVisible (b);
        }
        pages = { &synth, &tex, &fx, &mod, &arp, &exp };
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
        // logo réactif : il se déchire avec le niveau de sortie et les tirages random
        const float lvl = juce::jlimit (0.0f, 1.0f, juce::jmax (proc.meterOut[0].load(), proc.meterOut[1].load()));
        const float tear = juce::jmax (glitch, lvl > 0.7f ? (lvl - 0.7f) * 2.0f : 0.0f) * Theme::glitchAmount * 2.0f;
        if (tear > 0.02f)
        {
            juce::Random r ((juce::int64) juce::Time::getMillisecondCounter() / 60);
            const float dx = (r.nextFloat() - 0.5f) * 8.0f * tear;
            Theme::cutText (g, "IDGEMS", { 16.0f + dx, 10.0f, 150.0f, 36.0f }, 24.0f, juce::Colour (0xffff1e3c).withAlpha (0.6f * juce::jmin (1.0f, tear)),
                            juce::Justification::centredLeft, 0.6f, true);
            Theme::cutText (g, "SYNTH", { 136.0f - dx, 10.0f, 130.0f, 36.0f }, 24.0f, Theme::accent.withAlpha (0.5f * juce::jmin (1.0f, tear)),
                            juce::Justification::centredLeft, 0.0f, true);
        }
        Theme::cutText (g, "IDGEMS", { 16.0f, 10.0f, 150.0f, 36.0f }, 24.0f, Theme::accent, juce::Justification::centredLeft, 0.6f, true);
        Theme::cutText (g, "SYNTH", { 136.0f, 10.0f, 130.0f, 36.0f }, 24.0f, Theme::blue, juce::Justification::centredLeft, 0.0f, true);

        // message bref (sauvegarde de preset, etc.)
        if (flashText.isNotEmpty())
        {
            g.setFont (Theme::mono (10.0f, true));
            g.setColour (Theme::accent);
            g.drawText (flashText, flashArea, juce::Justification::centred);
        }
    }

    /** Déchirure brève de l'interface (tirage random, changement de preset). */
    void paintOverChildren (juce::Graphics& g) override
    {
        if (glitch <= 0.02f || Theme::glitchAmount <= 0.0f) return;
        juce::Random r ((juce::int64) juce::Time::getMillisecondCounter() / 40);
        const float a = glitch * Theme::glitchAmount * 2.0f;
        for (int k = 0; k < 7; ++k)
        {
            const float y = r.nextFloat() * (float) getHeight(), h = 1.0f + r.nextFloat() * 6.0f * glitch;
            const float x = r.nextFloat() * (float) getWidth() * 0.6f, w = (float) getWidth() * (0.1f + 0.5f * r.nextFloat());
            g.setColour ((k % 3 == 0 ? juce::Colour (0xffff1e3c) : (k % 3 == 1 ? Theme::blue : Theme::accent)).withAlpha (juce::jmin (0.5f, 0.35f * a)));
            g.fillRect (x, y, w, h);
        }
        for (int k = 0; k < 10; ++k)
        {
            g.setColour (juce::Colours::white.withAlpha (juce::jmin (0.25f, 0.2f * a)));
            g.fillRect (r.nextFloat() * (float) getWidth(), r.nextFloat() * (float) getHeight(), 4.0f + 30.0f * r.nextFloat(), 2.0f);
        }
    }

    void tickReactive()
    {
        auto& u = proc.undoHistory();
        undoBtn.setEnabled (u.canUndo());
        redoBtn.setEnabled (u.canRedo());
        aBtn.setToggleState (u.activeSlot() == 0, juce::dontSendNotification);
        bBtn.setToggleState (u.activeSlot() == 1, juce::dontSendNotification);
        const int rv = proc.randomEngine().getCursor() + 1000 * (int) proc.randomEngine().getHistory().size();
        const auto pn = proc.presets->currentName();
        if ((lastRandomVersion >= 0 && rv != lastRandomVersion) || (lastPreset.isNotEmpty() && pn != lastPreset)) glitch = 1.0f;
        lastRandomVersion = rv;
        lastPreset = pn;
        if (glitch > 0.0f)
        {
            glitch = juce::jmax (0.0f, glitch - 0.12f);
            repaint();
        }
        else repaint (0, 0, 280, topH);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && (e.eventComponent == &aBtn || e.eventComponent == &bBtn))
        {
            proc.undoHistory().copyActiveToOther();
            flash (proc.undoHistory().activeSlot() == 0 ? IDG_T ("A copié vers B") : IDG_T ("B copié vers A"));
        }
    }

    bool handleKey (const juce::KeyPress& k, std::function<void (int)> tab)
    {
        const auto mods = k.getModifiers();
        const bool cmd = mods.isCommandDown() || mods.isCtrlDown();
        const int code = k.getKeyCode();
        auto& u = proc.undoHistory();
        if (cmd && (code == 'Z' || code == 'z')) { if (mods.isShiftDown()) u.redo(); else u.undo(); return true; }
        if (cmd && (code == 'Y' || code == 'y')) { u.redo(); return true; }
        if (cmd && (code == 'S' || code == 's')) { browser.openSave(); return true; }
        if (cmd && (code == 'B' || code == 'b')) { u.selectSlot (1 - u.activeSlot()); return true; }
        if (code == juce::KeyPress::escapeKey) { browser.setVisible (false); randomPanel.setVisible (false); return true; }
        for (int i = 0; i < 6; ++i)
            if (code == juce::KeyPress::F1Key + i || (cmd && code == '1' + i)) { tab (i); return true; }
        if (cmd) return false;
        if (code == 'R' || code == 'r')
        {
            auto& re = proc.randomEngine();
            if (mods.isShiftDown()) { if (re.getCursor() > 0) re.recall (re.getCursor() - 1); }
            else re.request ({});
            return true;
        }
        if (code == juce::KeyPress::leftKey) { proc.presets->next (-1); return true; }
        if (code == juce::KeyPress::rightKey) { proc.presets->next (1); return true; }
        return false;
    }

    void flash (const juce::String& m)
    {
        flashText = m;
        flashTime = juce::Time::getMillisecondCounter();
        repaint (flashArea);
    }

    void tickFlash()
    {
        if (flashText.isNotEmpty() && juce::Time::getMillisecondCounter() - flashTime > 2500) { flashText.clear(); repaint (flashArea); }
    }

    void dragOperationEnded (const juce::DragAndDropTarget::SourceDetails&) override { proc.uiDragSource = -1; }

    void refreshPresetName() { presetName.setButtonText (proc.presets->currentName()); }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (topH);
        top.removeFromLeft (264);
        const int tabW[] { 66, 76, 46, 100, 92, 66 };
        for (int i = 0; i < tabs.size(); ++i) tabs[i]->setBounds (top.removeFromLeft (tabW[i]).reduced (2, 14));
        top.removeFromRight (10);
        meter.setBounds (top.removeFromRight (34).reduced (0, 8));
        top.removeFromRight (6);
        ceiling.setBounds (top.removeFromRight (54).reduced (0, 2));
        masterVol.setBounds (top.removeFromRight (54).reduced (0, 2));
        top.removeFromRight (8);
        panicBtn.setBounds (top.removeFromRight (56).reduced (0, 15));
        top.removeFromRight (6);
        scope.setBounds (top.removeFromRight (96).reduced (0, 7));
        top.removeFromRight (8);
        // random (dé, dosage, panneau), undo / A-B, puis presets centrés dans l'espace restant
        randomCluster.setBounds (top.removeFromRight (124));
        top.removeFromRight (6);
        {
            auto u = top.removeFromRight (100).withSizeKeepingCentre (100, 24);
            bBtn.setBounds (u.removeFromRight (22));
            u.removeFromRight (2);
            aBtn.setBounds (u.removeFromRight (22));
            u.removeFromRight (8);
            redoBtn.setBounds (u.removeFromRight (22));
            u.removeFromRight (2);
            undoBtn.setBounds (u.removeFromRight (22));
        }
        top.removeFromRight (4);
        auto pr = top.withSizeKeepingCentre (juce::jmin (top.getWidth(), 280), 28);
        prevBtn.setBounds (pr.removeFromLeft (26));
        pr.removeFromLeft (3);
        saveBtn.setBounds (pr.removeFromRight (64));
        pr.removeFromRight (3);
        nextBtn.setBounds (pr.removeFromRight (26));
        pr.removeFromRight (3);
        presetName.setBounds (pr);
        flashArea = presetName.getBounds().translated (0, 20).withHeight (12).expanded (60, 0);

        keyboard.setBounds (r.removeFromBottom (72).reduced (8, 4));
        macros.setBounds (r.removeFromLeft (92).reduced (6, 6).withTrimmedRight (-4));
        for (auto* pg : pages) pg->setBounds (r);
        browser.setBounds (getLocalBounds());
        randomPanel.setBounds (getLocalBounds());
    }

    static constexpr int topH = 58;
    IDGemsSynthProcessor& proc;
    juce::OwnedArray<juce::TextButton> tabs;
    SynthPage synth;
    MacroColumn macros;
    TexturePage tex;
    FxPage fx;
    PlayPage arp;
    ExportPage exp;
    ModListPage mod;
    std::vector<juce::Component*> pages;
    juce::TextButton panicBtn;
    Knob masterVol, ceiling;
    Meter meter;
    Scope scope;
    juce::MidiKeyboardComponent keyboard;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, presetName { "Init" }, saveBtn { "SAUVER" };
    PresetBrowser browser;
    juce::TextButton undoBtn { juce::String (juce::CharPointer_UTF8 ("\xe2\x86\xb6")) }, redoBtn { juce::String (juce::CharPointer_UTF8 ("\xe2\x86\xb7")) },
        aBtn { "A" }, bBtn { "B" };
    float glitch = 0.0f;
    int lastRandomVersion = -1;
    juce::String lastPreset;
    RandomCluster randomCluster;
    RandomPanel randomPanel;
    juce::String flashText;
    juce::uint32 flashTime = 0;
    juce::Rectangle<int> flashArea;
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
    juce::Component::SafePointer<IDGemsSynthEditor> safe (this);
    proc.presets->onChange = [safe] { if (safe != nullptr) safe->content->refreshPresetName(); };

    tooltip = std::make_unique<juce::TooltipWindow> (this, 600);
    setWantsKeyboardFocus (true);

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
    proc.presets->onChange = nullptr;
    tooltip.reset();
    content.reset();
    setLookAndFeel (nullptr);
}

void IDGemsSynthEditor::showRandomPanel() { content->randomPanel.open(); }

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

bool IDGemsSynthEditor::keyPressed (const juce::KeyPress& k)
{
    return content->handleKey (k, [this] (int i) { selectTab (i); });
}

void IDGemsSynthEditor::timerCallback()
{
    content->tickReactive();
    content->meter.tick();
    content->scope.tick();
    content->synth.tick();
    content->tickFlash();
}
