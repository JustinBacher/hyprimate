#include "workspace/WorkspaceStyleList.hpp"

namespace {

    // Invalid styles must make coverage false without instantiating KIND/Resources.
    struct IncompleteWorkspaceStyle {};
    static_assert(!hyprimate::workspace::WorkspaceStyleCoverage<
        hyprimate::animation::StyleList<IncompleteWorkspaceStyle>>::valid());
}  // namespace
