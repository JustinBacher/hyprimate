#include <memory>

#include "Check.hpp"
#include "workspace/WorkspaceHookLifetime.hpp"

using hyprimate::workspace::WorkspaceHookLifetime;

namespace {
    struct Owner : std::enable_shared_from_this<Owner> {
        WorkspaceHookLifetime m_hook_lifetime;
        int removals = 0;
        int original_calls = 0;
        bool original_valid = true;
        bool reenter_shutdown = false;

        void shutdown() {
            m_hook_lifetime.request<&Owner::remove>(*this);
        }
        void remove() {
            CHECK(m_hook_lifetime.busy());
            ++removals;
            original_valid = false;
            if (reenter_shutdown) {
                shutdown();
            }
        }
        void original() {
            CHECK(original_valid);
            ++original_calls;
        }
    };

    void shutdown_inside_callback() {
        for (int path = 0; path < 2; ++path) {
            auto owner = std::make_shared<Owner>();
            {
                const WorkspaceHookLifetime::Scope scope{owner.get()};
                auto native_callback = [&] {
                    owner->shutdown();
                    owner->shutdown();
                    CHECK(owner->removals == 0);
                    owner->original();
                };
                // Queue-like path calls fallback after the native callback;
                // mutate-like path calls the original within that callback.
                if (path == 0) {
                    owner->shutdown();
                    CHECK(owner->removals == 0);
                    owner->original();
                } else {
                    native_callback();
                }
                CHECK(owner->removals == 0);
            }
            CHECK(owner->original_calls == 1);
            CHECK(owner->removals == 1);
            CHECK(!owner->original_valid);
            owner->shutdown();
            CHECK(owner->removals == 1);
        }
    }

    void nested_callbacks_and_owner_pin() {
        auto owner = std::make_shared<Owner>();
        std::weak_ptr<Owner> weak = owner;
        {
            const WorkspaceHookLifetime::Scope outer{owner.get()};
            {
                const WorkspaceHookLifetime::Scope inner{owner.get()};
                owner->shutdown();
                CHECK(owner->removals == 0);
            }
            CHECK(owner->removals == 0);
            owner->original();
            owner.reset();
            CHECK(!weak.expired());
        }
        CHECK(weak.expired());
    }

    void immediate_and_reentrant_shutdown() {
        auto owner = std::make_shared<Owner>();
        owner->reenter_shutdown = true;
        owner->shutdown();
        CHECK(owner->removals == 1);
        owner->shutdown();
        CHECK(owner->removals == 1);
        owner->m_hook_lifetime.reset();
        owner->original_valid = true;
        owner->shutdown();
        CHECK(owner->removals == 2);
    }
}  // namespace

int main() {
    shutdown_inside_callback();
    nested_callbacks_and_owner_pin();
    immediate_and_reentrant_shutdown();
    return g_failures == 0 ? 0 : 1;
}
