// ProsperoStore - Host renderer for inspecting the same screens as the console.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/store.hpp"
#include "catalog/client.hpp"
#include "gfx/gl_program.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <cstdio>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#include "../third_party/stb/stb_image_write.h"
#pragma clang diagnostic pop

int main(int argc, char **argv)
{
    if (argc != 3 && argc != 4)
        return 2;
    const auto get_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    EGLDisplay display =
        get_display ? get_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr)
                    : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0;
    EGLint minor = 0;
    if (!eglInitialize(display, &major, &minor) || !eglBindAPI(EGL_OPENGL_API))
        return 3;
    const EGLint attributes[] = {EGL_CONTEXT_MAJOR_VERSION,
                                 4,
                                 EGL_CONTEXT_MINOR_VERSION,
                                 5,
                                 EGL_CONTEXT_OPENGL_PROFILE_MASK,
                                 EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                                 EGL_NONE};
    EGLContext context = eglCreateContext(display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attributes);
    if (context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context))
        return 4;
    {
        hui::gfx::set_glsl_prefix("#version 450 core\n");
        hui::gfx::Renderer renderer;
        store::Fonts fonts;
        hui::gfx::Canvas target;
        if (!renderer.init() || !fonts.load(renderer, argv[1]) || !target.create(1920, 1080, 1))
            return 5;
        store::Screen screen;
        if (argc == 4)
        {
            store::catalog::Client catalog(argv[3]);
            store::catalog::Snapshot snapshot;
            std::string error;
            if (!catalog.cached(snapshot, error))
            {
                std::fprintf(stderr, "%s\n", error.c_str());
                return 8;
            }
            std::vector<store::App> apps;
            for (const auto &entry : snapshot.entries)
                apps.push_back({entry.id, entry.name, entry.author, entry.description, entry.kind,
                                entry.version, entry.status == "coming_soon" ? "Coming soon" : "",
                                0});
            screen.set_catalog(std::move(apps), "Verified catalog");
        }
        hui::ui::Feedback feedback;
        for (int frame = 0; frame < 120; ++frame)
            screen.update({}, 1.0f / 60.0f, feedback);
        screen.draw(renderer, fonts.refs);
        renderer.present(target.framebuffer(), 1920, 1080);
        glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer());
        std::vector<unsigned char> pixels(1920 * 1080 * 4);
        glReadPixels(0, 0, 1920, 1080, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        stbi_flip_vertically_on_write(1);
        if (!stbi_write_png(argv[2], 1920, 1080, 4, pixels.data(), 1920 * 4))
            return 6;
        const auto error = glGetError();
        std::printf("Snapshot: %s GL=0x%x draws=%zu\n", argv[2], error, renderer.last_draw_calls());
        if (error != GL_NO_ERROR)
            return 7;
    }
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglTerminate(display);
    return 0;
}
