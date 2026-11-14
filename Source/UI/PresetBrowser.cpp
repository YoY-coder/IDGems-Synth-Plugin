#include "PresetBrowser.h"

namespace idgs::ui
{
PresetBrowser::PresetBrowser (IDGemsSynthProcessor& p) : proc (p)
{
    search.setTextToShowWhenEmpty ("Rechercher (nom, auteur, tag)...", Theme::textDim);
    search.onTextChange = [this] { refilter(); };
    addAndMakeVisible (search);
    list.setRowHeight (24);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (list);
    juce::StringArray cs { "Tous", "Factory" };
    cs.addArray (PresetManager::categories());
    for (auto& s : cs)
    {
        auto* b = cats.add (new juce::TextButton (s.toUpperCase()));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (77);
        b->setColour (juce::TextButton::buttonOnColourId, Theme::accent);
        b->onClick = [this, s] { category = s == "Tous" ? juce::String() : s; refilter(); };
        addAndMakeVisible (b);
    }
    cats[0]->setToggleState (true, juce::dontSendNotification);
    favOnly.setClickingTogglesState (true);
    favOnly.setColour (juce::TextButton::buttonOnColourId, Theme::accent);
    favOnly.onClick = [this] { refilter(); };
    closeBtn.onClick = [this] { close(); };
    saveBtn.onClick = [this] { openSave(); };
    rescan.onClick = [this] { proc.presets->rescan(); refilter(); };
    openFolder.setTooltip (IDG_T ("Ouvre le dossier des presets (pour copier / partager des fichiers .idgsynth)."));
    openFolder.onClick = [] { PresetManager::folder().startAsProcess(); };
    for (auto* b : { &favOnly, &closeBtn, &saveBtn, &rescan, &openFolder }) addAndMakeVisible (b);

    nameEd.setTextToShowWhenEmpty ("Nom du preset", Theme::textDim);
    authorEd.setTextToShowWhenEmpty ("Auteur", Theme::textDim);
    tagsEd.setTextToShowWhenEmpty (IDG_T ("Tags (séparés par des virgules)"), Theme::textDim);
    catBox.addItemList (PresetManager::categories(), 1);
    catBox.setSelectedItemIndex (PresetManager::categories().size() - 1);
    doSave.setColour (juce::TextButton::buttonColourId, Theme::accent.withAlpha (0.7f));
    doSave.onClick = [this]
    {
        if (nameEd.isEmpty()) return;
        if (auto* f = proc.appProps.getUserSettings()) f->setValue ("author", authorEd.getText());
        const bool ok = proc.presets->save (nameEd.getText(), authorEd.getText(), catBox.getText(), tagsEd.getText());
        if (flash) flash (ok ? IDG_T ("Preset sauvegardé") : IDG_T ("Échec de la sauvegarde"));
        saveMode = false;
        close();
    };
    for (auto* c2 : std::initializer_list<juce::Component*> { &nameEd, &authorEd, &tagsEd, &catBox, &doSave }) addChildComponent (c2);
}

void PresetBrowser::open()
{
    saveMode = false;
    for (auto* c2 : std::initializer_list<juce::Component*> { &nameEd, &authorEd, &tagsEd, &catBox, &doSave }) c2->setVisible (false);
    for (auto* c2 : std::initializer_list<juce::Component*> { &list, &search, &favOnly, &rescan, &openFolder }) c2->setVisible (true);
    for (auto* b : cats) b->setVisible (true);
    proc.presets->rescan();
    refilter();
    setVisible (true);
    toFront (true);
    resized();
    search.grabKeyboardFocus();
}

void PresetBrowser::openSave()
{
    saveMode = true;
    setVisible (true);
    toFront (true);
    for (auto* c2 : std::initializer_list<juce::Component*> { &list, &search, &favOnly, &rescan, &openFolder }) c2->setVisible (false);
    for (auto* b : cats) b->setVisible (false);
    for (auto* c2 : std::initializer_list<juce::Component*> { &nameEd, &authorEd, &tagsEd, &catBox, &doSave }) c2->setVisible (true);
    nameEd.setText (proc.presets->currentName(), false);
    if (auto* f = proc.appProps.getUserSettings()) authorEd.setText (f->getValue ("author"), false);
    resized();
    nameEd.grabKeyboardFocus();
}

void PresetBrowser::refilter()
{
    filtered = proc.presets->filter (search.getText(), category, favOnly.getToggleState());
    list.updateContent();
    list.repaint();
}

void PresetBrowser::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.7f));
    g.setColour (Theme::bg);
    g.fillRect (panelR);
    Theme::panelFrame (g, panelR.toFloat(), Theme::accent);
    Theme::cutText (g, saveMode ? "SAUVER LE PRESET" : "PRESETS", panelR.toFloat().reduced (16, 10).withHeight (26), 18.0f, Theme::accent,
                    juce::Justification::centredLeft, 0.4f, true);
    if (saveMode)
    {
        g.setFont (Theme::mono (11.0f));
        g.setColour (Theme::textDim);
        g.drawText (IDG_T ("Dossier : ") + PresetManager::folder().getFullPathName(), panelR.reduced (16, 14).removeFromBottom (18), juce::Justification::left);
    }
}

