#pragma once
#include <memory>
#include <string>
#include <vector>
#include <functional>

struct Action {
    virtual ~Action() = default;
    virtual void undo() = 0;
    virtual void redo() = 0;
    virtual std::string desc() const { return std::string(); }
};

// A simple action that wraps undo/redo lambdas
struct LambdaAction : Action {
    std::function<void()> undo_fn;
    std::function<void()> redo_fn;
    std::string d;
    LambdaAction(std::function<void()> u, std::function<void()> r, std::string desc = {})
        : undo_fn(std::move(u)), redo_fn(std::move(r)), d(std::move(desc)) {}
    void undo() override { if (undo_fn) undo_fn(); }
    void redo() override { if (redo_fn) redo_fn(); }
    std::string desc() const override { return d; }
};

class UndoManager {
public:
    static UndoManager& Instance();

    void push(std::unique_ptr<Action> a);
    bool undo();
    bool redo();
    bool can_undo() const { return !undoStack.empty(); }
    bool can_redo() const { return !redoStack.empty(); }
    void clear();
    void set_max_history(size_t n) { max_history = n; }

private:
    std::vector<std::unique_ptr<Action>> undoStack;
    std::vector<std::unique_ptr<Action>> redoStack;
    size_t max_history = 200;
};
