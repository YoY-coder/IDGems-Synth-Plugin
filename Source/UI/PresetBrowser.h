#pragma once
#include "Widgets.h"

namespace idgs::ui
{
/** Navigateur de presets façon Vital (repris d'IDGems) : catégories, recherche, favoris, auteur, tags,
    + fenêtre de sauvegarde. S'affiche par-dessus l'interface. */
class PresetBrowser : public juce::Component, private juce::ListBoxModel
{
public:
    explicit PresetBrowser (IDGemsSynthProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void open();
    void openSave();
    std::function<void (const juce::String&)> flash;

private:
    int getNumRows() override { return (int) filtered.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent& e) override { if (! panelR.contains (e.getPosition())) close(); }
    void refilter();
    void close() { setVisible (false); }

    IDGemsSynthProcessor& proc;
    juce::TextEditor search;
    juce::ListBox list { "presets", this };
    juce::OwnedArray<juce::TextButton> cats;
    juce::TextButton closeBtn { "FERMER" }, favOnly { "FAVORIS" }, saveBtn { "SAUVER" }, doSave { "ENREGISTRER" },
        rescan { IDG_T ("RAFRAÎCHIR") }, openFolder { "DOSSIER" };
    juce::TextEditor nameEd, authorEd, tagsEd;
    juce::ComboBox catBox;
    juce::String category;
    std::vector<int> filtered;
    bool saveMode = false;
    juce::Rectangle<int> panelR;
};
} // namespace idgs::ui
