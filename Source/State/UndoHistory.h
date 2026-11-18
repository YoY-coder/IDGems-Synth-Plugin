#pragma once
#include "../Core/JuceIncludes.h"

class IDGemsSynthProcessor;

namespace idgs
{
/** Undo / redo par instantanés + comparaison A / B (thread de l'interface).
    Un changement est validé quand la souris est relâchée : un geste sur un potard = une étape. */
class UndoHistory : private juce::Timer
{
public:
    explicit UndoHistory (IDGemsSynthProcessor& p);
    ~UndoHistory() override { stopTimer(); }

    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    void undo();
    void redo();
    /** Valide tout de suite l'état actuel (sinon : automatique 4 fois par seconde). */
    void commit();

    // ---- A / B
    int activeSlot() const { return slot; }
    void selectSlot (int s);   // 0 = A, 1 = B
    void copyActiveToOther();

    std::function<void()> onChange;
    static constexpr int maxSteps = 60;

private:
    void timerCallback() override;
    std::vector<float> signature() const;
    void restore (const juce::ValueTree& t);
    IDGemsSynthProcessor& proc;
    std::vector<juce::ValueTree> undoStack, redoStack;
    juce::ValueTree current;
    std::vector<float> sig;
    std::vector<bool> isMacro;
    int slot = 0;
    juce::ValueTree slots[2];
};
} // namespace idgs