void PresetBrowser::resized()
{
    panelR = getLocalBounds().reduced (getWidth() / 8, getHeight() / 10);
    auto r = panelR.reduced (16, 12);
    auto top = r.removeFromTop (28);
    closeBtn.setBounds (top.removeFromRight (80));
    top.removeFromRight (6);
    saveBtn.setBounds (top.removeFromRight (70));
    top.removeFromRight (6);
    rescan.setBounds (top.removeFromRight (100));
    top.removeFromRight (6);
    openFolder.setBounds (top.removeFromRight (80));
    r.removeFromTop (10);
    if (saveMode)
    {
        auto f = r.removeFromTop (190).withWidth (440);
        for (auto* c2 : std::initializer_list<juce::Component*> { &nameEd, &authorEd, &tagsEd, &catBox })
        {
            c2->setBounds (f.removeFromTop (28));
            f.removeFromTop (8);
        }
        doSave.setBounds (f.removeFromTop (30).withWidth (160));
        return;
    }
    auto left = r.removeFromLeft (160);
    for (auto* b : cats) { b->setBounds (left.removeFromTop (24)); left.removeFromTop (3); }
    left.removeFromTop (10);
    favOnly.setBounds (left.removeFromTop (24));
    r.removeFromLeft (12);
    search.setBounds (r.removeFromTop (26));
    r.removeFromTop (8);
    list.setBounds (r);
}

void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) filtered.size()) return;
    const auto& p = proc.presets->getAll()[(size_t) filtered[(size_t) row]];
    const bool current = filtered[(size_t) row] == proc.presets->currentIndex();
    g.setColour (selected || current ? Theme::accent.withAlpha (0.25f) : (row % 2 ? Theme::panelHi.withAlpha (0.5f) : juce::Colours::transparentBlack));
    g.fillRect (0, 0, w, h);
    // étoile des favoris (cliquable)
    g.setColour (p.favourite ? Theme::accent : Theme::line.brighter (0.3f));
    juce::Path star;
    star.addStar ({ 14.0f, (float) h * 0.5f }, 5, 3.0f, 7.0f);
    if (p.favourite) g.fillPath (star); else g.strokePath (star, juce::PathStrokeType (1.0f));
    g.setColour (Theme::text);
    g.setFont (Theme::sans (14.0f, current));
    g.drawText (p.name, 30, 0, w / 2 - 30, h, juce::Justification::centredLeft);
    g.setFont (Theme::mono (10.5f));
    g.setColour (Theme::textDim);
    g.drawText (p.author, w / 2, 0, w / 6, h, juce::Justification::centredLeft);
    g.setColour (Theme::blue);
    g.drawText (p.category, w / 2 + w / 6, 0, w / 7, h, juce::Justification::centredLeft);
    g.setColour (Theme::textDim);
    g.drawText (p.tags, w / 2 + w / 6 + w / 7, 0, w - (w / 2 + w / 6 + w / 7) - 4, h, juce::Justification::centredLeft);
}

void PresetBrowser::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= (int) filtered.size()) return;
    if (e.x < 28) { proc.presets->toggleFavourite (filtered[(size_t) row]); refilter(); return; }
    proc.presets->load (filtered[(size_t) row]);
    list.repaint();
}

void PresetBrowser::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (row < 0 || row >= (int) filtered.size()) return;
    close();
}
} // namespace idgs::ui
