#include <cmath>
#include <limits>

#include "Check.hpp"
#include "animations/shared/Core.hpp"
#include "workspace/WorkspaceCapturePolicy.hpp"
#include "workspace/WorkspaceController.hpp"

using hyprimate::workspace::Direction;
using hyprimate::workspace::WorkspaceController;

int main() {
    using Style = WorkspaceController::Style;
    using Start = WorkspaceController::Start;
    using Advance = WorkspaceController::Advance;
    const auto finish = [](WorkspaceController &controller, WorkspaceController::Monitor monitor) {
        auto result = Advance::None;
        for (int i = 0; i < 20 && result == Advance::None; ++i)
            result = controller.advance(monitor, 0.1);
        return result;
    };
    using hyprimate::workspace::CaptureKind;
    CHECK(hyprimate::workspace::blocks_surface_feedback(CaptureKind::SourceIsolated));
    CHECK(!hyprimate::workspace::blocks_surface_feedback(CaptureKind::DestinationLive));
    WorkspaceController controller;
    const auto quadratic = [](double t) { return t * t; };
    CHECK(controller.begin(9, 1, 2, Direction::Forward, Style::Rotation, 1.0, quadratic) == Start::Capture);
    controller.presented(9);
    controller.advance(9, 0.25);
    CHECK(std::abs(controller.state(9)->transition.progress() - 0.0625) < 1e-6);
    CHECK(controller.begin(9, 2, 3, Direction::Forward, Style::Rotation, 1.0, quadratic) == Start::Pending);
    CHECK(finish(controller, 9) == Advance::NextDestination);
    controller.presented(9);
    controller.advance(9, 0.25);
    CHECK(std::abs(controller.state(9)->transition.progress() - 0.0625) < 1e-6);
    controller.cancel(9);
    CHECK(WorkspaceController::selectStyle(false, "rotation") == Style::None);
    CHECK(WorkspaceController::selectStyle(true, "unknown") == Style::None);
    CHECK(WorkspaceController::selectStyle(true, "rotation") == Style::Rotation);
    CHECK(controller.begin(0, 1, 2, Direction::Forward, Style::Rotation, 0.5) == Start::Native);
    CHECK(controller.begin(1, 1, 1, Direction::Forward, Style::Rotation, 0.5) == Start::Native);
    CHECK(controller.begin(1, 1, 2, Direction::Forward, Style::Rotation, 0.5) == Start::Capture);
    CHECK(controller.begin(1, 2, 3, Direction::Forward, Style::Rotation, 0.5) == Start::RetainSource);
    CHECK(controller.state(1)->source == 1);
    CHECK(controller.state(1)->destination == 3);
    CHECK(controller.begin(1, 3, 1, Direction::Backward, Style::Rotation, 0.5) == Start::Native);
    CHECK(controller.state(1) == nullptr);

    CHECK(controller.begin(1, 1, 2, Direction::Forward, Style::Rotation, 0.5) == Start::Capture);
    CHECK(controller.begin(2, 8, 9, Direction::Backward, Style::Rotation, 0.5) == Start::Capture);
    controller.presented(1);
    controller.advance(1, 0.2);
    const double position = controller.state(1)->transition.progress();
    const double velocity = controller.state(1)->transition.velocity();
    CHECK(controller.begin(1, 2, 1, Direction::Backward, Style::Rotation, 0.5) == Start::Retarget);
    CHECK(controller.state(1)->transition.progress() == position);
    CHECK(controller.state(1)->transition.velocity() == velocity);
    CHECK(controller.state(2)->transition.progress() == 0.0);
    CHECK(controller.begin(1, 1, 3, Direction::Forward, Style::Rotation, 0.5) == Start::Pending);
    CHECK(controller.begin(1, 3, 4, Direction::Forward, Style::Rotation, 0.5) == Start::Pending);
    CHECK(*controller.state(1)->pending == 4);
    CHECK(controller.state(1)->source == 1 && controller.state(1)->destination == 2);
    CHECK(controller.begin(1, 4, 2, Direction::Forward, Style::Rotation, 0.5) == Start::Retarget);
    CHECK(!controller.state(1)->pending);
    CHECK(controller.begin(1, 2, 5, Direction::Forward, Style::Rotation, 0.5) == Start::Pending);
    CHECK(controller.begin(1, 5, 6, Direction::Forward, Style::Rotation, 0.5) == Start::Pending);
    CHECK(finish(controller, 1) == Advance::NextDestination);
    CHECK(controller.state(1)->source == 2 && controller.state(1)->destination == 6);
    CHECK(!controller.state(1)->pending);
    controller.presented(1);
    CHECK(finish(controller, 1) == Advance::Finished);
    CHECK(controller.state(1) == nullptr);
    CHECK(controller.state(2) != nullptr);

    CHECK(controller.begin(1, 1, 2, Direction::Forward, Style::Rotation, 0.5) == Start::Capture);
    controller.presented(1);
    controller.advance(1, 0.1);
    controller.begin(1, 2, 1, Direction::Backward, Style::Rotation, 0.5);
    controller.begin(1, 1, 3, Direction::Forward, Style::Rotation, 0.5);
    CHECK(finish(controller, 1) == Advance::NextSource);
    CHECK(controller.state(1)->source == 1 && controller.state(1)->destination == 3);
    controller.presented(1);
    CHECK(finish(controller, 1) == Advance::Finished);
    CHECK(controller.begin(1, 1, 2, Direction::Forward, Style::Rotation, 0.5) == Start::Capture);
    controller.presented(1);
    controller.begin(1, 2, 3, Direction::Forward, Style::Rotation, 0.5);
    CHECK(controller.advance(1, hyprimate::STALL_SECONDS + 0.01) == Advance::Finished);
    CHECK(controller.state(1) == nullptr);
    controller.cancel(2);
    CHECK(controller.state(2) == nullptr);
    CHECK(controller.begin(1, 1, 2, Direction::Forward, Style::None, 0.5) == Start::Native);
    CHECK(controller.begin(1, 1, 2, Direction::Forward, Style::Rotation,
              std::numeric_limits<double>::quiet_NaN()) == Start::Native);
    return g_failures == 0 ? 0 : 1;
}
