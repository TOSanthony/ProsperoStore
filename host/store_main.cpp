// ProsperoStore - Host renderer for inspecting the same screens as the console.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/store.hpp"
#include "catalog/client.hpp"
#include "catalog/icons.hpp"
#include "core/image.hpp"
#include "core/qr.hpp"
#include "core/save_file.hpp"
#include "gfx/gl_program.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <cstdio>
#include <cassert>
#include <set>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#include "../third_party/stb/stb_image_write.h"
#pragma clang diagnostic pop

static void check_artwork_requests()
{
    store::Screen screen;
    assert(screen.artwork().empty());
    std::vector<store::App> apps;
    for (unsigned i = 0; i < 100; ++i)
    {
        store::App app;
        app.title_id = "PPSA" + std::to_string(99000 + i);
        apps.push_back(app);
    }
    screen.set_catalog(std::move(apps), "test");
    hui::ui::Feedback feedback;
    hui::InputFrame down;
    down.nav = hui::Direction::down;
    for (unsigned i = 0; i < 20; ++i)
    {
        const auto wanted = screen.artwork();
        assert(!wanted.empty() && wanted.size() <= 16);
        assert(std::set<std::string>(wanted.begin(), wanted.end()).size() == wanted.size());
        screen.set_icon(wanted.front(), 1);
        assert(screen.artwork() == wanted); // Uploading artwork must never reset focus.
        screen.update(down, 1.0f / 60.0f, feedback);
    }
    const auto focused = screen.artwork().front();
    assert(focused == "PPSA99095");
    hui::InputFrame confirm;
    confirm.pressed = hui::action_bit(hui::Action::confirm);
    screen.update(confirm, 1.0f / 60.0f, feedback);
    assert(screen.artwork() == std::vector<std::string>{focused});
    screen.set_catalog({}, "empty");
    assert(screen.artwork().empty());
}

static void check_search_and_sort()
{
    store::Screen screen;
    store::App a, b, c;
    a.title_id = "PPSA99001";
    a.name = "Zulu";
    a.author = "Bear Studio";
    a.released = "2026-01-01";
    a.updated = "2026-10-01";
    b.title_id = "PPSA99002";
    b.name = "alpha";
    b.author = "Another Developer";
    b.released = "2026-09-01";
    b.updated = "2026-09-01";
    c.title_id = "PPSA99003";
    c.name = "Coming Soon";
    screen.set_catalog({a, b, c}, "test");
    assert(screen.artwork().front() == b.title_id);
    screen.set_query("STUDIO");
    assert(screen.artwork() == std::vector<std::string>{a.title_id});
    screen.set_query("ALpHa");
    assert(screen.artwork() == std::vector<std::string>{b.title_id});
    screen.set_query("no such app");
    assert(screen.artwork().empty());
    screen.set_query("");
    hui::InputFrame sort;
    sort.pressed = hui::action_bit(hui::Action::r3);
    hui::ui::Feedback feedback;
    screen.update(sort, 1.0f / 60.0f, feedback);
    assert(screen.artwork().front() == b.title_id); // Newest release.
    screen.update(sort, 1.0f / 60.0f, feedback);
    assert(screen.artwork().front() == a.title_id); // Recently updated.
    screen.set_catalog({c, b, a}, "refreshed");
    assert(screen.artwork().front() == a.title_id);
    screen.update(sort, 1.0f / 60.0f, feedback);
    assert(screen.artwork().front() == b.title_id);
    hui::InputFrame search;
    search.pressed = hui::action_bit(hui::Action::north);
    screen.update(search, 1.0f / 60.0f, feedback);
    assert(screen.pending_search);
    screen.pending_search = false;
    hui::InputFrame open;
    open.pressed = hui::action_bit(hui::Action::confirm);
    screen.update(open, 1.0f / 60.0f, feedback);
    assert(screen.pending_detail == b.title_id);
    screen.set_detail_error(b.title_id, "offline");
    screen.pending_detail.clear();
    screen.update(search, 1.0f / 60.0f, feedback);
    assert(screen.pending_detail == b.title_id && !screen.pending_search);
    store::catalog::Entry detail;
    detail.id = b.title_id;
    detail.description = "Verified description";
    screen.set_detail(detail);
    screen.set_catalog({}, "removed");
    assert(screen.artwork().empty());
    screen.update(search, 1.0f / 60.0f, feedback);
    assert(screen.pending_search);
}

