#pragma once

#include "FixtureOracle.hpp"
#include "compute/Compute.hpp"

namespace hyprimate::shader { struct Package; }

namespace hyprimate::compute::fixture {
    enum class Workload { Stencil, Particles };
    [[nodiscard]] bool cpu_requested() noexcept;
    [[nodiscard]] Result<Workload> match_workload(const shader::Package&);

    class NestedCpuAdapter {
    public:
        NestedCpuAdapter(Workload workload, Domain domain);
        void run(bool initialize, float seed_x, float parameter, float delta, FieldsOut outputs);
        [[nodiscard]] Result<void> publish(Device&);
        [[nodiscard]] std::uint64_t iterations() const noexcept { return iterations_; }
    private:
        Workload workload_;
        Domain domain_;
        unsigned read_{};
        std::uint64_t iterations_{};
        std::array<std::vector<float>, 2> energy_;
        std::array<std::vector<Vec2>, 2> positions_, velocities_;
        std::array<Field*, 2> dirty_{};
    };
}
