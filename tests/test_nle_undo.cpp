// Tests for NleUndoStack.h — command-pattern undo/redo stack.

#include "NleUndoStack.h"

#include <cstdio>
#include <memory>
#include <string>

using namespace nle;

// ---------------------------------------------------------------------------
// Minimal test harness
// ---------------------------------------------------------------------------

static int g_passed = 0;
static int g_failed = 0;

static void beginSuite(const char * name) {
	std::printf("\n[%s]\n", name);
}

static void check(bool condition, const char * expr, const char * file, int line) {
	if (condition) {
		++g_passed;
		std::printf("  PASS  %s\n", expr);
	} else {
		++g_failed;
		std::printf("  FAIL  %s  (%s:%d)\n", expr, file, line);
	}
}

#define CHECK(expr)    check((expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(a, b) check((a) == (b), #a " == " #b, __FILE__, __LINE__)

// ---------------------------------------------------------------------------
// A simple test command: increments/decrements a counter.
// ---------------------------------------------------------------------------

class IncrementCommand : public UndoableCommand {
public:
	IncrementCommand(int & counter, int delta, const std::string & desc)
		: m_counter(counter), m_delta(delta), m_desc(desc) {}

	void execute() override { m_counter += m_delta; }
	void undo() override    { m_counter -= m_delta; }
	std::string description() const override { return m_desc; }

private:
	int & m_counter;
	int m_delta;
	std::string m_desc;
};

// ---------------------------------------------------------------------------
// Execute, undo, redo cycle
// ---------------------------------------------------------------------------

static void testExecuteUndoRedo() {
	beginSuite("Execute, undo, redo cycle");

	int counter = 0;
	UndoStack stack;

	stack.execute(std::make_unique<IncrementCommand>(counter, 10, "Add 10"));
	CHECK_EQ(counter, 10);

	stack.undo();
	CHECK_EQ(counter, 0);

	stack.redo();
	CHECK_EQ(counter, 10);
}

// ---------------------------------------------------------------------------
// Multiple commands stacked
// ---------------------------------------------------------------------------

static void testMultipleCommands() {
	beginSuite("Multiple commands stacked");

	int counter = 0;
	UndoStack stack;

	stack.execute(std::make_unique<IncrementCommand>(counter, 1, "Add 1"));
	stack.execute(std::make_unique<IncrementCommand>(counter, 2, "Add 2"));
	stack.execute(std::make_unique<IncrementCommand>(counter, 3, "Add 3"));
	CHECK_EQ(counter, 6);

	stack.undo();
	CHECK_EQ(counter, 3);

	stack.undo();
	CHECK_EQ(counter, 1);

	stack.undo();
	CHECK_EQ(counter, 0);

	// No more to undo.
	stack.undo();
	CHECK_EQ(counter, 0);
}

// ---------------------------------------------------------------------------
// Redo cleared on new command after undo
// ---------------------------------------------------------------------------

static void testRedoClearedOnNewCommand() {
	beginSuite("Redo cleared on new command");

	int counter = 0;
	UndoStack stack;

	stack.execute(std::make_unique<IncrementCommand>(counter, 10, "A"));
	stack.execute(std::make_unique<IncrementCommand>(counter, 20, "B"));
	CHECK_EQ(counter, 30);

	// Undo B.
	stack.undo();
	CHECK_EQ(counter, 10);
	CHECK(stack.canRedo());

	// Execute a new command — redo history should be cleared.
	stack.execute(std::make_unique<IncrementCommand>(counter, 5, "C"));
	CHECK_EQ(counter, 15);
	CHECK(!stack.canRedo());
	CHECK_EQ(stack.redoCount(), static_cast<size_t>(0));
}

// ---------------------------------------------------------------------------
// canUndo / canRedo state tracking
// ---------------------------------------------------------------------------

static void testCanUndoCanRedo() {
	beginSuite("canUndo / canRedo state tracking");

	int counter = 0;
	UndoStack stack;

	CHECK(!stack.canUndo());
	CHECK(!stack.canRedo());

	stack.execute(std::make_unique<IncrementCommand>(counter, 1, "X"));
	CHECK(stack.canUndo());
	CHECK(!stack.canRedo());

	stack.undo();
	CHECK(!stack.canUndo());
	CHECK(stack.canRedo());

	stack.redo();
	CHECK(stack.canUndo());
	CHECK(!stack.canRedo());
}

// ---------------------------------------------------------------------------
// clear() resets everything
// ---------------------------------------------------------------------------

static void testClear() {
	beginSuite("clear() resets everything");

	int counter = 0;
	UndoStack stack;

	stack.execute(std::make_unique<IncrementCommand>(counter, 1, "A"));
	stack.execute(std::make_unique<IncrementCommand>(counter, 2, "B"));
	stack.undo();

	CHECK(stack.canUndo());
	CHECK(stack.canRedo());

	stack.clear();

	CHECK(!stack.canUndo());
	CHECK(!stack.canRedo());
	CHECK_EQ(stack.undoCount(), static_cast<size_t>(0));
	CHECK_EQ(stack.redoCount(), static_cast<size_t>(0));
}

// ---------------------------------------------------------------------------
// Description strings
// ---------------------------------------------------------------------------

static void testDescriptions() {
	beginSuite("Description strings");

	int counter = 0;
	UndoStack stack;

	// Empty stack — empty descriptions.
	CHECK_EQ(stack.undoDescription(), std::string(""));
	CHECK_EQ(stack.redoDescription(), std::string(""));

	stack.execute(std::make_unique<IncrementCommand>(counter, 1, "First"));
	CHECK_EQ(stack.undoDescription(), std::string("First"));

	stack.execute(std::make_unique<IncrementCommand>(counter, 2, "Second"));
	CHECK_EQ(stack.undoDescription(), std::string("Second"));

	stack.undo();
	CHECK_EQ(stack.undoDescription(), std::string("First"));
	CHECK_EQ(stack.redoDescription(), std::string("Second"));
}

// ---------------------------------------------------------------------------
// Undo/redo counts
// ---------------------------------------------------------------------------

static void testCounts() {
	beginSuite("Undo/redo counts");

	int counter = 0;
	UndoStack stack;

	CHECK_EQ(stack.undoCount(), static_cast<size_t>(0));
	CHECK_EQ(stack.redoCount(), static_cast<size_t>(0));

	stack.execute(std::make_unique<IncrementCommand>(counter, 1, "A"));
	stack.execute(std::make_unique<IncrementCommand>(counter, 2, "B"));
	CHECK_EQ(stack.undoCount(), static_cast<size_t>(2));
	CHECK_EQ(stack.redoCount(), static_cast<size_t>(0));

	stack.undo();
	CHECK_EQ(stack.undoCount(), static_cast<size_t>(1));
	CHECK_EQ(stack.redoCount(), static_cast<size_t>(1));
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	testExecuteUndoRedo();
	testMultipleCommands();
	testRedoClearedOnNewCommand();
	testCanUndoCanRedo();
	testClear();
	testDescriptions();
	testCounts();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
