#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

namespace Grid {
    // Each tile owns a monitor-sized framebuffer. Bound configuration before allocating.
    constexpr int MAX_COLUMNS = 10;

    inline int    columns(int64_t value) {
        return static_cast<int>(std::clamp<int64_t>(value, 1, MAX_COLUMNS));
    }

    inline int tileAt(double x, double y, int columns) {
        if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x >= columns || y >= columns)
            return -1;
        return static_cast<int>(std::floor(x)) + static_cast<int>(std::floor(y)) * columns;
    }

    inline int validTile(int id, int fallback, int count) {
        return id >= 0 && id < count ? id : std::clamp(fallback, 0, count - 1);
    }

    inline int tileAtPosition(double x, double y, double originX, double originY, double monitorScale, double tileWidth, double tileHeight, double gap, int columns) {
        if (monitorScale <= 0 || tileWidth <= 0 || tileHeight <= 0)
            return -1;
        const auto localX = x - originX / monitorScale;
        const auto localY = y - originY / monitorScale;
        const auto id     = tileAt(localX / (tileWidth + gap), localY / (tileHeight + gap), columns);
        if (id < 0 || localX - (id % columns) * (tileWidth + gap) >= tileWidth || localY - (id / columns) * (tileHeight + gap) >= tileHeight)
            return -1;
        return id;
    }

    // resolve(offset) must resolve relative to the configured starting workspace.
    template <typename ID, typename Resolver, typename Less = std::less<ID>>
    std::vector<ID> workspaceIDs(int count, bool center, ID invalid, Resolver resolve, Less less = {}) {
        std::vector<ID> ids(count, invalid);
        int             backtracked = 0;
        ID              first       = resolve(0);
        if (first == invalid)
            return ids;

        if (center) {
            for (int offset = 1; offset <= count / 2; ++offset) {
                const auto id = resolve(-offset);
                if (id == invalid || !less(id, first))
                    break;
                first = id;
                ++backtracked;
            }
        }

        for (int i = 0; i < count; ++i) {
            const auto id = resolve(i - backtracked);
            if (id == invalid || (i > 0 && !less(ids[i - 1], id)))
                break;
            ids[i] = id;
        }
        return ids;
    }
}
