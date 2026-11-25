#pragma once

namespace hyprimate::gl {
    // GL_TRIANGLES is 0x0004; kept independent of GL so the dispatch can be tested on the CPU.
    struct IndexedDrawContract {
        enum class Decision { Forward, Elements, Suppress };
        bool active = false;
        bool broken = false;
        bool consumed = false;
        int vertex_count = 0;

        [[nodiscard]] Decision dispatch(unsigned mode, int first, int count) noexcept {
            if (!active) return Decision::Forward;
            if (broken || mode != 0x0004U || first != 0 || count != vertex_count) {
                broken = true;
                return Decision::Suppress;
            }
            return Decision::Elements;
        }
    };
}
