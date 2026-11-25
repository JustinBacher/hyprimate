#pragma once

// Benchmark-only EGL device selection; deliberately separate from GlHarness.
#include <cstdio>
#include <string_view>
#include <vector>
#include <EGL/egl.h>

using EGLDeviceEXT = void*;


#include <GLES3/gl3.h>

namespace hyprimate::compute::fixture {
class DrmEglContext {
    using QueryDevices = EGLBoolean (*)(EGLint, EGLDeviceEXT*, EGLint*);
    using QueryDeviceString = const char* (*)(EGLDeviceEXT, EGLint);
    using GetPlatformDisplay = EGLDisplay (*)(EGLenum, void*, const EGLint*);
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLContext context_ = EGL_NO_CONTEXT;
    bool valid_ = false;
public:
    explicit DrmEglContext(std::string_view requested, int width = 64, int height = 64) {
        auto query_devices = reinterpret_cast<QueryDevices>(eglGetProcAddress("eglQueryDevicesEXT"));
        auto query_string = reinterpret_cast<QueryDeviceString>(eglGetProcAddress("eglQueryDeviceStringEXT"));
        auto get_platform = reinterpret_cast<GetPlatformDisplay>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
        if (!query_devices || !query_string || !get_platform) {
            std::fprintf(stderr, "Exact --render-node selection unavailable: EGL_EXT_device_enumeration/platform_device required\n"); return;
        }
        EGLint count = 0;
        if (!query_devices(0, nullptr, &count) || count <= 0) { std::fprintf(stderr, "EGL reports no devices\n"); return; }
        std::vector<EGLDeviceEXT> devices(static_cast<std::size_t>(count));
        if (!query_devices(count, devices.data(), &count)) { std::fprintf(stderr, "EGL device enumeration failed\n"); return; }
        constexpr EGLint RENDER_NODE = 0x3377;
        for (int i = 0; i < count; ++i) {
            const char* node = query_string(devices[static_cast<std::size_t>(i)], RENDER_NODE);
            if (node && requested == node) { display_ = get_platform(0x313F, devices[static_cast<std::size_t>(i)], nullptr); break; }
        }
        if (display_ == EGL_NO_DISPLAY) { std::fprintf(stderr, "No EGL device matches requested render node: %.*s\n", int(requested.size()), requested.data()); return; }
        EGLint major=0, minor=0;
        if (!eglInitialize(display_, &major, &minor) || !eglBindAPI(EGL_OPENGL_ES_API)) { std::fprintf(stderr, "Could not initialize EGL on requested device\n"); return; }
        constexpr EGLint attrs[] = {EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
        EGLConfig config{}; EGLint configs=0;
        if (!eglChooseConfig(display_, attrs, &config, 1, &configs) || configs != 1) { std::fprintf(stderr, "Requested EGL device lacks GLES3 pbuffer config\n"); return; }
        constexpr EGLint ctxattrs[] = {EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
        const EGLint surfattrs[] = {EGL_WIDTH,width,EGL_HEIGHT,height,EGL_NONE};
        surface_=eglCreatePbufferSurface(display_,config,surfattrs); context_=eglCreateContext(display_,config,EGL_NO_CONTEXT,ctxattrs);
        valid_=surface_!=EGL_NO_SURFACE && context_!=EGL_NO_CONTEXT && eglMakeCurrent(display_,surface_,surface_,context_);
    }
    ~DrmEglContext() { if(display_!=EGL_NO_DISPLAY){eglMakeCurrent(display_,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);if(context_!=EGL_NO_CONTEXT)eglDestroyContext(display_,context_);if(surface_!=EGL_NO_SURFACE)eglDestroySurface(display_,surface_);eglTerminate(display_);} }
    DrmEglContext(const DrmEglContext&)=delete; DrmEglContext& operator=(const DrmEglContext&)=delete;
    [[nodiscard]] bool valid() const noexcept{return valid_;}
};
}
