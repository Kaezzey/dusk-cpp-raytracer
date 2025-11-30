#include "../../include/core/undo.h"

UndoManager& UndoManager::Instance()
{
    static UndoManager inst;
    return inst;
}

void UndoManager::push(std::unique_ptr<Action> a)
{
    if (!a) return;
    undoStack.push_back(std::move(a));
    redoStack.clear();
    if (undoStack.size() > max_history) {
        undoStack.erase(undoStack.begin());
    }
}

bool UndoManager::undo()
{
    if (undoStack.empty()) return false;
    auto a = std::move(undoStack.back());
    undoStack.pop_back();
    a->undo();
    redoStack.push_back(std::move(a));
    return true;
}

bool UndoManager::redo()
{
    if (redoStack.empty()) return false;
    auto a = std::move(redoStack.back());
    redoStack.pop_back();
    a->redo();
    undoStack.push_back(std::move(a));
    return true;
}

void UndoManager::clear()
{
    undoStack.clear();
    redoStack.clear();
}
