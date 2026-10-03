// ProsperoStore - Storefront composition and navigation.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/store.hpp"
#include "core/save_file.hpp"
#include "ui/glyphs.hpp"

#include <utility>

namespace store
{
using namespace hui;

Screen::Screen() : theme_(ui::themes()[0])
{
    grid_.style.theme = theme_;
    grid_.style.columns = 5;
    grid_.style.card.art_aspect = 1.5f;
    grid_.style.card.title_size = 26;
    grid_.style.card.subtitle_size = 24;
    grid_.style.card.glow = true;
    grid_.set_bounds({96, 570, 1728, 380});
    tabs_.style.theme = theme_;
    tabs_.style.kind = ui::TabKind::underline;
    tabs_.style.on_page = true;
    tabs_.set_bounds({96, 462, 1728, 64});
    tabs_.set_tabs({{"Discover", 0, false, 0},
                    {"Apps", 0, false, 1},
                    {"Games", 0, false, 2},
                    {"Tools", 0, false, 3},
                    {"Coming soon", 0, false, 4},
                    {"Installed", 0, false, 5},
                    {"Updates", 0, false, 6}});
    tabs_.set_focused(false);
}

void Screen::set_catalog(std::vector<App> apps, std::string status)
{
    apps_ = std::move(apps);
    status_ = std::move(status);
    refresh_grid();
}

void Screen::refresh_grid()
{
    std::vector<ui::CardItem> cards;
    visible_.clear();
    const int section = tabs_.active();
    for (std::size_t i = 0; i < apps_.size(); ++i)
    {
        const App &app = apps_[i];
        if ((section == 1 && app.kind != "app") || (section == 2 && app.kind != "game") ||
            (section == 3 && app.kind != "tool") || (section == 4 && app.badge != "Coming soon") ||
            (section == 5 && app.badge != "Installed") || (section == 6 && app.badge != "Update"))
            continue;
        ui::CardItem card;
        card.title = app.name;
        card.subtitle = app.author;
        card.badge = app.badge;
        card.texture = app.icon;
        card.accent = theme_.primary;
        cards.push_back(std::move(card));
        visible_.push_back(i);
    }
    grid_.set_items(std::move(cards));
    grid_.enter();
}

void Screen::set_detail(const catalog::Entry &entry)
{
    for (auto &app : apps_)
        if (app.title_id == entry.id)
        {
            app.description = entry.description;
            app.version = entry.version;
            break;
        }
}

void Screen::set_icon(const std::string &id, std::uint32_t texture)
{
    for (auto &app : apps_)
        if (app.title_id == id)
            app.icon = texture;
    for (std::size_t i = 0; i < visible_.size(); ++i)
        if (apps_[visible_[i]].title_id == id)
            grid_.item(static_cast<int>(i)).texture = texture;
}

void Screen::update(const InputFrame &input, float dt, ui::Feedback &feedback)
{
    time_ += dt;
    if (input.is_pressed(Action::back))
    {
        if (details_)
            details_ = false;
        else
            quit_ = true;
        feedback.play(audio::Cue::back);
    }
    else if (!details_ &&
             (input.is_pressed(Action::page_next) || input.is_pressed(Action::page_prev)))
    {
        tabs_.step(input.is_pressed(Action::page_next) ? 1 : -1, input, feedback);
        refresh_grid();
    }
    else if (!details_ && grid_.handle(input, feedback) == ui::Event::activated)
    {
        details_ = true;
        pending_detail = apps_[visible_[static_cast<std::size_t>(grid_.focus())]].title_id;
    }
    grid_.set_active(!details_);
    grid_.update(dt);
    tabs_.update(dt);
}

void Screen::draw(gfx::Renderer &renderer, const ui::Fonts &fonts)
{
    scene_.clear();
    ui::Canvas canvas{scene_, fonts, renderer.glass_texture(), time_};
    ui::Painter paint(scene_, fonts, theme_);
    paint.heading("ProsperoStore", 96, 106, 36, paint.page_text());
    ui::text(scene_, fonts.regular, status_, 1824, 101, 24, paint.page_text_muted(),
             gfx::Align::right);
    if (details_ && !visible_.empty())
    {
        const App &app = apps_[visible_[static_cast<std::size_t>(grid_.focus())]];
        paint.panel({96, 190, 540, 540});
        if (app.icon != 0)
            scene_.image(app.icon, {128, 222, 476, 476}, gfx::kFullUv, gfx::Color::rgb(0xffffff));
        paint.heading(ui::fit_label(paint, app.name, 64, 1080), 704, 280, 64, paint.page_text());
        ui::text(scene_, fonts.regular, app.author, 704, 333, 28, paint.page_text_muted());
        ui::paragraph(scene_, fonts.regular,
                      app.description.empty() ? "Loading app details..." : app.description, 704,
                      407, 28, 1030, 40, paint.page_text(), 7);
        paint.button({704, 770, 500, 72}, "Checking install requirements", ui::ButtonKind::primary,
                     {1, 0, true});
        ui::text(scene_, fonts.regular, "Apps are provided by their developers.", 704, 900, 24,
                 paint.page_text_muted());
    }
    else
    {
        paint.heading("Your next discovery.", 96, 266, 76, paint.page_text());
        ui::text(scene_, fonts.regular, "Independent apps. New possibilities.", 100, 325, 30,
                 paint.page_text_muted());
        ui::text(scene_, fonts.regular, "Made for your console. Curated by the community.", 100,
                 379, 26, paint.page_text_muted());
        tabs_.draw(canvas);
        if (visible_.empty())
        {
            paint.panel({112, 590, 1696, 280});
            paint.heading(apps_.empty() ? "The catalog is on its way" : "Nothing here yet", 960,
                          707, 36, gfx::Align::center);
            ui::text(scene_, fonts.regular,
                     apps_.empty() ? "Verified apps will appear here when the catalog is ready."
                                   : "Explore Discover to find something new.",
                     960, 764, 26, theme_.text, gfx::Align::center);
        }
        else
            grid_.draw(canvas);
    }
    const ui::Hint hints[] = {{ui::Button::cross, "Details"},
                              {ui::Button::l1, "Sections", ui::Button::r1},
                              {ui::Button::circle, details_ ? "Back" : "Close"}};
    ui::draw_hints(scene_, fonts, ui::GlyphStyle::dark(), hints, 3, 96, false);
    auto backdrop = theme_.backdrop;
    backdrop.time = time_;
    renderer.begin();
    renderer.backdrop(backdrop);
    renderer.draw(scene_);
}

bool Fonts::load(gfx::Renderer &renderer, const std::string &assets)
{
    constexpr const char *names[] = {"inter-regular",    "inter-semibold", "montserrat-medium",
                                     "dejavu-sans-mono", "press-start-2p", "patrick-hand"};
    ui::FontRef *refs_by_index[] = {&refs.regular, &refs.semibold, &refs.display,
                                    &refs.mono,    &refs.pixel,    &refs.hand};
    for (std::size_t i = 0; i < 6; ++i)
    {
        std::string data;
        if (!save::read_file(assets + "/fonts/" + names[i] + ".huifont", &data) ||
            !faces_[i].load(data))
            return false;
        *refs_by_index[i] = {&faces_[i], renderer.batch().create_font_texture(faces_[i])};
    }
    return true;
}
} // namespace store
