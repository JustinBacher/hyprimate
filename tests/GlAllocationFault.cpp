#include <GLES3/gl3.h>

#include "GlAllocationFault.hpp"

namespace gl_harness {
    thread_local FailAllocation *FailAllocation::s_active = nullptr;

    FailAllocation::FailAllocation(Allocation kind, unsigned occurrence)
        : m_previous(s_active), m_kind(kind), m_remaining(occurrence) {
        s_active = this;
    }

    FailAllocation::~FailAllocation() {
        s_active = m_previous;
    }

    bool FailAllocation::intercept(Allocation kind) {
        if (s_active == nullptr || s_active->m_kind != kind || s_active->m_remaining == 0) {
            return false;
        }
        return --s_active->m_remaining == 0;
    }
}  // namespace gl_harness

using gl_harness::Allocation;
using gl_harness::FailAllocation;

extern "C" {
void __real_glTexStorage2D(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
void __wrap_glTexStorage2D(GLenum target, GLsizei levels, GLenum format, GLsizei w, GLsizei h) {
    if (!FailAllocation::intercept(Allocation::texture_2d))
        __real_glTexStorage2D(target, levels, format, w, h);
}
void __real_glTexStorage3D(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei);
void __wrap_glTexStorage3D(
    GLenum target, GLsizei levels, GLenum format, GLsizei w, GLsizei h, GLsizei d) {
    if (!FailAllocation::intercept(Allocation::texture_3d))
        __real_glTexStorage3D(target, levels, format, w, h, d);
}
void __real_glRenderbufferStorage(GLenum, GLenum, GLsizei, GLsizei);
void __wrap_glRenderbufferStorage(GLenum target, GLenum format, GLsizei w, GLsizei h) {
    if (!FailAllocation::intercept(Allocation::renderbuffer))
        __real_glRenderbufferStorage(target, format, w, h);
}
void __real_glBufferData(GLenum, GLsizeiptr, const void *, GLenum);
void __wrap_glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage) {
    if (!FailAllocation::intercept(Allocation::buffer))
        __real_glBufferData(target, size, data, usage);
}
}  // extern "C"
