#pragma once

// ---------------------------------------------------------------------------
// NleUndoStack — Command-pattern undo/redo stack.
//
// UndoableCommand is the abstract interface for reversible operations.
// UndoStack manages execution, undo, and redo of these commands.
//
// Pure logic — no dependencies on OF, GLFW, or VLC.
// ---------------------------------------------------------------------------

#include <memory>
#include <string>
#include <vector>

namespace nle {

// ---------------------------------------------------------------------------
// UndoableCommand — interface for reversible operations.
// ---------------------------------------------------------------------------

class UndoableCommand {
public:
	virtual ~UndoableCommand() = default;

	/// Execute the command (first time or redo).
	virtual void execute() = 0;

	/// Reverse the command.
	virtual void undo() = 0;

	/// Human-readable description (for UI display).
	virtual std::string description() const = 0;
};

// ---------------------------------------------------------------------------
// UndoStack
// ---------------------------------------------------------------------------

class UndoStack {
public:
	/// Execute a command and push it onto the undo stack.
	/// Clears any redo history.
	inline void execute(std::unique_ptr<UndoableCommand> cmd);

	inline bool canUndo() const;
	inline bool canRedo() const;

	/// Undo the most recent command.
	inline void undo();

	/// Redo the most recently undone command.
	inline void redo();

	/// Description of the command that would be undone.
	inline std::string undoDescription() const;

	/// Description of the command that would be redone.
	inline std::string redoDescription() const;

	/// Clear all undo and redo history.
	inline void clear();

	inline size_t undoCount() const;
	inline size_t redoCount() const;

private:
	std::vector<std::unique_ptr<UndoableCommand>> m_undoStack;
	std::vector<std::unique_ptr<UndoableCommand>> m_redoStack;
};

// ---------------------------------------------------------------------------
// Inline implementations
// ---------------------------------------------------------------------------

inline void UndoStack::execute(std::unique_ptr<UndoableCommand> cmd) {
	if (!cmd) return;
	cmd->execute();
	m_undoStack.push_back(std::move(cmd));
	m_redoStack.clear();
}

inline bool UndoStack::canUndo() const { return !m_undoStack.empty(); }
inline bool UndoStack::canRedo() const { return !m_redoStack.empty(); }

inline void UndoStack::undo() {
	if (m_undoStack.empty()) return;
	auto cmd = std::move(m_undoStack.back());
	m_undoStack.pop_back();
	cmd->undo();
	m_redoStack.push_back(std::move(cmd));
}

inline void UndoStack::redo() {
	if (m_redoStack.empty()) return;
	auto cmd = std::move(m_redoStack.back());
	m_redoStack.pop_back();
	cmd->execute();
	m_undoStack.push_back(std::move(cmd));
}

inline std::string UndoStack::undoDescription() const {
	if (m_undoStack.empty()) return "";
	return m_undoStack.back()->description();
}

inline std::string UndoStack::redoDescription() const {
	if (m_redoStack.empty()) return "";
	return m_redoStack.back()->description();
}

inline void UndoStack::clear() {
	m_undoStack.clear();
	m_redoStack.clear();
}

inline size_t UndoStack::undoCount() const { return m_undoStack.size(); }
inline size_t UndoStack::redoCount() const { return m_redoStack.size(); }

} // namespace nle