static void check_installed_sections()
{
    store::Screen screen;
    store::App app;
    app.title_id = "PPSA99010";
    app.name = "Example";
    app.available_version = "02.000.000";
    screen.set_catalog({app}, "verified", true);
    store::system::Inventory inventory;
    store::system::InstalledApp installed;
    installed.id = app.title_id;
    installed.name = app.name;
    installed.version = "01.000.000";
    installed.path = "/data/homebrew/" + app.title_id;
    installed.managed = true;
    inventory.apps.push_back(installed);
    screen.set_inventory(inventory);
    hui::InputFrame section;
    section.pressed = hui::action_bit(hui::Action::page_next);
    hui::ui::Feedback feedback;
    for (int index = 0; index < 5; ++index)
        screen.update(section, 1.0f / 60.0f, feedback);
    assert(screen.artwork() == std::vector<std::string>{app.title_id});
    screen.update(section, 1.0f / 60.0f, feedback);
    assert(screen.artwork() == std::vector<std::string>{app.title_id});
    inventory.apps.front().managed = false;
    screen.set_inventory(inventory);
    assert(screen.artwork().empty());
}

int main(int argc, char **argv)
{
    check_artwork_requests();
    check_search_and_sort();
    check_installed_sections();
    if (argc < 3 || argc > 5)
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
        std::vector<GLuint> textures;
        {
            std::string encoded;
            hui::Image image;
            if (hui::save::read_file(std::string(argv[1]) + "/images/coming-soon.png", &encoded) &&
                hui::decode_png(encoded, image))
            {
                textures.push_back(
                    renderer.batch().create_texture(image.width, image.height, image.rgba.data()));
                screen.set_coming_soon_art(textures.back());
            }
        }
        if (argc >= 4)
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
            {
                apps.push_back({entry.id, entry.name, entry.author, entry.description, entry.kind,
                                entry.version, entry.status == "coming_soon" ? "Coming soon" : "",
                                0, entry.released, entry.updated});
                apps.back().available_version = entry.content_version;
            }
            screen.set_catalog(std::move(apps), "Verified catalog");
            store::catalog::Icons icons(std::string(argv[3]) + "/icons");
            store::net::Control control;
            for (const auto &entry : snapshot.entries)
            {
                hui::Image image;
                if (!icons.cached(entry, image))
                {
                    std::string encoded;
                    const auto response = store::net::fetch(
                        entry.icon, store::net::Purpose::catalog, 2u << 20, encoded, control);
                    if (!response.ok() || !icons.store(entry, encoded, image))
                        continue;
                }
                const auto texture =
                    renderer.batch().create_texture(image.width, image.height, image.rgba.data());
                textures.push_back(texture);
                screen.set_icon(entry.id, texture);
            }
        }
        hui::ui::Feedback feedback;
        if (argc == 5)
        {
            const std::string mode = argv[4];
            if (mode == "search")
                screen.set_query("radio");
            else if (mode == "notice")
                screen.notify("Update available: 01.000.010",
                              "A newer ProsperoStore is listed on homebrew.page.");
            else if (mode == "scrolled" || mode == "coming-soon")
            {
                hui::InputFrame move;
                move.nav = hui::Direction::down;
                move.pressed = mode == "scrolled" ? 0 : hui::action_bit(hui::Action::page_next);
                for (int index = 0; index < (mode == "scrolled" ? 2 : 4); ++index)
                    screen.update(move.pressed ? hui::InputFrame{.pressed = move.pressed} : move,
                                  1.0f / 60.0f, feedback);
            }
            else if (mode == "empty")
                screen.set_query("no matching application");
            else if (mode == "updated")
            {
                hui::InputFrame sort;
                sort.pressed = hui::action_bit(hui::Action::r3);
                screen.update(sort, 1.0f / 60.0f, feedback);
                screen.update(sort, 1.0f / 60.0f, feedback);
            }
            else if (mode == "installed" || mode == "updates" || mode == "local-detail")
            {
                store::system::Inventory inventory;
                inventory.apps = {
                    {"PPSA99001", "ProsperoRadio", "01.000.000", "/data/homebrew/PPSA99001", "",
                     false, true, false},
                    {"PPSA99002", "ProsperoLight", "01.000.000", "/data/homebrew/PPSA99002",
                     "Installed outside ProsperoStore. Not managed by this app.", false, false,
                     false},
                    {"PPSA99980", "Example local app", "01.000.001", "/mnt/ext1/homebrew/PPSA99980",
                     "Installed outside ProsperoStore. Not managed by this app.", false, false,
                     false},
                    {"", "Example.ffpkg", "", "/mnt/ext1/homebrew/Example.ffpkg",
                     "Image installed outside ProsperoStore. Not managed by this app.", true, false,
                     false}};
                screen.set_inventory(std::move(inventory));
                screen.set_status("Installed library preview");
                hui::InputFrame section;
                section.pressed = hui::action_bit(hui::Action::page_next);
                for (int index = 0; index < (mode == "updates" ? 6 : 5); ++index)
                    screen.update(section, 1.0f / 60.0f, feedback);
                if (mode == "local-detail")
                {
                    screen.set_query("Example local");
                    hui::InputFrame open;
                    open.pressed = hui::action_bit(hui::Action::confirm);
                    screen.update(open, 1.0f / 60.0f, feedback);
                    assert(screen.pending_detail.empty());
                }
            }
            else if (mode == "detail" || mode == "detail-error" || mode == "detail-end")
            {
                screen.set_query("radio");
                hui::InputFrame open;
                open.pressed = hui::action_bit(hui::Action::confirm);
                screen.update(open, 1.0f / 60.0f, feedback);
                assert(!screen.pending_detail.empty());
                if (mode == "detail-error")
                    screen.set_detail_error(screen.pending_detail, "The network is unavailable.");
                else
                {
                    store::catalog::Client client(argv[3]);
                    store::catalog::Snapshot snapshot;
                    store::catalog::Entry entry;
                    store::net::Control control;
                    std::string error;
                    assert(client.cached(snapshot, error));
                    assert(client.detail(snapshot, screen.pending_detail, entry, control, error));
                    screen.set_detail(entry);
                    hui::Image qr;
                    assert(hui::encode_qr("https://homebrew.page/app/" + entry.id + "/", qr));
                    const auto texture =
                        renderer.batch().create_texture(qr.width, qr.height, qr.rgba.data());
                    assert(texture);
                    textures.push_back(texture);
                    screen.set_qr(entry.id, texture, qr.width);
                }
            }
            else
                return 9;
        }
        for (int frame = 0; frame < 120; ++frame)
            screen.update({}, 1.0f / 60.0f, feedback);
        screen.draw(renderer, fonts.refs);
        if (argc == 5 && std::string(argv[4]) == "detail-end")
        {
            hui::InputFrame scroll;
            scroll.nav = hui::Direction::down;
            for (int frame = 0; frame < 120; ++frame)
                screen.update(scroll, 1.0f / 60.0f, feedback);
            screen.draw(renderer, fonts.refs);
        }
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
        glDeleteTextures(static_cast<GLsizei>(textures.size()), textures.data());
    }
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglTerminate(display);
    return 0;
}
