// Preview sampler. Compile against the actual controller, not a lookalike easing function.
#include "workspace/WorkspaceDirection.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

using namespace hyprimate::workspace;

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    const double duration = std::atof(argv[1]);
    if (!(duration > 0 && duration <= 10)) return 1;
    // Every shot starts on the previous shot's endpoint, including between speeds.
    for (const int slow : {1, 4}) {
        for (int shot = 0; shot != 3; ++shot) {
            const auto direction = direction_between(shot == 1 ? 3 : 2, shot == 1 ? 2 : 3, false, false);
            Transition path;
            path.begin(direction, duration);
            const int request = std::lround(duration * 60 * slow / 2);
            for (int frame = 0;; ++frame) {
                std::printf("%d %d %d %.17g\n", slow, shot,
                    path.direction() == Direction::Backward, path.progress());
                if (!path.active()) break;
                // The presented pose and velocity, on the ORIGINAL leg. No capture swap.
                if (shot == 2 && frame == request) path.retarget(false, duration, path.motion());
                const double hz = 60.0 * slow;
                path.advance((frame + 1) / hz - frame / hz);
            }
        }
    }
}
