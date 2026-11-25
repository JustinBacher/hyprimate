#pragma once

#include <string>

namespace hyprimate::gl {
    // Owns one main-executable GOT relocation. Callers must serialize installation, calls through
    // the slot, and destruction (Hyprland renders and unloads plugins on its event-loop thread).
    class GotRedirect {
      public:
        using Pin = bool (*)(void *) noexcept;
        explicit GotRedirect(Pin pin = nullptr) noexcept : m_pin(pin) {}
        ~GotRedirect();
        GotRedirect(const GotRedirect &) = delete;
        GotRedirect &operator=(const GotRedirect &) = delete;
        [[nodiscard]] bool install(const char *symbol, void *hook, std::string &error,
            void *expected = nullptr);
        void reset() noexcept;
#ifdef HYPRIMATE_GOT_REDIRECT_TESTING
        [[nodiscard]] void **slot_for_testing() const noexcept { return m_slot; }
#endif

      private:
        void **m_slot = nullptr;
        void *m_original = nullptr;
        int m_protection = 0;
        void *m_hook = nullptr;
        void *m_hold = nullptr;
        Pin m_pin = nullptr;
    };
}
