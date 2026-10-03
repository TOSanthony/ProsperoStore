// ProsperoStore - Storefront composition and navigation.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/store.hpp"
#include "core/save_file.hpp"
#include "ui/glyphs.hpp"
#include "ui/components/data_common.hpp"

#include <utility>
#include <algorithm>
#include <cctype>

namespace store
{
using namespace hui;
namespace
{
std::string folded(std::string text)
{
    for (auto &c : text)
        if (static_cast<unsigned char>(c) < 128)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
} // namespace

Screen::Screen() : theme_(ui::themes()[0])
{
    grid_.style.theme = theme_;
    grid_.style.columns = 5;
    grid_.style.card.art_aspect = 1.0f;
    grid_.style.card.title_size = 26;
    grid_.style.card.subtitle_size = 24;
    grid_.style.card.glow = true;
    grid_.set_bounds({96, 480, 1728, 470});
    tabs_.style.theme = theme_;
    tabs_.style.kind = ui::TabKind::underline;
    tabs_.style.on_page = true;
    tabs_.set_bounds({96, 372, 1728, 64});
    tabs_.set_tabs({{"Discover", 0, false, 0},
                    {"Apps", 0, false, 1},
                    {"Games", 0, false, 2},
                    {"Tools", 0, false, 3},
                    {"Coming soon", 0, false, 4},
                    {"Installed", 0, false, 5},
                    {"Updates", 0, false, 6}});
    tabs_.set_focused(false);
    article_.style.theme = theme_;
    article_.style.body_size = 28;
    article_.style.footer = false;
    article_.set_bounds({684, 364, 1140, 458});
}

void Screen::set_catalog(std::vector<App> apps, std::string status)
{
    const auto previous = visible_.empty()
                              ? std::string{}
                              : apps_[visible_[static_cast<std::size_t>(grid_.focus())]].title_id;
    apps_ = std::move(apps);
    status_ = std::move(status);
    refresh_grid();
    const auto found = std::find_if(visible_.begin(), visible_.end(),
                                    [&](auto index) { return apps_[index].title_id == previous; });
    if (found != visible_.end())
        grid_.set_focus(static_cast<int>(found - visible_.begin()));
    else
        details_ = false;
    if (details_)
    {
        refresh_detail();
        pending_detail = previous;
    }
}

void Screen::set_query(std::string query)
{
    query_ = std::move(query);
    refresh_grid();
    grid_.set_focus(0);
}

void Screen::refresh_grid()
{
    std::vector<ui::CardItem> cards;
    visible_.clear();
    const int section = tabs_.active();
    const auto query = folded(query_);
    for (std::size_t i = 0; i < apps_.size(); ++i)
    {
        const App &app = apps_[i];
        if ((section == 1 && app.kind != "app") || (section == 2 && app.kind != "game") ||
            (section == 3 && app.kind != "tool") || (section == 4 && app.badge != "Coming soon") ||
            (section == 5 && app.badge != "Installed") || (section == 6 && app.badge != "Update"))
            continue;
        if (!query.empty() && folded(app.name).find(query) == std::string::npos &&
            folded(app.author).find(query) == std::string::npos)
            continue;
        visible_.push_back(i);
    }
    std::stable_sort(visible_.begin(), visible_.end(),
                     [&](auto first, auto second)
                     {
                         const auto &a = apps_[first];
                         const auto &b = apps_[second];
                         if (sort_ == Sort::released && a.released != b.released)
                             return a.released > b.released;
                         if (sort_ == Sort::updated && a.updated != b.updated)
                             return a.updated > b.updated;
                         const auto a_name = folded(a.name), b_name = folded(b.name);
                         return a_name == b_name ? a.title_id < b.title_id : a_name < b_name;
                     });
    for (const auto index : visible_)
    {
        const auto &app = apps_[index];
        ui::CardItem card;
        card.title = app.name;
        card.subtitle = app.author;
        card.badge = app.badge;
        card.texture = app.icon;
        card.accent = theme_.primary;
        cards.push_back(std::move(card));
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
            app.detail = entry;
            app.detail_error.clear();
            break;
        }
    if (details_ && !visible_.empty() &&
        apps_[visible_[static_cast<std::size_t>(grid_.focus())]].title_id == entry.id)
        refresh_detail();
}

void Screen::set_detail_error(const std::string &id, std::string message)
{
    for (auto &app : apps_)
        if (app.title_id == id)
            app.detail_error = message;
    if (details_ && !visible_.empty() &&
        apps_[visible_[static_cast<std::size_t>(grid_.focus())]].title_id == id)
        refresh_detail();
}

void Screen::refresh_detail()
{
    const auto &app = apps_[visible_[static_cast<std::size_t>(grid_.focus())]];
    using Block = ui::TextBlock;
    std::vector<Block> blocks;
    if (!app.detail_error.empty())
        blocks.push_back(Block::paragraph("Details unavailable: " + app.detail_error));
    if (app.detail)
    {
        const auto &entry = *app.detail;
        blocks.push_back(Block::paragraph(entry.description.empty() ? "No description provided."
                                                                    : entry.description));
        blocks.push_back(Block::key_value("Kind", entry.kind));
        blocks.push_back(
            Block::key_value("Release", entry.version.empty() ? "Not released" : entry.version));
        if (!entry.released.empty())
            blocks.push_back(
                Block::key_value("Released", entry.released.substr(0, entry.released.find('T'))));
        if (entry.size != 0)
            blocks.push_back(Block::key_value(
                "Download", ui::format_value(static_cast<double>(entry.size)) + "B"));
        blocks.push_back(
            Block::key_value("License", entry.license.empty() ? "Not specified" : entry.license));
        if (!entry.source.empty())
        {
            blocks.push_back(Block::heading("Source repository", 2));
            blocks.push_back(Block::paragraph(entry.source));
        }
        if (!entry.page.empty())
        {
            blocks.push_back(Block::heading("App page", 2));
            blocks.push_back(Block::paragraph(entry.page));
        }
        if (!entry.release_notes.empty())
        {
            blocks.push_back(Block::heading("Release notes", 2));
            blocks.push_back(Block::paragraph(entry.release_notes));
        }
    }
    else if (app.detail_error.empty())
        blocks.push_back(Block::paragraph("Loading verified app details..."));
    article_.set_content(std::move(blocks));
    article_.scroll_to(0, true);
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

std::vector<std::string> Screen::artwork() const
{
    std::vector<std::string> wanted;
    if (visible_.empty())
        return wanted;
    wanted.reserve(16);
    const auto focus = grid_.focus();
    wanted.push_back(apps_[visible_[static_cast<std::size_t>(focus)]].title_id);
    if (details_)
        return wanted;
    const auto view = grid_.bounds();
    const int columns = grid_.columns();
    const int begin = std::max(0, focus - 2 * columns);
    const int end = std::min(static_cast<int>(visible_.size()), focus + 3 * columns);
    for (int i = begin; i < end && wanted.size() < 16; ++i)
    {
        const auto cell = grid_.cell_rect(i);
        if (i != focus && cell.y + cell.h >= view.y - cell.h && cell.y <= view.y + view.h + cell.h)
            wanted.push_back(apps_[visible_[static_cast<std::size_t>(i)]].title_id);
    }
    return wanted;
}

void Screen::update(const InputFrame &input, float dt, ui::Feedback &feedback)
{
    time_ += dt;
    if (details_ && input.is_pressed(Action::north))
    {
        auto &app = apps_[visible_[static_cast<std::size_t>(grid_.focus())]];
        app.detail_error.clear();
        pending_detail = app.title_id;
        refresh_detail();
        feedback.play(audio::Cue::select);
    }
    else if (!details_ && input.is_pressed(Action::north))
    {
        pending_search = true;
        feedback.play(audio::Cue::select);
    }
    else if (!details_ && input.is_pressed(Action::r3))
    {
        sort_ = static_cast<Sort>((static_cast<unsigned>(sort_) + 1) % 3);
        refresh_grid();
        grid_.set_focus(0);
        feedback.play(audio::Cue::select);
    }
    else if (input.is_pressed(Action::back))
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
        grid_.set_focus(0);
    }
    else if (!details_ && grid_.handle(input, feedback) == ui::Event::activated)
    {
        details_ = true;
        pending_detail = apps_[visible_[static_cast<std::size_t>(grid_.focus())]].title_id;
        refresh_detail();
    }
    else if (details_)
        article_.handle(input, feedback);
    article_.set_active(details_);
    article_.update(dt);
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
    ui::text(scene_, fonts.regular, ui::fit_label(paint, status_, 24, 1200), 1824, 101, 24,
             paint.page_text_muted(), gfx::Align::right);
    if (details_ && !visible_.empty())
    {
        const App &app = apps_[visible_[static_cast<std::size_t>(grid_.focus())]];
        paint.panel({96, 190, 540, 540});
        if (app.icon != 0)
            scene_.image(app.icon, {128, 222, 476, 476}, gfx::kFullUv, gfx::Color::rgb(0xffffff));
        paint.heading(ui::fit_label(paint, app.name, 64, 1080), 704, 280, 64, paint.page_text());
        ui::text(scene_, fonts.regular, ui::fit_label(paint, app.author, 28, 1080), 704, 333, 28,
                 paint.page_text_muted());
        article_.draw(canvas);
        ui::text(scene_, fonts.regular,
                 app.badge == "Coming soon"
                     ? "Coming soon"
                     : "Installation is not available in this development build.",
                 704, 868, 24, paint.page_text_muted());
        ui::paragraph(scene_, fonts.regular,
                      "Apps are provided by their developers. Check the release notes for required "
                      "payloads or extra setup.",
                      704, 912, 24, 1080, 32, paint.page_text_muted(), 2);
    }
    else
    {
        paint.heading("Your next discovery.", 96, 236, 76, paint.page_text());
        ui::text(scene_, fonts.regular,
                 ui::fit_label(paint,
                               query_.empty() ? "Independent apps. New possibilities."
                                              : "Search: " + query_,
                               30, 1600),
                 100, 295, 30, paint.page_text_muted());
        tabs_.draw(canvas);
        const char *sort_name = sort_ == Sort::name       ? "Name"
                                : sort_ == Sort::released ? "Newest release"
                                                          : "Recently updated";
        ui::text(scene_, fonts.regular, std::string("Sort: ") + sort_name, 1824, 466, 24,
                 paint.page_text_muted(), gfx::Align::right);
        if (visible_.empty())
        {
            paint.panel({112, 590, 1696, 280});
            paint.heading(!query_.empty() ? "No matches"
                          : apps_.empty() ? "The catalog is on its way"
                                          : "Nothing here yet",
                          960, 707, 36, gfx::Align::center);
            ui::text(scene_, fonts.regular,
                     !query_.empty() ? "Try another app name or developer."
                     : apps_.empty() ? "Verified apps will appear here when the catalog is ready."
                                     : "Explore Discover to find something new.",
                     960, 764, 26, theme_.text, gfx::Align::center);
        }
        else
            grid_.draw(canvas);
    }
    const ui::Hint hints[] = {{ui::Button::cross, "Details"},
                              {ui::Button::triangle, "Search"},
                              {ui::Button::right_stick, "Sort"},
                              {ui::Button::l1, "Sections", ui::Button::r1},
                              {ui::Button::circle, details_ ? "Back" : "Close"}};
    if (details_)
    {
        const ui::Hint back[] = {{ui::Button::dpad, "Scroll"},
                                 {ui::Button::triangle, "Refresh details"},
                                 {ui::Button::circle, "Back"}};
        ui::draw_hints(scene_, fonts, ui::GlyphStyle::dark(), back, 3, 96, false);
    }
    else
        ui::draw_hints(scene_, fonts, ui::GlyphStyle::dark(), hints + (visible_.empty() ? 1 : 0),
                       visible_.empty() ? 4 : 5, 96, false);
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
