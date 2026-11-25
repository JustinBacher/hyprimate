#include "GlesState.hpp"

namespace hyprimate::shader::gles_detail {
    GLint integer(GLenum name) {
        GLint value = 0;
        glGetIntegerv(name, &value);
        return value;
    }

    bool fail(std::string &error, std::string_view message) {
        error = message;
        return false;
    }

    // GLES cannot reinsert errors into the caller's error flags. The API
    // requires initially clean flags; consume runtime errors before returning.
    void discard_errors() {
        for (unsigned i = 0; i < 8 && glGetError() != GL_NO_ERROR; ++i) {
        }
    }

    bool gl_ok(std::string &error, std::string_view operation) {
        const auto result = glGetError();
        if (result == GL_NO_ERROR) {
            return true;
        }
        discard_errors();
        error = operation;
        error += ": GLES error ";
        error += std::to_string(result);
        return false;
    }

    void enable(GLenum cap, bool enabled) {
        if (enabled) {
            glEnable(cap);
        } else {
            glDisable(cap);
        }
    }

    void count_storage(Statistics &stats, std::size_t bytes) {
        ++stats.allocations;
        stats.allocated_bytes += bytes;
    }

    bool can_use_program(GLuint current, std::string &error) {
        if (integer(GL_TRANSFORM_FEEDBACK_ACTIVE) && !integer(GL_TRANSFORM_FEEDBACK_PAUSED)) {
            return fail(error, "caller transform feedback is active and unpaused");
        }
        // Switching away from a delete-pending current program destroys its name.
        if (current != 0) {
            GLint deleted = GL_FALSE;
            glGetProgramiv(current, GL_DELETE_STATUS, &deleted);
            if (deleted != GL_FALSE) {
                return fail(error, "caller program is pending deletion");
            }
            GLint linked = GL_FALSE;
            glGetProgramiv(current, GL_LINK_STATUS, &linked);
            if (linked == GL_FALSE) {
                return fail(error, "caller program cannot be restored after a failed relink");
            }
        }
        return true;
    }
}  // namespace hyprimate::shader::gles_detail
