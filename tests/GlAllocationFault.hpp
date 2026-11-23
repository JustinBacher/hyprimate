#pragma once

namespace gl_harness {
    enum class Allocation { texture_2d, texture_3d, renderbuffer, buffer };

    // Simulate a driver leaving an allocation unchanged. Only this test thread and the
    // selected call are affected; all other GLES operations reach the real driver.
    class FailAllocation {
      public:
        explicit FailAllocation(Allocation kind, unsigned occurrence = 1);
        ~FailAllocation();
        FailAllocation(const FailAllocation &) = delete;
        FailAllocation &operator=(const FailAllocation &) = delete;
        FailAllocation(FailAllocation &&) = delete;
        FailAllocation &operator=(FailAllocation &&) = delete;

        static bool intercept(Allocation kind);

      private:
        static thread_local FailAllocation *s_active;
        FailAllocation *m_previous;
        Allocation m_kind;
        unsigned m_remaining;
    };
}  // namespace gl_harness
