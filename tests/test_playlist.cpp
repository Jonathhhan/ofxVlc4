// Tests for playlist manipulation logic (add, remove, move) as implemented in
// MediaLibrary.  The Playlist helper class below is a self-contained mirror of
// the core locked operations so that these tests require no OF, GLFW, or VLC
// dependencies.

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Minimal test harness (mirrors test_utils.cpp)
// ---------------------------------------------------------------------------

static int g_passed = 0;
static int g_failed = 0;
static std::string g_currentSuite;

static void beginSuite(const char * name) {
	g_currentSuite = name;
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

#define CHECK(expr) check((expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(a, b) check((a) == (b), #a " == " #b, __FILE__, __LINE__)

// ---------------------------------------------------------------------------
// Playlist – a self-contained fixture that mirrors MediaLibrary's locked
// playlist operations.  currentIndex mirrors MediaLibraryState::currentIndex
// and is kept in sync exactly as MediaLibrary does it.
// ---------------------------------------------------------------------------

struct Playlist {
	std::vector<std::string> items;
	int currentIndex = -1;

	// --- helpers -----------------------------------------------------------

	bool empty() const { return items.empty(); }
	int size() const { return static_cast<int>(items.size()); }
	bool validIndex(int i) const { return i >= 0 && i < size(); }

	// --- add ---------------------------------------------------------------

	void add(const std::string & path) {
		items.push_back(path);
		if (currentIndex < 0) {
			currentIndex = 0;
		}
	}

	// --- remove ------------------------------------------------------------
	// Returns the replacement currentIndex, or -1 when the playlist becomes
	// empty, mirroring MediaLibrary::removePlaylistItem.

	struct RemoveResult {
		bool removed = false;
		bool wasCurrent = false;
		bool playlistEmpty = false;
		int replacementIndex = -1;
	};

	RemoveResult remove(int index) {
		RemoveResult result;
		if (!validIndex(index)) {
			return result;
		}

		result.removed = true;
		result.wasCurrent = (index == currentIndex);

		items.erase(items.begin() + index);

		if (items.empty()) {
			currentIndex = -1;
			result.playlistEmpty = true;
		} else {
			if (index < currentIndex) {
				--currentIndex;
			} else if (index == result.wasCurrent ? index : -1) {
				// wasCurrent case handled below
			}
			// Re-implement the exact MediaLibrary logic:
			// Already adjusted for index < currentIndex above.
			// Now handle index == original currentIndex, clamping to last.
			if (result.wasCurrent && currentIndex >= size()) {
				currentIndex = size() - 1;
			}
			if (result.wasCurrent && currentIndex >= 0) {
				result.replacementIndex = currentIndex;
			}
		}
		return result;
	}

	// --- move (single item) ------------------------------------------------
	// Mirrors MediaLibrary::movePlaylistItem.

	void moveItem(int fromIndex, int toIndex) {
		if (!validIndex(fromIndex)) return;
		if (toIndex < 0) return;
		if (toIndex > size()) toIndex = size();
		if (toIndex == fromIndex || toIndex == fromIndex + 1) return;

		const int originalCurrent = currentIndex;
		const std::string moved = items[static_cast<size_t>(fromIndex)];
		const int insertIndex = (fromIndex < toIndex) ? (toIndex - 1) : toIndex;

		items.erase(items.begin() + fromIndex);
		items.insert(items.begin() + insertIndex, moved);

		if (originalCurrent == fromIndex) {
			currentIndex = insertIndex;
		} else if (originalCurrent > fromIndex && originalCurrent <= insertIndex) {
			currentIndex = originalCurrent - 1;
		} else if (originalCurrent < fromIndex && originalCurrent >= insertIndex) {
			currentIndex = originalCurrent + 1;
		} else {
			currentIndex = originalCurrent;
		}
	}

	// --- move (multiple items) ---------------------------------------------
	// Mirrors MediaLibrary::movePlaylistItems.

	void moveItems(std::vector<int> indices, int toIndex) {
		if (indices.empty()) return;
		if (items.empty()) return;

		std::sort(indices.begin(), indices.end());
		indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
		indices.erase(
			std::remove_if(indices.begin(), indices.end(), [this](int i) { return !validIndex(i); }),
			indices.end());
		if (indices.empty()) return;

		if (toIndex < 0) toIndex = 0;
		if (toIndex > size()) toIndex = size();

		std::vector<std::string> movedItems;
		movedItems.reserve(indices.size());
		std::vector<std::string> remaining;
		remaining.reserve(static_cast<size_t>(size()) - indices.size());

		size_t cursor = 0;
		for (int i = 0; i < size(); ++i) {
			if (cursor < indices.size() && indices[cursor] == i) {
				movedItems.push_back(items[static_cast<size_t>(i)]);
				++cursor;
			} else {
				remaining.push_back(items[static_cast<size_t>(i)]);
			}
		}

		const int removedBeforeInsert = static_cast<int>(
			std::count_if(indices.begin(), indices.end(), [toIndex](int i) { return i < toIndex; }));
		int adjustedInsert = toIndex - removedBeforeInsert;
		if (adjustedInsert < 0) adjustedInsert = 0;
		if (adjustedInsert > static_cast<int>(remaining.size()))
			adjustedInsert = static_cast<int>(remaining.size());

		if (currentIndex >= 0) {
			const auto it = std::find(indices.begin(), indices.end(), currentIndex);
			if (it != indices.end()) {
				currentIndex = adjustedInsert + static_cast<int>(std::distance(indices.begin(), it));
			} else {
				const int removedBeforeCurrent = static_cast<int>(
					std::count_if(indices.begin(), indices.end(), [this](int i) { return i < currentIndex; }));
				int remainingIdx = currentIndex - removedBeforeCurrent;
				if (remainingIdx >= adjustedInsert) {
					remainingIdx += static_cast<int>(movedItems.size());
				}
				currentIndex = remainingIdx;
			}
		}

		remaining.insert(remaining.begin() + adjustedInsert, movedItems.begin(), movedItems.end());
		items = std::move(remaining);
	}
};

// ---------------------------------------------------------------------------
// Helper: build a playlist from a list of labels { "a", "b", "c", ... }
// ---------------------------------------------------------------------------

static Playlist make(std::initializer_list<const char *> labels, int current = 0) {
	Playlist pl;
	for (const char * l : labels) {
		pl.items.emplace_back(l);
	}
	pl.currentIndex = labels.size() > 0 ? current : -1;
	return pl;
}

// ---------------------------------------------------------------------------
// Tests: add
// ---------------------------------------------------------------------------

static void testAdd() {
	beginSuite("add");

	{
		Playlist pl;
		CHECK(pl.empty());
		CHECK_EQ(pl.currentIndex, -1);
		pl.add("a");
		CHECK(!pl.empty());
		CHECK_EQ(pl.size(), 1);
		CHECK_EQ(pl.currentIndex, 0);
	}

	{
		Playlist pl;
		pl.add("a");
		pl.add("b");
		pl.add("c");
		CHECK_EQ(pl.size(), 3);
		// currentIndex stays at 0 after the first add
		CHECK_EQ(pl.currentIndex, 0);
		CHECK_EQ(pl.items[0], "a");
		CHECK_EQ(pl.items[1], "b");
		CHECK_EQ(pl.items[2], "c");
	}
}

// ---------------------------------------------------------------------------
// Tests: remove
// ---------------------------------------------------------------------------

static void testRemove() {
	beginSuite("remove");

	// Remove the only item
	{
		Playlist pl = make({ "a" }, 0);
		const auto r = pl.remove(0);
		CHECK(r.removed);
		CHECK(r.wasCurrent);
		CHECK(r.playlistEmpty);
		CHECK_EQ(r.replacementIndex, -1);
		CHECK(pl.empty());
		CHECK_EQ(pl.currentIndex, -1);
	}

	// Remove item before current → currentIndex decremented
	{
		Playlist pl = make({ "a", "b", "c" }, 2);
		pl.remove(0); // remove "a"
		CHECK_EQ(pl.size(), 2);
		CHECK_EQ(pl.items[0], "b");
		CHECK_EQ(pl.items[1], "c");
		CHECK_EQ(pl.currentIndex, 1); // was 2, now 1
	}

	// Remove item after current → currentIndex unchanged
	{
		Playlist pl = make({ "a", "b", "c" }, 1);
		pl.remove(2); // remove "c"
		CHECK_EQ(pl.size(), 2);
		CHECK_EQ(pl.currentIndex, 1); // unchanged
	}

	// Remove current item when it's the last in the list → clamp to new end
	{
		Playlist pl = make({ "a", "b", "c" }, 2);
		const auto r = pl.remove(2); // remove "c" (current, last)
		CHECK(r.wasCurrent);
		CHECK(!r.playlistEmpty);
		CHECK_EQ(pl.size(), 2);
		CHECK_EQ(pl.currentIndex, 1); // clamped to new last index
		CHECK_EQ(r.replacementIndex, 1);
	}

	// Remove current item in the middle → currentIndex stays at same position
	{
		Playlist pl = make({ "a", "b", "c" }, 1);
		const auto r = pl.remove(1); // remove "b" (current, middle)
		CHECK(r.wasCurrent);
		CHECK(!r.playlistEmpty);
		CHECK_EQ(pl.size(), 2);
		// After erase index 1, the new index 1 ("c") becomes current
		CHECK_EQ(pl.currentIndex, 1);
		CHECK_EQ(r.replacementIndex, 1);
	}

	// Remove out-of-range index → no-op
	{
		Playlist pl = make({ "a", "b" }, 0);
		const auto r = pl.remove(5);
		CHECK(!r.removed);
		CHECK_EQ(pl.size(), 2);
		CHECK_EQ(pl.currentIndex, 0);
	}
}

// ---------------------------------------------------------------------------
// Tests: moveItem (single)
// ---------------------------------------------------------------------------

static void testMoveItem() {
	beginSuite("moveItem (single)");

	// Move first item to end
	{
		Playlist pl = make({ "a", "b", "c" }, 0);
		pl.moveItem(0, 3); // insert before position 3 → becomes last
		CHECK_EQ(pl.items[0], "b");
		CHECK_EQ(pl.items[1], "c");
		CHECK_EQ(pl.items[2], "a");
		CHECK_EQ(pl.currentIndex, 2); // "a" was current, followed it
	}

	// Move last item to front
	{
		Playlist pl = make({ "a", "b", "c" }, 2);
		pl.moveItem(2, 0);
		CHECK_EQ(pl.items[0], "c");
		CHECK_EQ(pl.items[1], "a");
		CHECK_EQ(pl.items[2], "b");
		CHECK_EQ(pl.currentIndex, 0); // "c" was current, followed it
	}

	// Move item over current → current shifts down
	{
		Playlist pl = make({ "a", "b", "c", "d" }, 2); // current = "c"
		pl.moveItem(3, 1); // move "d" before "b"; inserts at 1 → "a","d","b","c"
		CHECK_EQ(pl.items[0], "a");
		CHECK_EQ(pl.items[1], "d");
		CHECK_EQ(pl.items[2], "b");
		CHECK_EQ(pl.items[3], "c");
		CHECK_EQ(pl.currentIndex, 3); // "c" shifted right
	}

	// No-op: toIndex == fromIndex
	{
		Playlist pl = make({ "a", "b", "c" }, 1);
		pl.moveItem(1, 1);
		CHECK_EQ(pl.items[0], "a");
		CHECK_EQ(pl.items[1], "b");
		CHECK_EQ(pl.items[2], "c");
		CHECK_EQ(pl.currentIndex, 1);
	}

	// No-op: toIndex == fromIndex + 1 (adjacent)
	{
		Playlist pl = make({ "a", "b", "c" }, 1);
		pl.moveItem(1, 2);
		CHECK_EQ(pl.items[1], "b");
		CHECK_EQ(pl.currentIndex, 1);
	}

	// Invalid fromIndex → no-op
	{
		Playlist pl = make({ "a", "b" }, 0);
		pl.moveItem(5, 0);
		CHECK_EQ(pl.size(), 2);
		CHECK_EQ(pl.currentIndex, 0);
	}
}

// ---------------------------------------------------------------------------
// Tests: moveItems (multi)
// ---------------------------------------------------------------------------

static void testMoveItems() {
	beginSuite("moveItems (multi)");

	// Move two non-adjacent items to the front
	{
		Playlist pl = make({ "a", "b", "c", "d", "e" }, 0);
		pl.moveItems({ 1, 3 }, 0); // move "b","d" to front
		// remaining: "a","c","e"  movedItems: "b","d"  adjustedInsert=0
		// result: "b","d","a","c","e"
		CHECK_EQ(pl.items[0], "b");
		CHECK_EQ(pl.items[1], "d");
		CHECK_EQ(pl.items[2], "a");
		CHECK_EQ(pl.items[3], "c");
		CHECK_EQ(pl.items[4], "e");
		// "a" was current (0); 0 not in moved set; removedBeforeCurrent=0 (neither 1 nor 3 < 0) — wait, indices {1,3}: none < 0, so remainingIdx=0; 0 >= adjustedInsert(0) → remainingIdx += 2 = 2
		CHECK_EQ(pl.currentIndex, 2);
	}

	// Move two items to the end
	{
		Playlist pl = make({ "a", "b", "c", "d" }, 3); // current = "d"
		pl.moveItems({ 0, 2 }, 4); // move "a","c" to end
		// remaining: "b","d"  movedItems: "a","c"  removedBeforeInsert=2 adjustedInsert=2
		// result: "b","d","a","c"
		CHECK_EQ(pl.items[0], "b");
		CHECK_EQ(pl.items[1], "d");
		CHECK_EQ(pl.items[2], "a");
		CHECK_EQ(pl.items[3], "c");
		// "d" was at 3; not in moved set; removedBeforeCurrent(indices {0,2} < 3) = 2; remainingIdx = 1; 1 >= adjustedInsert(2)? no → currentIndex=1
		CHECK_EQ(pl.currentIndex, 1);
	}

	// Move the current item
	{
		Playlist pl = make({ "a", "b", "c", "d" }, 1); // current = "b"
		pl.moveItems({ 1 }, 3); // move "b" after "c"
		// remaining: "a","c","d"  movedItems: "b"  removedBeforeInsert=1 adjustedInsert=2
		// result: "a","c","b","d"
		CHECK_EQ(pl.items[0], "a");
		CHECK_EQ(pl.items[1], "c");
		CHECK_EQ(pl.items[2], "b");
		CHECK_EQ(pl.items[3], "d");
		// "b" is in moved set at position 0 in indices → currentIndex = adjustedInsert + 0 = 2
		CHECK_EQ(pl.currentIndex, 2);
	}

	// Empty indices → no-op
	{
		Playlist pl = make({ "a", "b", "c" }, 0);
		pl.moveItems({}, 0);
		CHECK_EQ(pl.size(), 3);
		CHECK_EQ(pl.currentIndex, 0);
	}

	// All duplicate indices normalised to one
	{
		Playlist pl = make({ "a", "b", "c" }, 0);
		pl.moveItems({ 2, 2, 2 }, 0);
		// Only "c" moves to front
		CHECK_EQ(pl.items[0], "c");
		CHECK_EQ(pl.items[1], "a");
		CHECK_EQ(pl.items[2], "b");
	}

	// Out-of-range indices silently ignored
	{
		Playlist pl = make({ "a", "b", "c" }, 1);
		pl.moveItems({ 99, 1 }, 0); // 99 is invalid
		// Only "b" moves to front
		CHECK_EQ(pl.items[0], "b");
		CHECK_EQ(pl.items[1], "a");
		CHECK_EQ(pl.items[2], "c");
	}

	// toIndex clamped to size
	{
		Playlist pl = make({ "a", "b", "c" }, 0);
		pl.moveItems({ 0 }, 100); // clamp to 3 (end)
		// "a" moves to end: "b","c","a"
		CHECK_EQ(pl.items[0], "b");
		CHECK_EQ(pl.items[1], "c");
		CHECK_EQ(pl.items[2], "a");
		CHECK_EQ(pl.currentIndex, 2); // "a" followed
	}
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	testAdd();
	testRemove();
	testMoveItem();
	testMoveItems();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
