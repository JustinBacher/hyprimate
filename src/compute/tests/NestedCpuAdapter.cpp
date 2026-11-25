#include "NestedCpuAdapter.hpp"
#include "shader/Package.hpp"

#include <algorithm>
#include <cstdlib>

namespace hyprimate::compute::fixture {
    bool cpu_requested() noexcept {
        const auto* requested = std::getenv("HYPRIMATE_COMPUTE_BENCH_CPU");
        return requested && std::string_view(requested) == "1";
    }

    Result<Workload> match_workload(const shader::Package& package) {
        const auto check = [&](std::string_view name) {
            const auto expected = shader::load_package(std::string(HYPRIMATE_SHADER_EXAMPLES) + "/" + std::string(name) + "/effect.json");
            if (!expected || package.parameters != expected->parameters || package.passes != expected->passes ||
                package.fields.size() != expected->fields.size() || !package.textures.empty() ||
                !package.buffers.empty() || package.environment.used()) return false;
            // Timing fixtures vary the element count, not the kernel/render ABI or code.
            return std::ranges::equal(package.fields, expected->fields, [](const auto& a, const auto& b) {
                return a.name == b.name && a.type == b.type && a.ping_pong == b.ping_pong && a.lifetime == b.lifetime &&
                    a.domain.has_value() && !a.scale && a.domain->z == 1 && a.domain->x > 0 && a.domain->y > 0;
            });
        };
        if (check("compute-stencil")) return Workload::Stencil;
        if (check("compute-particles") && package.fields[0].domain == package.fields[1].domain &&
            package.fields[0].domain->y == 1 && package.meshes.size() == 1 &&
            package.meshes[0].instances == package.fields[0].domain->x) return Workload::Particles;
        return std::unexpected("benchmark CPU adapter only accepts the checked-in stencil and particle fixtures");
    }


    NestedCpuAdapter::NestedCpuAdapter(Workload workload, Domain domain) : workload_(workload), domain_(domain) {
        if (workload == Workload::Stencil) {
            const auto elements = std::size_t{domain.x} * domain.y;
            for (auto& side : energy_) side.resize(elements);
        } else {
            for (auto& side : positions_) side.resize(domain.x);
            for (auto& side : velocities_) side.resize(domain.x);
        }
    }
    void NestedCpuAdapter::run(bool initialize, float seed_x, float parameter, float delta, FieldsOut outputs) {
        const unsigned write = 1 - read_;
        if (workload_ == Workload::Stencil) {
            if (initialize) seed_stencil(energy_[write], domain_.x, domain_.y);
            else step_stencil(energy_[read_], energy_[write], domain_.x, domain_.y, parameter);
            dirty_[0] = outputs[0];
        } else {
            if (initialize) seed_particles(positions_[write], velocities_[write], seed_x);
            else step_particles(positions_[read_], velocities_[read_], positions_[write], velocities_[write], parameter, delta);
            dirty_[0] = outputs[0];
            dirty_[1] = outputs[1];
        }
        read_ = write;
        ++iterations_;
    }
    Result<void> NestedCpuAdapter::publish(Device& device) {
        const auto upload = [&](unsigned slot, std::span<const std::byte> data) -> Result<void> {
            if (!dirty_[slot]) return {};
            auto result = device.upload(*dirty_[slot], data);
            if (result) dirty_[slot] = nullptr;
            return result;
        };
        if (workload_ == Workload::Stencil) return upload(0, std::as_bytes(std::span(energy_[read_])));
        if (auto positions = upload(0, std::as_bytes(std::span(positions_[read_]))); !positions) return positions;
        return upload(1, std::as_bytes(std::span(velocities_[read_])));
    }
}
