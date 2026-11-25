#include <cerrno>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "Check.hpp"
#include "workspace/planet_hop/PlanetGlRenderer.hpp"

namespace {
    std::string source_path;
    std::string fifo_path;
    bool hook_fired = false;
    bool open_called = false;
    bool swapped = false;

    void swap_once() {
        if (!hook_fired) {
            hook_fired = true;
            swapped = std::rename(fifo_path.c_str(), source_path.c_str()) == 0;
        }
    }
}  // namespace

extern "C" int __real_open(const char *path, int flags, ...);
extern "C" int __wrap_open(const char *path, int flags, ...) {
    if (source_path == path) {
        open_called = true;
        swap_once();
    }
    if ((flags & O_CREAT) != 0 || (flags & O_TMPFILE) == O_TMPFILE) {
        va_list args;
        va_start(args, flags);
        const auto mode = va_arg(args, int);
        va_end(args);
        return __real_open(path, flags, mode);
    }
    return __real_open(path, flags);
}

// The pre-fix ifstream implementation never calls the executable's open symbol.
// This legacy hook reproduces its race after file_size; the open hook above is
// required for the current descriptor-based implementation and future changes.
extern "C" std::uintmax_t real_file_size(const std::filesystem::path &, std::error_code &) noexcept
    asm("__real__ZNSt10filesystem9file_sizeERKNS_7__cxx114pathERSt10error_code");
extern "C" std::uintmax_t wrap_file_size(
    const std::filesystem::path &path, std::error_code &error) noexcept
    asm("__wrap__ZNSt10filesystem9file_sizeERKNS_7__cxx114pathERSt10error_code");
std::uintmax_t wrap_file_size(const std::filesystem::path &path, std::error_code &error) noexcept {
    const auto SIZE = real_file_size(path, error);
    if (!error && path == source_path) {
        swap_once();
    }
    return SIZE;
}

int main() {
    std::string directory =
        (std::filesystem::temp_directory_path() / "planet-depth-XXXXXX").string();
    CHECK(mkdtemp(directory.data()) != nullptr);
    if (g_failures != 0) {
        return 1;
    }
    source_path = directory + "/source.png";
    fifo_path = directory + "/replacement";
    {
        std::ofstream source(source_path);
        source << "nonempty";
        CHECK(source.good());
    }
    CHECK(mkfifo(fifo_path.c_str(), 0600) == 0);
    if (g_failures == 0) {
        // This executable is CPU-only: no EGL context or driver threads exist at fork.
        const pid_t child = fork();
        CHECK(child >= 0);
        if (child == 0) {
            hyprimate::workspace::PlanetGlRenderer renderer;
            const bool prepared = renderer.prepare_depth(source_path);
            _exit(open_called && hook_fired && swapped && !prepared ? 0 : 1);
        }
        if (child > 0) {
            int status = 0;
            pid_t result = 0;
            for (int i = 0; i < 200 && result == 0; ++i) {
                do {
                    result = waitpid(child, &status, WNOHANG);
                } while (result < 0 && errno == EINTR);
                if (result == 0) {
                    usleep(10000);
                }
            }
            const bool finished = result == child;
            if (!finished) {
                kill(child, SIGKILL);
                do {
                    result = waitpid(child, &status, 0);
                } while (result < 0 && errno == EINTR);
            }
            CHECK(finished && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }
    std::filesystem::remove_all(directory);
    return g_failures == 0 ? 0 : 1;
}
