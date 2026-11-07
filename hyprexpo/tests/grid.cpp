#include "../Grid.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>
#include <string>

static void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

int main() {
    check(Grid::columns(0) == 1, "zero columns must be safe");
    check(Grid::columns(-5) == 1, "negative columns must be safe");
    check(Grid::columns(std::numeric_limits<int64_t>::max()) == Grid::MAX_COLUMNS, "columns must be bounded before narrowing or allocating");
    check(Grid::columns(3) == 3, "valid configuration must be preserved");

    check(Grid::tileAt(0, 0, 3) == 0, "top left tile");
    check(Grid::tileAt(2.99, 2.99, 3) == 8, "bottom right tile");
    check(Grid::tileAt(0, -0.01, 3) == -1, "negative positions must not truncate to zero");
    check(Grid::tileAt(3, 0, 3) == -1, "right edge must not wrap to next row");
    check(Grid::tileAt(0, 3, 3) == -1, "bottom edge must not index past grid");
    check(Grid::tileAt(std::numeric_limits<double>::quiet_NaN(), 0, 3) == -1, "NaN input must not be converted to an index");
    check(Grid::tileAt(0, std::numeric_limits<double>::infinity(), 3) == -1, "infinite input must not be converted to an index");
    check(Grid::validTile(9, 4, 9) == 4, "one-past-end selection falls back to the opened workspace");
    check(Grid::validTile(-1, 4, 9) == 4, "missing selection falls back to the opened workspace");
    check(Grid::validTile(8, 4, 9) == 8, "last tile remains selectable");

    check(Grid::tileAtPosition(50, 50, 0, 0, 1, 100, 100, 5, 3) == 0, "point inside a tile");
    check(Grid::tileAtPosition(102, 50, 0, 0, 1, 100, 100, 5, 3) == -1, "gaps must not select a workspace");
    check(Grid::tileAtPosition(105, 50, 0, 0, 1, 100, 100, 5, 3) == 1, "tile after a gap");
    check(Grid::tileAtPosition(500, 300, -2000, -1200, 2, 1000, 600, 0, 3) == 4, "physical camera offsets must be converted at monitor scale two");
    check(Grid::tileAtPosition(500, 300, -1500, -900, 1.5, 1000, 600, 0, 3) == 4, "fractional monitor scale must select the same tile");
    check(Grid::tileAtPosition(500, 300, 0, 0, 0, 1000, 600, 0, 3) == -1, "zero monitor scale must be safe");
    check(Grid::tileAtPosition(500, 300, 0, 0, 1, 0, 600, 0, 3) == -1, "zero tile size must be safe");

    constexpr int64_t invalid    = -999;
    auto              sequential = [](int offset) -> int64_t { return 5 + offset; };
    check(Grid::workspaceIDs(9, true, invalid, sequential) == std::vector<int64_t>({1, 2, 3, 4, 5, 6, 7, 8, 9}), "center must place the anchor in the middle tile");
    check(Grid::workspaceIDs(9, false, invalid, sequential) == std::vector<int64_t>({5, 6, 7, 8, 9, 10, 11, 12, 13}), "first must start at the configured anchor");
    check(Grid::workspaceIDs(1, true, invalid, sequential) == std::vector<int64_t>({5}), "a single tile needs no backtracking");
    auto wrapping = [](int offset) -> int64_t {
        constexpr int64_t workspaces[] = {2, 7, 12};
        return workspaces[(offset % 3 + 3) % 3];
    };
    check(Grid::workspaceIDs(9, false, invalid, wrapping) == std::vector<int64_t>({2, 7, 12, invalid, invalid, invalid, invalid, invalid, invalid}),
          "skip-empty first must leave unused tiles invalid");
    check(Grid::workspaceIDs(9, true, invalid, wrapping) == std::vector<int64_t>({2, 7, 12, invalid, invalid, invalid, invalid, invalid, invalid}),
          "skip-empty center must stop on wrap");
    auto nearStart = [](int offset) -> int64_t { return offset < -1 ? 20 + offset : 2 + offset; };
    check(Grid::workspaceIDs(9, true, invalid, nearStart) == std::vector<int64_t>({1, 2, 3, 4, 5, 6, 7, 8, 9}),
          "centering near the first workspace must stop backtracking on wrap");
    auto failure = [=](int) { return invalid; };
    check(Grid::workspaceIDs(4, true, invalid, failure) == std::vector<int64_t>(4, invalid), "invalid selectors must leave safe placeholder tiles");
    auto address      = [](int offset) { return std::to_string(9 + offset); };
    auto numericOrder = [](const std::string& a, const std::string& b) { return std::stoi(a) < std::stoi(b); };
    check(Grid::workspaceIDs(4, false, std::string{}, address, numericOrder) == std::vector<std::string>({"9", "10", "11", "12"}),
          "workspace addresses must retain numeric ordering across digit boundaries");
    check(Grid::workspaceIDs(3, true, std::string{}, address, numericOrder) == std::vector<std::string>({"8", "9", "10"}), "centering must use the workspace address comparator");
    std::cout << "Grid regression checks passed\n";
}
