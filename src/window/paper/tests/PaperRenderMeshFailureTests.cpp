#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <vector>

#include "Check.hpp"
#include "window/paper/PaperRenderMesh.hpp"

namespace {
    // This executable contains no renderer or worker threads. Arm the replacement
    // allocator only around the CPU build and only for the enlarged heads vector.
    thread_local std::size_t fail_size = 0;
    thread_local bool fault_observed = false;

    class FailAllocation {
      public:
        explicit FailAllocation(std::size_t size) {
            fault_observed = false;
            fail_size = size;
        }
        ~FailAllocation() {
            fail_size = 0;
        }
        FailAllocation(const FailAllocation &) = delete;
        FailAllocation &operator=(const FailAllocation &) = delete;
    };

    bool equal(const SPaperVertex &a, const SPaperVertex &b) {
        return a.x == b.x && a.y == b.y && a.z == b.z && a.u == b.u && a.v == b.v &&
               a.nx == b.nx && a.ny == b.ny && a.nz == b.nz;
    }
}  // namespace

void *operator new(std::size_t size) {
    if (fail_size != 0 && size == fail_size) {
        fail_size = 0;
        fault_observed = true;
        throw std::bad_alloc();
    }
    if (void *memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}

void operator delete(void *memory) noexcept {
    std::free(memory);
}

void operator delete(void *memory, std::size_t) noexcept {
    ::operator delete(memory);
}

int main() {
    hyprimate::PaperSurface surface;
    surface.points = {{{0, 0, 0}, {0, 0}},
        {{1, 0, 0}, {1, 0}},
        {{1, 1, 0}, {1, 1}},
        {{0, 1, .4}, {0, 1}}};
    surface.faces = {{{0, 1, 2}, {}}, {{0, 2, 3}, {}}};
    CPaperRenderMesh mesh;
    if (!mesh.build(surface)) return 1;
    const auto initial = mesh.vertices();
    const std::vector<SPaperVertex> expected(initial.begin(), initial.end());
    if (expected.size() != 24) return 1;
    // The shared corner receives contributions from both non-coplanar faces.
    CHECK(expected[0].nx == expected[12].nx);
    CHECK(expected[0].ny == expected[12].ny);
    CHECK(expected[0].nz == expected[12].nz);
    CHECK(expected[0].nx != 0 || expected[0].ny != 0);

    auto larger = surface;
    constexpr std::size_t POINT_COUNT = 4096;
    larger.points.resize(POINT_COUNT);
    bool built = true;
    {
        // Same face count reuses m_neighbors, resetting it before m_heads grows.
        // Fail that growth, then revisit the original cache key on this mesh.
        FailAllocation fail(POINT_COUNT * sizeof(std::size_t));
        built = mesh.build(larger);
    }
    CHECK(fault_observed);
    CHECK(!built);
    if (!fault_observed || built || !mesh.build(surface)) return 1;
    const auto recovered = mesh.vertices();
    CHECK(recovered.size() == expected.size());
    if (recovered.size() == expected.size())
        CHECK(std::equal(recovered.begin(), recovered.end(), expected.begin(), equal));
    CHECK(mesh.build(larger));
    CHECK(mesh.build(surface));
    return g_failures == 0 ? 0 : 1;
}
