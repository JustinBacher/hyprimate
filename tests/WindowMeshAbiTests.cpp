#include <concepts>
#include <type_traits>
#include <vector>

#include <hyprland/src/helpers/DeformableMesh.hpp>

#include "WindowAnimationFacts.hpp"

using namespace hyprimate::animation;

namespace {
template<Vertex V> Mesh<V> fill_mesh(const MeshTarget<V> &target) {
    target.buffers->window.push_back(V{.x = 1, .y = 2, .u = 3, .v = 4});
    Mesh<V> result{};
    result.window = &target.buffers->window;
    return result;
}

static_assert(Vertex<MeshVertex>);
static_assert(Vertex<SMeshRenderVertex>);
static_assert(std::same_as<decltype(Mesh<SMeshRenderVertex>{}.window),
    const std::vector<SMeshRenderVertex> *>);
template Mesh<MeshVertex> fill_mesh(const MeshTarget<MeshVertex> &);
template Mesh<SMeshRenderVertex> fill_mesh(const MeshTarget<SMeshRenderVertex> &);
} // namespace
