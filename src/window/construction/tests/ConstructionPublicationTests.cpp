#include "Check.hpp"
#include "window/construction/ConstructionPublication.hpp"

using namespace hyprimate;

int main() {
    constexpr Box capture{60, 20, 200, 100};
    constexpr ConstructionQuad pane{{12, 30, 5}, {80, -8, 16}, {4, 40, -4}, {20, 10, 80, 40},
        {255, 255, 255, 255}, ConstructionSurface::Client};
    ConstructionBatch batch;
    batch.add(pane);
    auto worker = pane;
    worker.surface = ConstructionSurface::Atlas;
    batch.add(worker);
    construction::Publication receipt;
    // Only the center and lower half of the tilted print were sampled in the last live draw.
    receipt.remember(batch, capture, capture, {40, 30, 40, 20});
    CHECK(receipt.known && receipt.count == 1);
    const auto &face = receipt.faces[0];
    CHECK((face.uv == std::array{40.F, 30.F, 40.F, 20.F}));
    CHECK((face.origin == std::array{34.F, 48.F, 7.F}));
    CHECK((face.u == std::array{40.F, -4.F, 8.F}));
    CHECK((face.v == std::array{2.F, 20.F, -2.F}));
    // A successful empty image and an unrepresentable publication have different handoffs.
    receipt.remember(batch, capture, capture, {110, 20, 42, 71});
    CHECK(receipt.known && receipt.count == 0);
    for (std::size_t i = 0; i < receipt.faces.size(); ++i) batch.add(pane);
    receipt.remember(batch, capture, capture, {0, 0, 200, 100});
    CHECK(!receipt.known);
    return g_failures == 0 ? 0 : 1;
}
