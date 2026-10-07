// =============================================================================
//  parallel.h  -  Split per-column raster work across CPU cores
// =============================================================================
#pragma once
#include <QtConcurrent/QtConcurrent>
#include <vector>
#include <numeric>

// Runs f(x) for x in [0, n). Columns are handed out in small chunks so a thread
// that gets cheap sky-only columns picks up more work.
template <class F>
void parallelColumns(int n, F &&f, int chunk = 16)
{
    int chunks = (n + chunk - 1) / chunk;
    std::vector<int> ids(chunks);
    std::iota(ids.begin(), ids.end(), 0);
    QtConcurrent::blockingMap(ids, [&](int c) {
        int x1 = std::min(n, (c + 1) * chunk);
        for (int x = c * chunk; x < x1; x++) f(x);
    });
}
