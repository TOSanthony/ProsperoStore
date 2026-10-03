// ProsperoStore - Storefront composition and navigation.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/store.hpp"
#include "core/save_file.hpp"
#include "install/transaction.hpp"
#include "system/locations.hpp"
#include "ui/glyphs.hpp"
#include "ui/components/data_common.hpp"

#include <utility>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>

namespace store
{
using namespace hui;
namespace
{
using gfx::Color;
using gfx::Rect;

// ---- the design language: Storefront's shapes in Farlight's colours ----

const Color kWhite = Color::rgb(0xffffff);
const Color kBlack = Color::rgb(0x000000);
const Color kClear = Color::rgb(0x000000, 0.0f);
const Color kInk = Color::rgb(0xf4efe6);      // warm white: all text
const Color kDeep = Color::rgb(0x0e0f24);     // Farlight: its dark,
const Color kMid = Color::rgb(0x42358f);      // ... its mid tone
const Color kAccent = Color::rgb(0xffd166);   // ... and its light: calls to action
const Color kOnAccent = Color::rgb(0x241a05); // text on the accent
const Color kCoal = Color::rgb(0x0e0f12);
const Color kPanel = gfx::mix(Color::rgb(0x202228), kDeep, 0.5f);
const Color kOwned = Color::rgb(0x8fdab2); // "this is installed"

constexpr float kWidth = gfx::kVirtualWidth;
constexpr float kHeight = gfx::kVirtualHeight;
constexpr float kMargin = 96.0f;
constexpr float kRight = kWidth - kMargin;
constexpr float kTopY = 78.0f;

// The home page scrolls as one sheet under the top bar.
constexpr float kPageTop = 104.0f;
constexpr float kBannerY = 116.0f;
constexpr float kBannerH = 300.0f;
constexpr float kBannerRadius = 28.0f;
constexpr float kBannerSeconds = 6.0f;
constexpr std::size_t kFeatured = 5;
constexpr float kChipsY = 440.0f; // under the banner
constexpr float kChipH = 48.0f;
constexpr float kStickY = 116.0f; // where the chips stop when the banner scrolls away
constexpr float kGridGap = 24.0f; // between the chips and the first row
constexpr int kColumns = 5;
constexpr float kCardGap = 24.0f;
constexpr float kCardW = (kWidth - 2.0f * kMargin - (kColumns - 1) * kCardGap) / kColumns;
constexpr float kCoverH = kCardW; // app icons are square
constexpr float kCardH = kCoverH + 84.0f;
constexpr float kCardRadius = 14.0f;
constexpr float kRowPitch = kCardH + 28.0f;
constexpr float kLift = 0.04f;        // how much the focused card grows
constexpr float kViewBottom = 968.0f; // the grid ends above the hint row
constexpr float kFadeFoot = 56.0f;    // ... and fades out over this distance
constexpr float kFadeHead = 20.0f;

// The product page.
constexpr Rect kPreview{96.0f, 128.0f, 600.0f, 600.0f};
constexpr float kPreviewRadius = 28.0f;
constexpr float kInfoX = 768.0f;
constexpr float kInfoW = 520.0f;
constexpr Rect kActionBox{1336.0f, 392.0f, 488.0f, 472.0f};
constexpr Rect kActionButton{1368.0f, 600.0f, 424.0f, 72.0f};
constexpr float kButtonRadius = 18.0f;

constexpr const char *kSectionNames[] = {"Discover",    "Apps",      "Games",  "Tools",
                                         "Coming soon", "Installed", "Updates"};

// What one pass over the cards records: all covers, then all shapes, then each
// face, so a grid costs a few draw calls instead of several per card.
enum Layer : unsigned
{
    kImages = 1,
    kShapes = 2,
    kSemibold = 4,
    kRegular = 8,
    kAllLayers = 15,
};

std::string folded(std::string text)
{
    for (auto &c : text)
        if (static_cast<unsigned char>(c) < 128)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// "31.8 MB", "548 GB": sizes as people say them.
std::string size_text(std::uint64_t bytes)
{
    char text[32];
    const double megabytes = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (megabytes >= 10240.0)
        std::snprintf(text, sizeof(text), "%.0f GB", megabytes / 1024.0);
    else if (megabytes >= 1024.0)
        std::snprintf(text, sizeof(text), "%.1f GB", megabytes / 1024.0);
    else if (megabytes >= 10.0)
        std::snprintf(text, sizeof(text), "%.0f MB", megabytes);
    else
        std::snprintf(text, sizeof(text), "%.1f MB", megabytes);
    return text;
}

// Baseline that centres a line of the given size on cy.
float centred(float cy, float size)
{
    return cy + size * 0.35f;
}

void draw_check(gfx::DrawList &list, float cx, float cy, float size, Color colour)
{
    const float stroke = std::max(2.0f, size * 0.16f);
    list.line(cx - size * 0.42f, cy + size * 0.02f, cx - size * 0.12f, cy + size * 0.32f, stroke,
              colour);
    list.line(cx - size * 0.12f, cy + size * 0.32f, cx + size * 0.44f, cy - size * 0.3f, stroke,
              colour);
}

// A pill with a word on it. Returns its width.
float pill(gfx::DrawList &list, const ui::Fonts &fonts, std::string_view word, float x, float cy,
           float height, Color fill, Color ink, float alpha, unsigned layers)
{
    const float size = height * 0.58f;
    const float width = fonts.semibold.measure(word, size) + height * 0.9f;
    if (layers & kShapes)
        list.rounded_rect({x, cy - height * 0.5f, width, height}, height * 0.5f,
                          fill.with_alpha(alpha));
    if (layers & kSemibold)
        ui::text(list, fonts.semibold, word, x + width * 0.5f, centred(cy, size), size,
                 ink.with_alpha(alpha), gfx::Align::center);
    return width;
}

// The state a card or the banner announces, and how loudly.
struct Mark
{
    const char *word = nullptr;
    bool loud = false; // on the accent
};
Mark mark(const App &app)
{
    if (app.badge == "Update")
        return {"Update", true};
    if (app.badge == "Duplicate")
        return {"Duplicate", false};
    if (app.badge == "Coming soon")
        return {"Coming soon", false};
    return {};
}

ui::Theme farlight_theme()
{
    ui::Theme theme = ui::themes()[0];
    theme.page = kCoal;
    theme.page_text = kInk;
    theme.page_text_muted = kInk.with_alpha(0.62f);
    theme.surface = kPanel;
    theme.surface_high = gfx::mix(kPanel, kMid, 0.18f);
    theme.text = kInk;
    theme.text_muted = kInk.with_alpha(0.62f);
    theme.primary = kAccent;
    theme.on_primary = kOnAccent;
    theme.accent = kAccent;
    theme.outline = kInk.with_alpha(0.2f);
    theme.focus = kInk;
    theme.success = kOwned;
    return theme;
}
} // namespace

Screen::Screen() : theme_(farlight_theme())
{
    article_.style.theme = theme_;
    article_.style.body_size = 26;
    article_.style.footer = false;
    article_.style.panel = false;
    article_.style.focus_ring = false;
    article_.style.padding = 4;
    article_.set_bounds({kInfoX - 4.0f, 392.0f, kInfoW + 8.0f, 500.0f});
    dialog_.style.theme = theme_;
    about_.style.theme = theme_;
    about_.style.body_size = 26;
    about_.style.footer = false;
    about_.style.panel = false;
    about_.style.focus_ring = false;
    about_.style.padding = 4;
    about_.set_bounds({kMargin - 4.0f, 350.0f, 1300.0f, 590.0f});
    toasts_.style.theme = theme_;
    toasts_.style.frosted = true;
    toasts_.style.duration = 10.0f;
    plate_.snap(1.0f);
}

void Screen::notify(std::string title, std::string body)
{
    toasts_.push(ui::StatusKind::info, std::move(title), std::move(body), 10.0f);
}

// ---- data -------------------------------------------------------------------

bool Screen::in_section(const App &app, int section) const
{
    if (app.local_only && section != 5)
        return false;
    switch (section)
    {
    case 1:
        return app.kind == "app";
    case 2:
        return app.kind == "game";
    case 3:
        return app.kind == "tool";
    case 4:
        return app.catalog_badge == "Coming soon";
    case 5:
        return !app.installed.empty();
    case 6:
        return app.badge == "Update";
    default:
        return true;
    }
}

void Screen::rebuild()
{
    visible_.clear();
    for (int &count : counts_)
        count = 0;
    const auto query = folded(query_);
    for (std::size_t i = 0; i < apps_.size(); ++i)
    {
        const App &app = apps_[i];
        if (!query.empty() && app.folded_name.find(query) == std::string::npos &&
            app.folded_author.find(query) == std::string::npos)
            continue;
        for (int section = 0; section < kSections; ++section)
            counts_[section] += in_section(app, section) ? 1 : 0;
        if (in_section(app, section_))
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
                         return a.folded_name == b.folded_name ? a.title_id < b.title_id
                                                               : a.folded_name < b.folded_name;
                     });
    // The banner features the newest releases.
    featured_.clear();
    for (std::size_t i = 0; i < apps_.size(); ++i)
        if (!apps_[i].local_only && !apps_[i].released.empty() &&
            apps_[i].catalog_badge != "Coming soon")
            featured_.push_back(i);
    std::stable_sort(featured_.begin(), featured_.end(), [&](auto first, auto second)
                     { return apps_[first].released > apps_[second].released; });
    if (featured_.size() > kFeatured)
        featured_.resize(kFeatured);
    banner_ = std::min(banner_, std::max(0, static_cast<int>(featured_.size()) - 1));
    banner_previous_ = banner_;
    const int count = static_cast<int>(visible_.size());
    focus_ = std::clamp(focus_, 0, std::max(0, count - 1));
    if (count == 0 && zone_ == Zone::grid)
        zone_ = Zone::chips;
    if (zone_ == Zone::banner && !banner_shown())
        zone_ = count ? Zone::grid : Zone::chips;
    lift_.resize(apps_.size());
    appear_.resize(apps_.size());
    fits_stale_ = true;
}

// The cards whose rows are on screen at this scroll, and one row beyond.
void Screen::rows_in_view(float scroll, int &first, int &last) const
{
    const int count = static_cast<int>(visible_.size());
    const float top = scroll + kPageTop - grid_top() - kRowPitch;
    const float bottom = scroll + kHeight - grid_top() + kRowPitch;
    first = std::clamp(static_cast<int>(std::floor(top / kRowPitch)) * kColumns, 0, count);
    last = std::clamp((static_cast<int>(std::floor(bottom / kRowPitch)) + 1) * kColumns, 0, count);
}

void Screen::stress(std::size_t count)
{
    std::vector<App> apps;
    for (const auto &app : apps_)
        if (!app.local_only)
            apps.push_back(app);
    const std::size_t real = apps.size();
    for (std::size_t i = 0; real && apps.size() < count; ++i)
    {
        App copy = apps[i % real];
        const auto number = std::to_string(10000 + i);
        copy.title_id = "PPSA" + number;
        copy.name += " " + number.substr(1);
        copy.badge = copy.catalog_badge;
        copy.installed.clear();
        apps.push_back(std::move(copy));
    }
    const auto icons = apps;
    set_catalog(std::move(apps), status_, catalog_current_);
    for (const auto &app : icons)
    {
        set_icon(app.title_id, app.icon);
        set_art(app.title_id, app.art);
    }
}

const App *Screen::focused() const
{
    return visible_.empty() ? nullptr : &apps_[visible_[static_cast<std::size_t>(focus_)]];
}

const App *Screen::page_app() const
{
    return details_ ? focused() : nullptr;
}

bool Screen::banner_shown() const
{
    return section_ == 0 && query_.empty() && !featured_.empty();
}

std::uint32_t Screen::art(const App &app, bool large) const
{
    return large && app.art                     ? app.art
           : app.icon                           ? app.icon
           : app.art                            ? app.art
           : app.catalog_badge == "Coming soon" ? coming_soon_art_
                                                : 0;
}

void Screen::set_catalog(std::vector<App> apps, std::string status, bool current)
{
    const auto previous = focused() ? focused()->title_id : std::string{};
    const auto shown = details_ ? previous : std::string{};
    apps_ = std::move(apps);
    catalog_current_ = current;
    for (auto &app : apps_)
    {
        app.catalog_badge = app.badge;
        app.installed.clear();
        app.folded_name = folded(app.name);
        app.folded_author = folded(app.author);
    }
    fresh_catalog_ = true;
    // With a thousand apps and a hundred installed, a search per installed
    // app is a hundred thousand comparisons on the frame: look them up instead.
    // (Room for the local ones first: the keys point into the apps.)
    apps_.reserve(apps_.size() + inventory_.apps.size());
    std::map<std::string_view, std::size_t> listed;
    for (std::size_t i = 0; i < apps_.size(); ++i)
        listed.emplace(apps_[i].title_id, i);
    for (const auto &installed : inventory_.apps)
    {
        const auto id = installed.id.empty() ? "local:" + installed.path : installed.id;
        const auto known = listed.find(id);
        auto found = known == listed.end()
                         ? apps_.end()
                         : apps_.begin() + static_cast<std::ptrdiff_t>(known->second);
        if (found == apps_.end())
        {
            App local;
            local.title_id = id;
            local.name = installed.name;
            local.author = installed.image ? "Installed image" : "Installed app";
            local.local_only = true;
            local.folded_name = folded(local.name);
            local.folded_author = folded(local.author);
            apps_.push_back(std::move(local));
            found = apps_.end() - 1;
            listed.emplace(found->title_id, apps_.size() - 1);
        }
        found->installed.push_back(installed);
    }
    for (auto &app : apps_)
        if (!app.installed.empty())
        {
            const auto &installed = app.installed.front();
            app.badge = installed.duplicate ? "Duplicate"
                        : installed.managed &&
                                catalog::update_available(installed.version, app.available_version)
                            ? "Update"
                            : "Installed";
        }
    status_ = std::move(status);
    loading_ = loading_ && apps_.empty();
    rebuild();
    const auto found = std::find_if(visible_.begin(), visible_.end(),
                                    [&](auto index) { return apps_[index].title_id == previous; });
    if (found != visible_.end())
        focus_ = static_cast<int>(found - visible_.begin());
    details_ = details_ && found != visible_.end() && shown == previous;
    if (details_)
    {
        refresh_detail();
        pending_detail = apps_[*found].local_only ? std::string{} : previous;
    }
}

void Screen::set_inventory(system::Inventory inventory)
{
    inventory_ = std::move(inventory);
    // Where the store itself is installed, for its own update.
    for (const auto &app : inventory_.apps)
        if (app.id == self_id_ && !app.image && app.path.ends_with("/" + self_id_))
            self_location_ = app.path.substr(0, app.path.find_last_of('/'));
    inventory_ready_ = true;
    auto catalog_apps = apps_;
    std::erase_if(catalog_apps, [](const auto &app) { return app.local_only; });
    for (auto &app : catalog_apps)
        app.badge = app.catalog_badge;
    set_catalog(std::move(catalog_apps), status_, catalog_current_);
}

void Screen::set_query(std::string query)
{
    query_ = std::move(query);
    focus_ = 0;
    rebuild();
    if (!visible_.empty())
        zone_ = Zone::grid;
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
    if (page_app() && page_app()->title_id == entry.id)
        refresh_detail();
}

void Screen::set_detail_error(const std::string &id, std::string message)
{
    for (auto &app : apps_)
        if (app.title_id == id)
            app.detail_error = message;
    if (page_app() && page_app()->title_id == id)
        refresh_detail();
}

void Screen::refresh_detail()
{
    const auto &app = *focused();
    using Block = ui::TextBlock;
    std::vector<Block> blocks;
    if (app.local_only && catalog_current_ &&
        std::any_of(app.installed.begin(), app.installed.end(),
                    [](const auto &installed) { return installed.managed; }))
        blocks.push_back(Block::paragraph("This app is no longer listed in the verified catalog. "
                                          "Check with its developer before using it."));
    if (!app.detail_error.empty())
        blocks.push_back(Block::paragraph("Details unavailable: " + app.detail_error));
    if (app.detail)
    {
        const auto &entry = *app.detail;
        blocks.push_back(Block::paragraph(entry.description.empty() ? "No description provided."
                                                                    : entry.description));
        blocks.push_back(
            Block::key_value("Release", entry.version.empty() ? "Not released" : entry.version));
        if (!entry.released.empty())
            blocks.push_back(
                Block::key_value("Released", entry.released.substr(0, entry.released.find('T'))));
        if (entry.size != 0)
            blocks.push_back(Block::key_value("Download", size_text(entry.size)));
        blocks.push_back(
            Block::key_value("License", entry.license.empty() ? "Not specified" : entry.license));
        if (!entry.source.empty())
        {
            blocks.push_back(Block::heading("Source repository", 3));
            blocks.push_back(Block::paragraph(entry.source));
        }
        if (!entry.release_notes.empty())
        {
            blocks.push_back(Block::heading("Release notes", 3));
            blocks.push_back(Block::paragraph(entry.release_notes));
        }
    }
    else if (app.detail_error.empty() && !app.local_only)
        blocks.push_back(Block::paragraph("Loading verified app details..."));
    for (const auto &installed : app.installed)
    {
        blocks.push_back(Block::heading(installed.image ? "Installed image" : "Installed", 3));
        if (!installed.version.empty())
            blocks.push_back(Block::key_value("Version", installed.version));
        blocks.push_back(Block::paragraph(installed.path));
    }
    article_.set_content(std::move(blocks));
    article_.scroll_to(0, true);
}

void Screen::set_icon(const std::string &id, std::uint32_t texture)
{
    for (std::size_t i = 0; i < apps_.size(); ++i)
        if (apps_[i].title_id == id)
        {
            apps_[i].icon = texture;
            if (texture && fresh_catalog_ && i < appear_.size())
                appear_[i].snap(1.0f);
        }
}

void Screen::set_art(const std::string &id, std::uint32_t texture)
{
    for (auto &app : apps_)
        if (app.title_id == id)
            app.art = texture;
}

std::vector<std::string> Screen::artwork_backlog() const
{
    auto wanted = artwork();
    for (const auto &app : apps_)
        if (!app.local_only && !app.icon &&
            std::find(wanted.begin(), wanted.end(), app.title_id) == wanted.end())
            wanted.push_back(app.title_id);
    return wanted;
}

void Screen::set_qr(std::string id, std::uint32_t texture, int width)
{
    qr_id_ = std::move(id);
    qr_texture_ = texture;
    qr_width_ = width;
}

std::vector<std::string> Screen::artwork() const
{
    std::vector<std::string> wanted;
    const App *first = focused();
    if (first && !first->local_only)
        wanted.push_back(first->title_id);
    if (details_ || !first)
        return wanted;
    wanted.reserve(16);
    // The rows on screen and one beyond each edge, then the banner's title.
    int begin = 0, end = 0;
    rows_in_view(scroll_.target, begin, end);
    for (int k = begin; k < end && wanted.size() < 15; ++k)
    {
        const App &app = apps_[visible_[static_cast<std::size_t>(k)]];
        if (k != focus_ && !app.local_only)
            wanted.push_back(app.title_id);
    }
    if (banner_shown())
    {
        const auto &id = apps_[featured_[static_cast<std::size_t>(banner_)]].title_id;
        if (std::find(wanted.begin(), wanted.end(), id) == wanted.end())
            wanted.push_back(id);
    }
    return wanted;
}

// ---- geometry ---------------------------------------------------------------

float Screen::chips_rest() const
{
    return banner_shown() ? kChipsY : kStickY;
}
float Screen::chips_y() const
{
    return std::max(chips_rest() - scroll_.value, kStickY);
}
float Screen::grid_top() const
{
    return chips_rest() + kChipH + kGridGap;
}
float Screen::window_top() const
{
    return chips_y() + kChipH + 4.0f;
}
// How visible something at screen height y is inside the grid's window.
float Screen::fade_at(float y) const
{
    return std::min(tween::clamp01((y - window_top()) / kFadeHead),
                    tween::clamp01((kViewBottom - y) / kFadeFoot));
}
// A card's place on the unscrolled page.
Rect Screen::card_rect(int index) const
{
    return {kMargin + static_cast<float>(index % kColumns) * (kCardW + kCardGap),
            grid_top() + static_cast<float>(index / kColumns) * kRowPitch, kCardW, kCardH};
}
// What the focus highlight surrounds, in page coordinates: the scroll offset
// is taken off when it is drawn, so the highlight rides with the page.
Rect Screen::focus_target() const
{
    if (zone_ == Zone::banner)
        return Rect{kMargin, kBannerY, kWidth - 2.0f * kMargin, kBannerH}.inset(-7.0f);
    if (zone_ == Zone::chips || visible_.empty())
    {
        Rect chip = chips_[section_].inset(-6.0f);
        chip.y += scroll_.value;
        return chip;
    }
    return card_rect(focus_).inset(-12.0f);
}
float Screen::focus_radius() const
{
    if (zone_ == Zone::banner)
        return kBannerRadius + 7.0f;
    if (zone_ == Zone::chips || visible_.empty())
        return kChipH * 0.5f + 6.0f;
    return 24.0f;
}
// Any row but the first sends the banner away and parks the chips under the
// top bar, with the focused row right below them.
float Screen::scroll_target() const
{
    if (zone_ != Zone::grid || visible_.empty() || focus_ < kColumns)
        return 0.0f;
    return card_rect(focus_).y - (kStickY + kChipH + kGridGap);
}

// ---- input ------------------------------------------------------------------

// The edge of something: a quiet "no" (and nothing at all for a held
// direction, which only means the player has not let go yet).
void Screen::refuse(ui::Feedback &feedback, bool repeat, float dx, float dy)
{
    if (repeat)
        return;
    feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
    feedback.rumble(0.25f, 0.05f);
    nudge_.trigger();
    nudge_x_ = dx;
    nudge_y_ = dy;
}

void Screen::step_section(int delta, bool repeat, ui::Feedback &feedback)
{
    const int next = section_ + delta;
    if (next < 0 || next >= kSections)
        return refuse(feedback, repeat, static_cast<float>(delta), 0.0f);
    section_ = next;
    focus_ = 0;
    rebuild();
    swap_.start(0.4f);
    feedback.play(audio::Cue::tab, 0.92f + 0.04f * static_cast<float>(next),
                  ui::pan_for_x(chips_[next].cx()));
}

void Screen::show_banner(int slot)
{
    if (slot == banner_)
        return;
    banner_previous_ = banner_;
    banner_ = slot;
    banner_fade_.start(0.6f);
    banner_clock_ = 0.0f;
}

void Screen::update_home(const InputFrame &input, ui::Feedback &feedback)
{
    if (input.is_pressed(Action::north))
    {
        pending_search = true;
        feedback.play(audio::Cue::select);
        return;
    }
    if (input.is_pressed(Action::r3))
    {
        sort_ = static_cast<Sort>((static_cast<unsigned>(sort_) + 1) % 3);
        focus_ = 0;
        rebuild();
        swap_.start(0.3f);
        feedback.play(audio::Cue::select);
        return;
    }
    if (input.is_pressed(Action::page_next) || input.is_pressed(Action::page_prev))
        return step_section(input.is_pressed(Action::page_next) ? 1 : -1, false, feedback);
    if (section_ == 6 && input.is_pressed(Action::west) && !visible_.empty())
    {
        // Update all: every update on this shelf, in the order shown.
        if (!installer_ || !guard_)
            return refuse(feedback, false, 0.0f, 1.0f);
        update_all_.clear();
        for (const auto index : visible_)
            update_all_.push_back(apps_[index].title_id);
        toasts_.push(ui::StatusKind::info,
                     "Updating " + std::to_string(update_all_.size()) +
                         (update_all_.size() == 1 ? " app" : " apps"),
                     "Apps that are running are skipped.", 6.0f);
        feedback.play(audio::Cue::select);
        return;
    }

    const int count = static_cast<int>(visible_.size());
    const int featured = static_cast<int>(featured_.size());
    if (zone_ == Zone::grid && count > 0)
    {
        const int column = focus_ % kColumns, row = focus_ / kColumns;
        const int before = focus_;
        switch (input.nav)
        {
        case Direction::left:
            if (column == 0)
                refuse(feedback, input.nav_repeat, -1.0f, 0.0f);
            else
                --focus_;
            break;
        case Direction::right:
            if (column == kColumns - 1 || focus_ + 1 >= count)
                refuse(feedback, input.nav_repeat, 1.0f, 0.0f);
            else
                ++focus_;
            break;
        case Direction::up:
            if (row == 0)
            {
                zone_ = Zone::chips;
                feedback.play(audio::Cue::focus, 1.1f);
            }
            else
                focus_ -= kColumns;
            break;
        case Direction::down:
            if (focus_ + kColumns < count)
                focus_ += kColumns;
            else if ((count - 1) / kColumns > row)
                focus_ = count - 1; // a short last row: land on its last card
            else
                refuse(feedback, input.nav_repeat, 0.0f, 1.0f);
            break;
        case Direction::none:
            break;
        }
        if (focus_ != before) // rows further down sound a little lower
            feedback.play(audio::Cue::focus,
                          std::max(0.82f, 1.06f - 0.04f * static_cast<float>(focus_ / kColumns)),
                          ui::pan_for_x(card_rect(focus_).cx()));
    }
    else if (zone_ == Zone::banner)
    {
        if (input.nav == Direction::left || input.nav == Direction::right)
        {
            // The banner is a loop: it has no ends to refuse at.
            const int step = input.nav == Direction::right ? 1 : -1;
            show_banner((banner_ + step + featured) % featured);
            feedback.play(audio::Cue::slider, 1.0f + 0.04f * static_cast<float>(banner_));
        }
        else if (input.nav == Direction::up)
            refuse(feedback, input.nav_repeat, 0.0f, -1.0f);
        else if (input.nav == Direction::down)
        {
            zone_ = Zone::chips;
            feedback.play(audio::Cue::focus, 1.1f);
        }
    }
    else if (input.nav == Direction::left || input.nav == Direction::right)
        step_section(input.nav == Direction::right ? 1 : -1, input.nav_repeat, feedback);
    else if (input.nav == Direction::up)
    {
        if (banner_shown())
        {
            zone_ = Zone::banner;
            feedback.play(audio::Cue::focus, 1.16f);
        }
        else
            refuse(feedback, input.nav_repeat, 0.0f, -1.0f);
    }
    else if (input.nav == Direction::down)
    {
        if (count == 0)
            refuse(feedback, input.nav_repeat, 0.0f, 1.0f);
        else
        {
            zone_ = Zone::grid;
            feedback.play(audio::Cue::focus);
        }
    }

    if (input.is_pressed(Action::confirm))
    {
        if (zone_ == Zone::banner)
        {
            // The banner's title opens where it stands in Discover.
            const auto wanted = featured_[static_cast<std::size_t>(banner_)];
            const auto found = std::find(visible_.begin(), visible_.end(), wanted);
            if (found == visible_.end())
                return refuse(feedback, false, 0.0f, 1.0f);
            focus_ = static_cast<int>(found - visible_.begin());
            zone_ = Zone::grid;
        }
        if (zone_ == Zone::grid && count > 0)
        {
            details_ = true;
            press_.trigger();
            pending_detail = focused()->local_only ? std::string{} : focused()->title_id;
            refresh_detail();
            feedback.play(audio::Cue::open);
        }
        else if (count > 0)
        {
            zone_ = Zone::grid;
            feedback.play(audio::Cue::select);
        }
        else
            refuse(feedback, false, 0.0f, 1.0f);
    }
    else if (input.is_pressed(Action::back))
    {
        // Back has one step to take first: from deep in the grid to its top.
        if (zone_ == Zone::grid && focus_ >= kColumns)
            focus_ %= kColumns;
        else if (!activity_.id.empty() || !activity_.waiting.empty())
        {
            ask_ = Ask::quit;
            dialog_.open({ui::StatusKind::warning,
                          "Close ProsperoStore?",
                          "An app is still being installed. Closing now cancels it; nothing "
                          "half-installed is left behind.",
                          {{"Keep installing"}, {"Close", ui::ButtonKind::primary, true}}},
                         feedback);
            return;
        }
        else
            quit_ = true;
        feedback.play(audio::Cue::back);
    }
}

void Screen::update_page(const InputFrame &input, ui::Feedback &feedback)
{
    const App &shown = *focused();
    const Offer state = offer(shown);
    if (input.is_pressed(Action::confirm) && state.primary != Order::Kind::none)
    {
        if (!state.armed)
        {
            // The line under the button already says why.
            feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
            feedback.rumble(0.25f, 0.05f);
            nudge_.trigger();
        }
        else if (state.primary == Order::Kind::uninstall)
            ask_uninstall(shown, feedback);
        else if (state.primary == Order::Kind::adopt)
        {
            ask_ = Ask::adopt;
            ask_id_ = shown.title_id;
            dialog_.open({ui::StatusKind::question,
                          "Manage " + shown.name + " with ProsperoStore?",
                          "Nothing is changed now. From then on its updates are offered here, and "
                          "an update replaces the app's whole folder: anything you added inside "
                          "that folder is lost with it.",
                          {{"Cancel"}, {"Manage", ui::ButtonKind::primary}}},
                         feedback);
        }
        else
        {
            press_.trigger();
            feedback.play(audio::Cue::select);
            order(shown, state.primary);
        }
    }
    else if (input.is_pressed(Action::west) && state.uninstall && installer_ && guard_)
        ask_uninstall(shown, feedback);
    else if (input.is_pressed(Action::back))
    {
        details_ = false;
        feedback.play(audio::Cue::back);
    }
    else if (input.is_pressed(Action::north))
    {
        auto &app = apps_[visible_[static_cast<std::size_t>(focus_)]];
        app.detail_error.clear();
        pending_detail = app.local_only ? std::string{} : app.title_id;
        refresh_detail();
        feedback.play(audio::Cue::select);
    }
    else
        article_.handle(input, feedback);
}

void Screen::update(const InputFrame &input, float dt, ui::Feedback &feedback)
{
    time_ += dt;
    // A scripted install presses the button as soon as it can be pressed.
    if (!auto_order_.empty() && details_ && focused() && focused()->title_id == auto_order_)
    {
        const Offer state = offer(*focused());
        if (state.armed && !state.busy && state.primary == Order::Kind::install)
        {
            order(*focused(), Order::Kind::install);
            auto_order_.clear();
        }
    }
    fresh_catalog_ = false;
    // "Update all" hands the installer one app at a time: each needs its
    // verified details first, and one that can't be updated now is passed over.
    if (!update_all_.empty() && pending_order.kind == Order::Kind::none)
    {
        const auto &id = update_all_.front();
        const auto found = std::find_if(apps_.begin(), apps_.end(),
                                        [&](const auto &app) { return app.title_id == id; });
        if (found == apps_.end() || found->badge != "Update" || !found->detail_error.empty())
            update_all_.erase(update_all_.begin());
        else if (!found->detail)
        {
            if (update_asked_ != id && pending_detail.empty())
                pending_detail = update_asked_ = id;
        }
        else
        {
            const Offer state = offer(*found);
            if (state.armed && !state.busy && state.primary == Order::Kind::install)
                order(*found, Order::Kind::install);
            update_all_.erase(update_all_.begin());
        }
    }
    if (dialog_.is_open())
    {
        // The question takes every input until it is answered.
        if (dialog_.handle(input, feedback) == ui::Event::activated && dialog_.choice() == 1)
        {
            if (ask_ == Ask::quit)
                quit_ = true;
            else
                for (const auto &app : apps_)
                    if (app.title_id == ask_id_ && !app.installed.empty())
                        order(app,
                              ask_ == Ask::adopt ? Order::Kind::adopt : Order::Kind::uninstall);
        }
    }
    else if (panel_)
        update_panel(input, feedback);
    else if (input.is_pressed(Action::menu) ||
             (input.is_pressed(Action::west) && !details_ && section_ != 6))
    {
        // Options opens the settings; Square, the queue.
        open_panel(input.is_pressed(Action::menu) ? 1 : 0);
        feedback.play(audio::Cue::open);
    }
    else if (details_ && focused())
        update_page(input, feedback);
    else
    {
        details_ = false;
        update_home(input, feedback);
    }
    panel_value_.target = panel_ ? 1.0f : 0.0f;
    panel_value_.update(dt, 14.0f);
    about_.set_active(panel_ && panel_tab_ == 2);
    about_.update(dt);

    // The banner moves on by itself while nothing holds it.
    const bool home = !details_ && banner_shown();
    if (home && zone_ != Zone::banner && featured_.size() > 1)
    {
        banner_clock_ += dt;
        if (banner_clock_ >= kBannerSeconds)
            show_banner((banner_ + 1) % static_cast<int>(featured_.size()));
    }
    banner_fade_.update(dt);
    swap_.update(dt);
    banner_focus_.target = zone_ == Zone::banner ? 1.0f : 0.0f;
    banner_focus_.update(dt, 16.0f);
    scroll_.target = scroll_target();
    scroll_.update(dt, 11.0f);
    page_.target = details_ ? 1.0f : 0.0f;
    page_.update(dt, 13.0f);
    plate_.target = zone_ == Zone::grid && !visible_.empty() ? 1.0f : 0.0f;
    plate_.update(dt, 18.0f);
    ring_.target(focus_target());
    ring_.update(dt, 20.0f);
    ring_radius_.target = focus_radius();
    ring_radius_.update(dt, 20.0f);
    chip_pill_.target(chips_[section_]);
    chip_pill_.update(dt, 18.0f);
    nudge_.update(dt, 9.0f);
    press_.update(dt, 7.0f);
    const App *lifted = zone_ == Zone::grid ? focused() : nullptr;
    for (std::size_t i = 0; i < lift_.size(); ++i)
    {
        lift_[i].target = lifted == &apps_[i] ? 1.0f : 0.0f;
        if (!lift_[i].settled())
            lift_[i].update(dt, 16.0f);
        // A picture fades in over the plate that stood in for it.
        appear_[i].target = art(apps_[i]) ? 1.0f : 0.0f;
        if (!appear_[i].settled())
            appear_[i].update(dt, 9.0f);
    }
    article_.set_active(details_);
    article_.update(dt);
    dialog_.update(dt);
    toasts_.update(dt, feedback);
}

// ---- drawing: the home page ---------------------------------------------------

// Fitting and measuring allocate: once per catalog, not per frame.
void Screen::layout(const ui::Fonts &fonts)
{
    if (fits_stale_)
    {
        fits_.assign(apps_.size(), {});
        fits_stale_ = false;
    }
    float x = kMargin + ui::button_width(ui::Button::l1, 30) + 16.0f;
    for (int i = 0; i < kSections; ++i)
    {
        const auto number = std::to_string(counts_[i]);
        const float w = 48.0f + fonts.semibold.measure(kSectionNames[i], 22) + 10.0f +
                        fonts.mono.measure(number, 18);
        chips_[i] = {x, chips_y(), w, kChipH};
        x += w + 12.0f;
    }
    if (!placed_)
    {
        // The first frame: the highlights start where they belong, not at the origin.
        chip_pill_.snap(chips_[section_]);
        ring_.snap(focus_target());
        ring_radius_.snap(focus_radius());
        placed_ = true;
    }
}

// The app the installer is working on, for the top bar.
const App *Screen::busy_app(float &progress, const char *&phase) const
{
    if (activity_.id.empty())
        return nullptr;
    for (const auto &app : apps_)
        if (app.title_id == activity_.id)
        {
            using Phase = install::Phase;
            const auto now = static_cast<Phase>(activity_.phase);
            phase = now == Phase::downloading ? "Downloading"
                    : now == Phase::verifying ? "Verifying"
                    : now == Phase::unpacking ? "Unpacking"
                    : now == Phase::removing  ? "Removing"
                                              : "Installing";
            const bool measured =
                (now == Phase::downloading || now == Phase::unpacking) && activity_.total > 0;
            progress = measured ? tween::clamp01(static_cast<float>(activity_.done) /
                                                 static_cast<float>(activity_.total))
                                : -1.0f;
            return &app;
        }
    return nullptr;
}

void Screen::draw_top_bar(const ui::Fonts &fonts)
{
    auto &list = scene_;
    list.rotated_rect({kMargin + 2.0f, kTopY - 11.0f, 22.0f, 22.0f}, 5.0f, 0.7854f, kAccent);
    list.rotated_rect({kMargin + 8.0f, kTopY - 5.0f, 10.0f, 10.0f}, 2.0f, 0.7854f, kDeep);
    const float brand = ui::text(list, fonts.semibold, "PROSPEROSTORE", kMargin + 42.0f,
                                 centred(kTopY, 22), 22, kInk, gfx::Align::left, 5.0f);
    // Where the apps come from, said once and quietly beside the name.
    const float x = kMargin + 42.0f + brand + 22.0f;
    list.rounded_rect({x, kTopY - 11.0f, 1.5f, 22.0f}, 0, kInk.with_alpha(0.25f));
    const float words = ui::text(list, fonts.regular, "Apps from ", x + 22.0f, centred(kTopY, 22),
                                 22, kInk.with_alpha(0.62f));
    ui::text(list, fonts.semibold, "homebrew.page", x + 22.0f + words, centred(kTopY, 22), 22,
             kAccent);
    float progress = -1.0f;
    const char *phase = "";
    if (const App *busy = busy_app(progress, phase))
    {
        // A transaction is running: a turning arc, what it is doing, and to whom.
        std::string line = std::string(phase) + " " + busy->name;
        if (progress >= 0.0f)
            line += "  " + std::to_string(static_cast<int>(progress * 100.0f)) + "%";
        const float width =
            ui::text(list, fonts.semibold, fonts.semibold.font->fit(line, 22, 760.0f), kRight,
                     centred(kTopY, 22), 22, kInk, gfx::Align::right);
        const float cx = kRight - width - 28.0f;
        list.ring(cx, kTopY, 12.0f, 3.0f, kInk.with_alpha(0.16f));
        list.arc(cx, kTopY, 12.0f, 3.0f, time_ * 4.0f, 1.9f, kAccent);
    }
    else
        ui::text(list, fonts.regular, fonts.regular.font->fit(status_, 22, 900.0f), kRight,
                 centred(kTopY, 22), 22, kInk.with_alpha(0.62f), gfx::Align::right);
}

void Screen::draw_banner(const ui::Fonts &fonts)
{
    // The banner fades as the page scrolls it under the top bar.
    const float visible = tween::clamp01(1.0f - scroll_.value / 240.0f);
    if (!banner_shown() || visible <= 0.004f)
        return;
    auto &list = scene_;
    const Rect b{kMargin, kBannerY - scroll_.value, kWidth - 2.0f * kMargin, kBannerH};
    const float fade = banner_fade_.running ? tween::smoothstep(banner_fade_.progress()) : 1.0f;
    const Color tone = gfx::mix(kPanel, kDeep, 0.5f);

    list.push_opacity(visible);
    list.shadow({b.x, b.y + 18.0f, b.w, b.h}, kBannerRadius, 44, kBlack.with_alpha(0.5f));
    list.gradient_rect_h(b, kBannerRadius, tone, gfx::mix(tone, kMid, 0.45f));
    list.bordered_rect(b, kBannerRadius, kClear, 1.5f, kInk.with_alpha(0.1f));

    // One featured title at an opacity, so two of them can cross-fade.
    const auto slot = [&](int index, float alpha)
    {
        if (alpha <= 0.01f)
            return;
        const App &app = apps_[featured_[static_cast<std::size_t>(index)]];
        const float x = b.x + 56.0f;
        list.push_opacity(alpha);
        // The artwork floats on the right with a little of the accent's light.
        const Rect cover{b.x + b.w - 56.0f - 228.0f, b.y + 36.0f, 228.0f, 228.0f};
        list.glow(cover.inset(24.0f), 40, 110, kAccent.with_alpha(0.16f));
        list.shadow({cover.x, cover.y + 14.0f, cover.w, cover.h}, 26, 36, kBlack.with_alpha(0.5f));
        if (const auto texture = art(app, true))
            list.image(texture, cover, gfx::kFullUv, kWhite, 26);
        else
            list.rounded_rect(cover, 26, kInk.with_alpha(0.06f));
        list.bordered_rect(cover, 26, kClear, 1.5f, kInk.with_alpha(0.16f));

        const char *kicker = app.badge == "Update"      ? "UPDATE AVAILABLE"
                             : app.badge == "Installed" ? "IN YOUR LIBRARY"
                             : index == 0               ? "NEW RELEASE"
                                                        : "FEATURED";
        list.rounded_rect({x, b.y + 55.0f, 28.0f, 3.0f}, 1.5f, kAccent);
        ui::text(list, fonts.semibold, kicker, x + 40.0f, b.y + 64.0f, 18, kAccent,
                 gfx::Align::left, 4.0f);
        ui::text(list, fonts.display, fonts.display.font->fit(app.name, 62, 1100.0f), x - 3.0f,
                 b.y + 142.0f, 62, kInk);
        std::string line = app.author;
        if (!app.version.empty())
            line += (line.empty() ? "" : "  \xC2\xB7  ") + app.version;
        ui::text(list, fonts.regular, fonts.regular.font->fit(line, 26, 1000.0f), x, b.y + 186.0f,
                 26, kInk.with_alpha(0.78f));
        list.pop_opacity();
    };
    if (fade < 1.0f)
        slot(banner_previous_, 1.0f - fade);
    slot(banner_, fade);

    // The call to action lights up when the banner has the focus.
    const float lit = banner_focus_.value;
    const Rect action{b.x + 56.0f, b.y + 214.0f, 214.0f, 52.0f};
    list.bordered_rect(action, 26, gfx::mix(kInk.with_alpha(0.06f), kAccent, lit), 1.5f,
                       gfx::mix(kInk.with_alpha(0.3f), kAccent, lit));
    ui::text(list, fonts.semibold, "View details", action.cx(), centred(action.cy(), 22), 22,
             gfx::mix(kInk.with_alpha(0.86f), kOnAccent, lit), gfx::Align::center);

    // Page dots: the current one is a small bar that fills as its seconds pass.
    const int count = static_cast<int>(featured_.size());
    float x = b.x + 300.0f;
    const float cy = action.cy();
    for (int i = 0; i < count && count > 1; ++i)
    {
        if (i == banner_)
        {
            list.rounded_rect({x, cy - 4.0f, 44.0f, 8.0f}, 4, kInk.with_alpha(0.3f));
            list.rounded_rect(
                {x, cy - 4.0f, 8.0f + 36.0f * tween::clamp01(banner_clock_ / kBannerSeconds), 8.0f},
                4, kInk);
            x += 56.0f;
        }
        else
        {
            list.circle(x + 4.0f, cy, 4.0f, kInk.with_alpha(0.42f));
            x += 20.0f;
        }
    }
    list.pop_opacity();
}

void Screen::draw_chips(const ui::Fonts &fonts)
{
    auto &list = scene_;
    const auto glyphs = ui::GlyphStyle::dark();
    const float cy = chips_y() + kChipH * 0.5f;
    ui::draw_button(list, fonts, glyphs, ui::Button::l1, kMargin, cy, 30);
    // The active section is one pill that glides between the chips.
    Rect pill = chip_pill_.value();
    pill.y = chips_y();
    if (zone_ != Zone::grid)
        pill.x += ui::shake(nudge_.value, time_) * nudge_x_;
    list.rounded_rect(pill, kChipH * 0.5f, kInk);
    // Outlines, then labels, then counts: three draw calls for the row.
    for (int pass = 0; pass < 3; ++pass)
        for (int i = 0; i < kSections; ++i)
        {
            const Rect &chip = chips_[i];
            // How much of this chip the pill covers decides its ink.
            const float overlap =
                std::min(pill.x + pill.w, chip.x + chip.w) - std::max(pill.x, chip.x);
            const float on = tween::clamp01(overlap / chip.w);
            const Color ink = gfx::mix(kInk.with_alpha(0.78f), kCoal, on);
            if (pass == 0)
                list.bordered_rect(chip, kChipH * 0.5f, kClear, 1.5f,
                                   kInk.with_alpha(0.2f * (1.0f - on)));
            else if (pass == 1)
                ui::text(list, fonts.semibold, kSectionNames[i], chip.x + 24.0f, centred(cy, 22),
                         22, ink);
            else
                ui::text(list, fonts.mono, std::to_string(counts_[i]),
                         chip.x + 34.0f + fonts.semibold.measure(kSectionNames[i], 22),
                         centred(cy, 18), 18, ink.with_alpha(0.6f));
        }
    const Rect &last = chips_[kSections - 1];
    ui::draw_button(list, fonts, glyphs, ui::Button::r1, last.x + last.w + 16.0f, cy, 30);
    const char *sort_name = sort_ == Sort::name       ? "Name"
                            : sort_ == Sort::released ? "Newest release"
                                                      : "Recently updated";
    ui::text(list, fonts.regular,
             query_.empty() ? std::string("Sorted by ") + sort_name
                            : fonts.regular.font->fit("Results for \"" + query_ + "\"", 22, 420.0f),
             kRight, centred(cy, 22), 22, kInk.with_alpha(0.62f), gfx::Align::right);
}

// One card: cover, mark, title and a second line. The cover's tint runs from
// the visibility at its top to the one at its bottom, which gives the grid a
// soft edge without painting over the backdrop.
void Screen::draw_card(const ui::Fonts &fonts, int k, unsigned layers)
{
    Rect r = card_rect(k);
    r.y -= scroll_.value;
    if (r.y > kViewBottom || r.y + r.h < kPageTop)
        return;
    auto &list = scene_;
    const std::size_t index = visible_[static_cast<std::size_t>(k)];
    const App &app = apps_[index];
    // Fitting allocates: once per card, the first time it is drawn.
    if (auto &fit = fits_[index]; !fit.ready)
        fit = {fonts.semibold.font->fit(app.name, 24, kCardW - 20.0f),
               fonts.regular.font->fit(app.author, 20, kCardW - 20.0f), true};
    const float lift = lift_[index].value;
    const float in = swap_.running ? tween::stagger(swap_.elapsed, k, 0.02f, 0.25f) : 1.0f;
    const float shake = k == focus_ && zone_ == Zone::grid ? ui::shake(nudge_.value, time_) : 0.0f;
    list.push_transform(1.0f + kLift * lift, r.cx(), r.cy(), shake * nudge_x_,
                        shake * nudge_y_ + 20.0f * (1.0f - in));
    const Rect cover{r.x, r.y, r.w, kCoverH};
    const float top = in * fade_at(cover.y + 1.0f), foot = in * fade_at(cover.y + cover.h);
    const float light = 0.86f + 0.14f * lift; // resting covers sit back a little
    const float shown = appear_[index].value, plate = 1.0f - shown;
    if (const auto texture = art(app); texture && (layers & kImages))
        list.image_gradient(texture, cover, gfx::kFullUv, Color{light, light, light, top * shown},
                            Color{light, light, light, foot * shown}, kCardRadius);
    if (plate > 0.01f && (layers & kShapes))
    {
        // No picture yet: a quiet plate with the store's diamond.
        list.gradient_rect(cover, kCardRadius, kInk.with_alpha(0.07f * top * plate),
                           kMid.with_alpha(0.16f * foot * plate));
        list.rotated_rect({cover.cx() - 22.0f, cover.cy() - 22.0f, 44.0f, 44.0f}, 9.0f, 0.7854f,
                          kInk.with_alpha(0.12f * std::min(top, foot) * plate));
    }
    // A transaction for this app shows on its card: what, and how far.
    const bool queued = std::find(activity_.waiting.begin(), activity_.waiting.end(),
                                  app.title_id) != activity_.waiting.end();
    const bool working = activity_.id == app.title_id;
    if ((working || queued) && foot > 0.01f)
    {
        const Offer state = offer(app);
        if (layers & kShapes)
        {
            const Rect track{cover.x + 12.0f, cover.y + cover.h - 20.0f, cover.w - 24.0f, 8.0f};
            list.rounded_rect(track, 4, kBlack.with_alpha(0.55f * foot));
            if (state.progress >= 0.0f)
                list.rounded_rect(
                    {track.x, track.y, std::max(8.0f, track.w * state.progress), 8.0f}, 4,
                    kAccent.with_alpha(foot));
        }
        pill(list, fonts, state.headline, r.x + 10.0f, r.y + 28.0f, 34.0f, kAccent, kOnAccent,
             in * fade_at(r.y + 26.0f), layers);
    }
    const float marks = in * fade_at(r.y + 26.0f);
    // The rocket picture already says "coming soon".
    const bool said = !app.icon && coming_soon_art_ && app.catalog_badge == "Coming soon";
    if (const auto state = mark(app);
        state.word && marks > 0.01f && !(said && !state.loud) && !working && !queued)
        pill(list, fonts, state.word, r.x + 10.0f, r.y + 28.0f, 34.0f,
             state.loud ? kAccent : kBlack.with_alpha(0.62f), state.loud ? kOnAccent : kInk, marks,
             layers);
    // On the focus plate the words step in from its edge.
    const float x = r.x + 2.0f + 8.0f * lift;
    const float title = in * fade_at(r.y + kCoverH + 36.0f);
    if (title > 0.01f && (layers & kSemibold))
        ui::text(list, fonts.semibold, fits_[index].title, x, r.y + kCoverH + 36.0f, 24,
                 kInk.with_alpha(title * (0.84f + 0.16f * lift)));
    const float second = in * fade_at(r.y + kCoverH + 68.0f);
    if (second > 0.01f && app.badge == "Installed")
    {
        if (layers & kShapes)
            draw_check(list, x + 9.0f, r.y + kCoverH + 61.0f, 16.0f, kOwned.with_alpha(second));
        if (layers & kSemibold)
            ui::text(list, fonts.semibold, "Installed", x + 26.0f, r.y + kCoverH + 68.0f, 20,
                     kOwned.with_alpha(second));
    }
    else if (second > 0.01f && (layers & kRegular))
        ui::text(list, fonts.regular, fits_[index].author, x, r.y + kCoverH + 68.0f, 20,
                 kInk.with_alpha(0.6f * second));
    list.pop_transform();
}

void Screen::draw_grid(const ui::Fonts &fonts)
{
    auto &list = scene_;
    const Rect window{0, window_top(), kWidth, kViewBottom - window_top()};
    const int count = static_cast<int>(visible_.size());
    const int focused = zone_ == Zone::grid && count > 0 ? focus_ : -1;
    list.push_clip(window);
    int begin = 0, end = 0;
    rows_in_view(scroll_.value, begin, end);
    for (const unsigned layer : {kImages, kShapes, kSemibold, kRegular})
        for (int k = begin; k < end; ++k)
            if (k != focused) // that one is drawn last, on top of its neighbours
                draw_card(fonts, k, layer);
    if (count == 0)
    {
        const bool library = section_ == 5;
        const char *title = library && !inventory_ready_      ? "Checking your library"
                            : library && !inventory_.complete ? "Library unavailable"
                            : !query_.empty()                 ? "No matches"
                            : library                         ? "No installed apps found"
                            : section_ == 6                   ? "Everything is up to date"
                            : apps_.empty()                   ? "The catalog is on its way"
                                                              : "Nothing on this shelf right now";
        const char *note =
            library && !inventory_ready_ ? "Reading installed apps and receipts."
            : library && !inventory_.complete
                ? "Installed apps are unavailable until their locations can be read."
            : !query_.empty() ? "Try another app name or developer."
            : section_ == 6   ? "Updates for apps installed by ProsperoStore appear here."
            : apps_.empty()   ? "Verified apps will appear here when the catalog is ready."
                              : "Try another section.";
        // While something is on its way an arc draws itself into a full
        // circle, fades, and starts again a little further round.
        const bool waiting =
            library ? !inventory_ready_ : apps_.empty() && loading_ && query_.empty();
        const float y = window.y + (kViewBottom - window.y) * (waiting ? 0.56f : 0.45f);
        if (waiting)
        {
            constexpr float kTurn = 6.2831853f, kPeriod = 1.7f;
            const float t = std::fmod(time_, kPeriod) / kPeriod;
            const float sweep = kTurn * tween::cubic_in_out(t / 0.72f);
            const float alpha = 1.0f - tween::smoothstep((t - 0.8f) / 0.2f);
            const float start = std::floor(time_ / kPeriod) * 2.2f + time_ * 0.5f;
            const float cx = 960.0f, cy = y - 118.0f, radius = 46.0f;
            list.glow({cx - radius, cy - radius, radius * 2.0f, radius * 2.0f}, radius, 36,
                      kAccent.with_alpha(0.1f + 0.08f * alpha * sweep / kTurn));
            list.ring(cx, cy, radius, 6.0f, kInk.with_alpha(0.12f));
            list.arc(cx, cy, radius, 6.0f, start, std::max(0.02f, sweep),
                     kAccent.with_alpha(alpha));
        }
        title = waiting && !library ? "Loading the catalog" : title;
        ui::text(list, fonts.semibold, title, 960, y, 30, kInk.with_alpha(0.86f),
                 gfx::Align::center);
        ui::text(list, fonts.regular, note, 960, y + 42.0f, 24, kInk.with_alpha(0.6f),
                 gfx::Align::center);
    }
    list.pop_clip();

    // The focus highlight: one object that glides between the banner, the
    // chips and the cards. On a card it is also the plate and the light under it.
    Rect ring = ring_.value();
    const float shake = ui::shake(nudge_.value, time_);
    ring.x += shake * nudge_x_;
    ring.y += shake * nudge_y_ - scroll_.value;
    const float radius = ring_radius_.value;
    const float plate = plate_.value;
    if (plate > 0.01f)
    {
        list.shadow({ring.x, ring.y + 16.0f, ring.w, ring.h}, radius, 38,
                    kBlack.with_alpha(0.55f * plate));
        list.glow(ring, radius, 30, kAccent.with_alpha((0.2f + 0.1f * ui::breathe(time_)) * plate));
        list.rounded_rect(ring, radius, kPanel.with_alpha(plate));
    }
    list.bordered_rect(ring, radius, kClear, 3.0f, kInk.with_alpha(0.94f));
    if (focused >= 0)
    {
        list.push_clip(window);
        draw_card(fonts, focused, kAllLayers);
        list.pop_clip();
    }
}

// ---- drawing: the product page ----------------------------------------------

void Screen::order(const App &app, Order::Kind kind)
{
    pending_order = {};
    pending_order.kind = kind;
    pending_order.id = app.title_id;
    if (!app.installed.empty())
    {
        const auto &path = app.installed.front().path;
        pending_order.location = path.substr(0, path.find_last_of('/'));
    }
    else
        pending_order.location = settings_.location;
    if (app.title_id == self_id_)
        pending_order.location = self_location_;
    if (kind == Order::Kind::install && app.detail)
        pending_order.entry = *app.detail;
    pending_order.entry.id = app.title_id;
    if (pending_order.entry.name.empty())
        pending_order.entry.name = app.name;
}

bool Screen::open_app(const std::string &id)
{
    section_ = 0;
    query_.clear();
    rebuild();
    for (std::size_t k = 0; k < visible_.size(); ++k)
        if (apps_[visible_[k]].title_id == id)
        {
            focus_ = static_cast<int>(k);
            zone_ = Zone::grid;
            details_ = true;
            pending_detail = id;
            refresh_detail();
            return true;
        }
    return false;
}

void Screen::remote_install(const std::string &id)
{
    if (open_app(id))
        auto_order_ = id;
}

bool Screen::remote_uninstall(const std::string &id)
{
    for (const auto &app : apps_)
        if (app.title_id == id && !app.installed.empty() && offer(app).armed)
        {
            order(app, Order::Kind::uninstall);
            return true;
        }
    return false;
}

bool Screen::remote_adopt(const std::string &id)
{
    for (const auto &app : apps_)
        if (app.title_id == id)
        {
            const Offer state = offer(app);
            if (state.armed && state.primary == Order::Kind::adopt)
            {
                order(app, Order::Kind::adopt);
                return true;
            }
        }
    return false;
}

bool Screen::remote_order()
{
    if (!details_ || !focused() || !focused()->detail)
        return false;
    order(*focused(), Order::Kind::install);
    return true;
}

void Screen::set_installer(bool available, bool guard, std::string reason, std::string location)
{
    installer_ = available;
    guard_ = guard;
    installer_reason_ = std::move(reason);
    install_location_ = std::move(location);
}

void Screen::finish_job(bool ok, bool restart, std::string title, std::string body)
{
    const bool cancelled = title.ends_with(": cancelled");
    restart_needed_ = restart_needed_ || restart;
    history_.push_back({ok, title, body});
    if (history_.size() > 12)
        history_.erase(history_.begin());
    toasts_.push(ok          ? ui::StatusKind::success
                 : cancelled ? ui::StatusKind::info
                             : ui::StatusKind::danger,
                 std::move(title), std::move(body), 10.0f);
}

Screen::Offer Screen::offer(const App &app) const
{
    Offer out;
    const system::InstalledApp *installed = app.installed.empty() ? nullptr : &app.installed[0];
    const bool soon = app.catalog_badge == "Coming soon";
    const bool image = app.detail && !app.detail->format.empty() && app.detail->format != "zip";
    const auto megabytes = [](std::uint64_t bytes) { return size_text(bytes); };

    // A transaction for this app comes first: what it is doing, and Cancel.
    const bool waiting = std::find(activity_.waiting.begin(), activity_.waiting.end(),
                                   app.title_id) != activity_.waiting.end();
    if (activity_.id == app.title_id || waiting)
    {
        using Phase = install::Phase;
        const auto phase = waiting ? Phase::idle : static_cast<Phase>(activity_.phase);
        out.busy = true;
        out.tone = 1;
        out.headline = phase == Phase::downloading  ? "Downloading"
                       : phase == Phase::verifying  ? "Verifying"
                       : phase == Phase::unpacking  ? "Unpacking"
                       : phase == Phase::activating ? "Finishing"
                       : phase == Phase::removing   ? "Removing"
                                                    : "Waiting";
        const bool measured =
            (phase == Phase::downloading || phase == Phase::unpacking) && activity_.total > 0;
        if (measured)
        {
            out.progress = tween::clamp01(static_cast<float>(activity_.done) /
                                          static_cast<float>(activity_.total));
            out.note = megabytes(activity_.done) + " of " + megabytes(activity_.total);
        }
        else
            out.note = waiting ? "Queued behind another app" : "One moment";
        // Once the folder is being put in place there is nothing left to cancel.
        if (phase != Phase::activating && phase != Phase::removing)
        {
            out.label = "Cancel";
            out.primary = Order::Kind::cancel;
            out.armed = true;
        }
        out.reason = "The app's folder is only changed once everything is verified and unpacked.";
        return out;
    }

    if (app.title_id == self_id_ && !self_id_.empty())
    {
        // The store's own page: it can't be uninstalled from inside, and its
        // update is finished by restarting it.
        const bool newer = catalog::update_available(self_version_, app.available_version);
        out.headline = restart_needed_ ? "Restart to finish"
                       : newer         ? "Update"
                                       : "This is ProsperoStore";
        out.note = newer && !restart_needed_
                       ? self_version_ + "  \xE2\x86\x92  " + app.available_version
                       : "Version " + self_version_;
        out.tone = newer || restart_needed_ ? 1 : 0;
        out.reason = restart_needed_
                         ? "Close ProsperoStore and open it again to use the new version."
                     : newer ? ""
                             : "You are using it right now.";
        if (newer && !restart_needed_)
        {
            out.label = "Update";
            out.primary = Order::Kind::install;
            if (!installer_)
                out.reason = installer_reason_;
            else if (!app.detail)
                out.reason = "Waiting for the app's verified details.";
            else
            {
                out.armed = true;
                out.reason = "The new version is put in place now and starts the next time "
                             "ProsperoStore is opened.";
            }
        }
        return out;
    }
    if (installed && app.badge == "Update")
    {
        out.headline = "Update";
        out.note = installed->version + "  \xE2\x86\x92  " + app.available_version;
        out.label = "Update";
        out.primary = Order::Kind::install;
        out.uninstall = true;
        out.tone = 1;
    }
    else if (installed)
    {
        out.headline = "Installed";
        out.note =
            installed->version.empty() ? "Installed as an image" : "Version " + installed->version;
        out.tone = 2;
        if (installed->managed)
        {
            out.label = "Uninstall";
            out.primary = Order::Kind::uninstall;
        }
        else if (!app.local_only && !installed->image && !installed->duplicate &&
                 installed->path.ends_with("/" + app.title_id))
        {
            // Installed by hand, and listed in the catalog: it can be handed over.
            out.label = "Manage with ProsperoStore";
            out.primary = Order::Kind::adopt;
            out.armed = installer_;
            out.reason = installer_ ? "Installed outside ProsperoStore. Hand it over to get its "
                                      "updates here."
                                    : installer_reason_;
            return out;
        }
        else
            out.reason = installed->reason;
    }
    else if (soon)
    {
        out.headline = "Coming soon";
        out.note = "No release has been published yet";
        out.reason = "It will appear here when it is released";
    }
    else if (image)
    {
        out.headline = "Not installable";
        out.note = "Published as a disk image";
        out.reason = "Can't be installed by this version of ProsperoStore";
    }
    else if (!app.local_only)
    {
        out.headline = "Ready";
        out.note = app.detail && app.detail->size ? megabytes(app.detail->size) + " download"
                                                  : "Verified by the signed catalog";
        out.label = "Install";
        out.primary = Order::Kind::install;
    }
    if (out.primary == Order::Kind::none)
        return out;
    if (installed && std::find(running_.begin(), running_.end(), app.title_id) != running_.end())
    {
        out.reason = app.name + " is running. Close it first.";
        return out;
    }
    // Why the button rests, or what pressing it does.
    const bool changes = installed != nullptr; // an update or an uninstall
    if (!installer_)
        out.reason = installer_reason_;
    else if (changes && !guard_)
        out.reason = "Needs the running-app check, which this build doesn't have yet.";
    else if (out.primary == Order::Kind::install && !app.detail)
        out.reason = "Waiting for the app's verified details.";
    else
    {
        out.armed = true;
        out.reason = out.primary == Order::Kind::uninstall
                         ? "Removes the app's folder. Its saved data stays on the console."
                         : "Checked against the signed catalog before anything is installed.";
    }
    return out;
}

void Screen::ask_uninstall(const App &app, ui::Feedback &feedback)
{
    ask_ = Ask::uninstall;
    ask_id_ = app.title_id;
    dialog_.open({ui::StatusKind::warning,
                  "Uninstall " + app.name + "?",
                  "The app's folder is removed from this console. Its saved data stays.",
                  {{"Cancel"}, {"Uninstall", ui::ButtonKind::primary, true}}},
                 feedback);
}

void Screen::draw_action_box(const ui::Fonts &fonts, std::uint32_t glass, const App &app,
                             float content)
{
    auto &list = overlay_;
    Rect box = kActionBox;
    box.y += 30.0f * (1.0f - content);
    list.shadow({box.x, box.y + 20.0f, box.w, box.h}, 30, 50, kBlack.with_alpha(0.45f));
    // Frosted glass: the blurred screen, a tint, then a hairline of light.
    list.glass(glass, box, 30, kWhite);
    list.rounded_rect(box, 30, gfx::mix(kPanel, kMid, 0.12f).with_alpha(0.6f));
    list.bordered_rect(box, 30, kClear, 1.5f, kInk.with_alpha(0.2f));

    const Offer state = offer(app);
    const Color tone = state.tone == 1 ? kAccent : state.tone == 2 ? kOwned : kInk;
    const float x = box.x + 32.0f;
    ui::text(list, fonts.semibold, "ON THIS CONSOLE", x, box.y + 52.0f, 16, kInk.with_alpha(0.55f),
             gfx::Align::left, 3.5f);
    float cursor = x;
    if (state.tone == 2)
    {
        draw_check(list, x + 20.0f, box.y + 113.0f, 34.0f, tone);
        cursor += 52.0f;
    }
    ui::text(list, fonts.display, fonts.display.font->fit(state.headline, 48, box.w - 64.0f),
             cursor, box.y + 130.0f, 48, tone);
    ui::text(list, fonts.regular, fonts.regular.font->fit(state.note, 21, box.w - 64.0f), x,
             box.y + 172.0f, 21, kInk.with_alpha(0.62f));
    if (state.progress >= 0.0f)
    {
        const Rect track{x, box.y + 188.0f, box.w - 64.0f, 6.0f};
        list.rounded_rect(track, 3, kInk.with_alpha(0.16f));
        list.rounded_rect({track.x, track.y, std::max(6.0f, track.w * state.progress), track.h}, 3,
                          kAccent);
    }

    // The primary button: filled when it can be pressed, at rest when it
    // can't, and the line under it always says why.
    Rect button = kActionButton;
    button.y += box.y - kActionBox.y;
    button.x += ui::shake(nudge_.value, time_, 10.0f) * (details_ ? 1.0f : 0.0f);
    if (!state.label.empty())
    {
        list.push_transform(1.0f - 0.035f * press_.value, button.cx(), button.cy(), 0, 0);
        if (state.armed && !state.busy)
        {
            list.glow(button, kButtonRadius, 22,
                      kAccent.with_alpha(0.2f + 0.1f * ui::breathe(time_)));
            list.rounded_rect(button, kButtonRadius, kAccent);
        }
        else
            list.bordered_rect(button, kButtonRadius, kAccent.with_alpha(0.1f), 2.0f,
                               kAccent.with_alpha(state.armed ? 1.0f : 0.55f));
        ui::text(list, fonts.semibold, state.label, button.cx(), centred(button.cy(), 28), 28,
                 state.armed && !state.busy ? kOnAccent
                                            : kAccent.with_alpha(state.armed ? 1.0f : 0.7f),
                 gfx::Align::center);
        list.pop_transform();
    }
    ui::paragraph(list, fonts.regular, state.reason, x,
                  button.y + (state.label.empty() ? 30.0f : 118.0f), 20, box.w - 64.0f, 28,
                  kInk.with_alpha(0.62f), 2);
    ui::paragraph(list, fonts.regular,
                  "Apps are provided by their developers. Check the release notes for required "
                  "payloads or extra setup.",
                  x, box.y + box.h - 92.0f, 18, box.w - 64.0f, 26, kInk.with_alpha(0.45f), 3);
}

void Screen::draw_page(const ui::Fonts &fonts, std::uint32_t glass)
{
    const float t = page_.value;
    const App *shown = focused();
    if (t <= 0.004f || !shown)
        return;
    auto &list = overlay_;
    const App &app = *shown;

    // The whole home page, blurred and darkened, is the page's wall.
    const Rect full{0, 0, kWidth, kHeight};
    const float veil = tween::clamp01(t * 1.7f);
    list.glass(glass, full, 0, kWhite.with_alpha(veil));
    list.rounded_rect(full, 0, gfx::mix(kCoal, kDeep, 0.4f).with_alpha(0.86f * veil));
    list.glow(kPreview.inset(60.0f), 60, 220, kAccent.with_alpha(0.08f * veil));

    // Everything but the cover fades in once the cover is well on its way.
    const float content = tween::smoothstep((t - 0.45f) / 0.55f);
    const float slide = 56.0f * (1.0f - tween::cubic_out(t));
    list.push_opacity(content);
    list.push_transform(1.0f, 0, 0, slide, 0);
    ui::text(list, fonts.semibold, ui::upper(app.local_only ? "installed" : app.kind), kInfoX, 170,
             20, kAccent, gfx::Align::left, 4.0f);
    ui::text(list, fonts.display, fonts.display.font->fit(app.name, 72, kRight - kInfoX),
             kInfoX - 4.0f, 250, 72, kInk);
    std::string line = app.author;
    if (!app.version.empty())
        line += (line.empty() ? "" : "  \xC2\xB7  ") + app.version;
    ui::text(list, fonts.regular, fonts.regular.font->fit(line, 26, kRight - kInfoX), kInfoX, 300,
             26, kInk.with_alpha(0.72f));
    list.rounded_rect({kInfoX, 346, kInfoW, 1.5f}, 0, kInk.with_alpha(0.12f));
    ui::Canvas canvas{list, fonts, glass, time_};
    article_.draw(canvas);
    list.pop_transform();

    draw_action_box(fonts, glass, app, content);

    // The QR code for the app's page, under the cover.
    if (qr_texture_ != 0 && qr_id_ == app.title_id)
    {
        const Rect code{kPreview.x, 760.0f, 200.0f, 200.0f};
        list.rounded_rect(code.inset(-8.0f), 14, kWhite);
        list.image(qr_texture_, code, gfx::kFullUv, kWhite);
        ui::paragraph(list, fonts.regular, "Scan for the app's page, release notes and source",
                      code.x + 240.0f, 836.0f, 24, 330, 34, kInk.with_alpha(0.86f), 2);
        ui::text(list, fonts.mono, "homebrew.page/app/" + app.title_id, code.x + 240.0f, 920.0f, 20,
                 kInk.with_alpha(0.6f));
    }
    list.pop_opacity();

    // The cover grows from the card it was opened on to the preview.
    Rect from = card_rect(focus_);
    from.y -= scroll_.value;
    from.h = kCoverH;
    const Rect cover{tween::lerp(from.x, kPreview.x, t), tween::lerp(from.y, kPreview.y, t),
                     tween::lerp(from.w, kPreview.w, t), tween::lerp(from.h, kPreview.h, t)};
    const float radius = tween::lerp(kCardRadius, kPreviewRadius, t);
    const float alpha = tween::clamp01(t * 14.0f);
    list.shadow({cover.x, cover.y + 26.0f * t, cover.w, cover.h}, radius, 56,
                kBlack.with_alpha(0.55f * veil));
    if (const auto texture = art(app, true))
        list.image(texture, cover, gfx::kFullUv, kWhite.with_alpha(alpha), radius);
    else
        list.gradient_rect(cover, radius, kInk.with_alpha(0.07f * alpha),
                           kMid.with_alpha(0.2f * alpha));
    list.bordered_rect(cover, radius, kClear, 1.5f, kInk.with_alpha(0.16f * alpha));
}

void Screen::draw(gfx::Renderer &renderer, const ui::Fonts &fonts)
{
    layout(fonts);
    scene_.clear();
    overlay_.clear();
    const float page = page_.value;

    // Anything opened over the home page pushes it back a little.
    scene_.push_transform(1.0f - 0.03f * page, 960, 540, 0, 0);
    draw_top_bar(fonts);
    scene_.push_clip({0, kPageTop, kWidth, kHeight - kPageTop});
    draw_banner(fonts);
    draw_grid(fonts);
    draw_chips(fonts);
    scene_.pop_clip();
    scene_.pop_transform();

    const auto glyphs = ui::GlyphStyle::dark();
    // On the Updates shelf Square takes them all.
    const bool all = section_ == 6 && !visible_.empty() && installer_ && guard_;
    const ui::Hint home[] = {
        {ui::Button::cross, "Details"},
        {all ? ui::Button::square : ui::Button::triangle, all ? "Update all" : "Search"},
        {ui::Button::right_stick, "Sort"},
        {ui::Button::l1, "Sections", ui::Button::r1},
        {ui::Button::circle, "Close"}};
    // The page's row names the one thing Cross does for this app, when it can.
    const Offer state = focused() ? offer(*focused()) : Offer{};
    ui::Hint detail[5];
    int details = 0;
    if (state.armed)
        detail[details++] = {ui::Button::cross, state.label.c_str()};
    if (state.uninstall && installer_ && guard_ && !state.busy)
        detail[details++] = {ui::Button::square, "Uninstall"};
    detail[details++] = {ui::Button::dpad, "Scroll"};
    if (focused() && !focused()->local_only)
        detail[details++] = {ui::Button::triangle, "Refresh"};
    detail[details++] = {ui::Button::circle, "Back"};
    // A hint row leaves as the next layer arrives, so two are never legible at once.
    scene_.push_opacity(1.0f - tween::clamp01(page * 3.0f));
    ui::draw_hints(scene_, fonts, glyphs, home + (visible_.empty() ? 1 : 0),
                   visible_.empty() ? 4 : 5, kRight, true);
    scene_.pop_opacity();

    const std::uint32_t glass = renderer.glass_texture();
    draw_page(fonts, glass);
    if (page > 0.004f)
    {
        overlay_.push_opacity(tween::clamp01((page - 0.4f) / 0.6f));
        ui::draw_hints(overlay_, fonts, glyphs, detail, details, kRight, true);
        overlay_.pop_opacity();
    }
    draw_panel(fonts, glass);
    ui::Canvas canvas{overlay_, fonts, glass, time_};
    dialog_.draw(canvas);
    toasts_.draw(canvas);

    // Farlight as the Aurora Shelf paints it: the cover's dark and mid
    // tones as slow clouds, with a little of its light in the brightest one.
    gfx::BackdropSpec backdrop;
    backdrop.mode = gfx::BackdropMode::aurora;
    backdrop.colors[0] = gfx::mix(kDeep, Color::rgb(0x05060c), 0.35f);
    backdrop.colors[1] = gfx::mix(kDeep, kMid, 0.35f);
    backdrop.colors[2] = kMid;
    backdrop.colors[3] = gfx::mix(kMid, kAccent, 0.55f);
    backdrop.time = time_;
    renderer.begin();
    renderer.backdrop(backdrop);
    renderer.draw(scene_);
    if (!overlay_.empty())
    {
        renderer.glass();
        renderer.draw(overlay_);
    }
}

// ---- settings ---------------------------------------------------------------

std::string format_settings(const Settings &settings)
{
    return "location=" + settings.location + "\nupdates=" + (settings.check_updates ? "1" : "0") +
           "\nsounds=" + (settings.sounds ? "1" : "0") +
           "\nvibration=" + (settings.vibration ? "1" : "0") + "\n";
}

Settings parse_settings(std::string_view text)
{
    Settings settings;
    while (!text.empty())
    {
        const auto end = text.find('\n');
        const auto line = text.substr(0, end);
        text = end == text.npos ? std::string_view{} : text.substr(end + 1);
        const auto equals = line.find('=');
        if (equals == line.npos)
            continue;
        const auto key = line.substr(0, equals), value = line.substr(equals + 1);
        if (key == "location" && system::clean_absolute_path(value))
            settings.location = value;
        else if (key == "updates")
            settings.check_updates = value != "0";
        else if (key == "sounds")
            settings.sounds = value != "0";
        else if (key == "vibration")
            settings.vibration = value != "0";
    }
    return settings;
}

void Screen::set_locations(std::vector<std::pair<std::string, std::uint64_t>> locations)
{
    locations_ = std::move(locations);
    // A saved location that isn't offered on this console gives way to the first that is.
    const auto offered = [&](const auto &place) { return place.first == settings_.location; };
    if (!locations_.empty() && std::none_of(locations_.begin(), locations_.end(), offered))
        settings_.location = locations_.front().first;
}

const App *Screen::self_app() const
{
    for (const auto &app : apps_)
        if (app.title_id == self_id_)
            return &app;
    return nullptr;
}

// ---- the panel: Queue, Settings, About ----------------------------------------

void Screen::open_panel(int tab)
{
    panel_ = true;
    panel_tab_ = std::clamp(tab, 0, 2);
    queue_focus_ = 0;
    if (panel_tab_ == 2)
        write_about();
}

void Screen::write_about()
{
    using Block = ui::TextBlock;
    std::vector<Block> blocks;
    blocks.push_back(Block::paragraph(
        "ProsperoStore installs, updates and uninstalls the apps listed at homebrew.page, "
        "the catalog of homebrew for this console."));
    blocks.push_back(
        Block::key_value("Version", self_version_.empty() ? "Unknown" : self_version_));
    blocks.push_back(Block::key_value("Catalog", status_));
    blocks.push_back(Block::key_value("New apps go to", settings_.location));
    blocks.push_back(Block::heading("How it keeps installs safe", 3));
    blocks.push_back(
        Block::bullet("The catalog is signed, and the store refuses one it can't verify."));
    blocks.push_back(
        Block::bullet("A download is checked against the catalog before it is unpacked."));
    blocks.push_back(
        Block::bullet("An app's folder is replaced in one step, only when the new one is "
                      "complete. A running app is never touched."));
    blocks.push_back(Block::heading("Please note", 3));
    blocks.push_back(Block::paragraph(
        "Apps are published by their own developers, who are responsible for their content "
        "and licensing. A listing is not a security audit. ProsperoStore and the catalog come "
        "without warranty."));
    blocks.push_back(Block::heading("Where things are", 3));
    blocks.push_back(Block::key_value("Store files", "/data/prosperostore"));
    blocks.push_back(Block::key_value("Logs", "/data/prosperostore/logs"));
    blocks.push_back(Block::heading("Thanks", 3));
    blocks.push_back(Block::paragraph(
        "ShadowMountPlus by drakmor puts installed apps on the home screen. Built with "
        "ps5-opengl, the Homebrew UI Lab and the PS5 native app boilerplate, and with curl, "
        "OpenSSL, zlib, miniz, Monocypher, yyjson, PicoSHA2, QR Code generator and stb. "
        "Fonts: Inter, Montserrat and DejaVu Sans Mono."));
    blocks.push_back(
        Block::paragraph("BlackBearReloaded. Free software under the GPL, version 3 or later."));
    about_.set_content(std::move(blocks));
    about_.scroll_to(0, true);
}

void Screen::update_panel(const InputFrame &input, ui::Feedback &feedback)
{
    if (input.is_pressed(Action::back) || input.is_pressed(Action::menu))
    {
        panel_ = false;
        feedback.play(audio::Cue::back);
        return;
    }
    if (input.is_pressed(Action::page_next) || input.is_pressed(Action::page_prev))
    {
        const int next = panel_tab_ + (input.is_pressed(Action::page_next) ? 1 : -1);
        if (next < 0 || next > 2)
            return refuse(feedback, false, 0.0f, 0.0f);
        open_panel(next);
        feedback.play(audio::Cue::tab, 0.96f + 0.04f * static_cast<float>(next));
        return;
    }
    const int step = input.nav == Direction::down ? 1 : input.nav == Direction::up ? -1 : 0;
    if (panel_tab_ == 0)
    {
        // What is running first, then what waits, in order.
        std::vector<std::string> ids;
        if (!activity_.id.empty())
            ids.push_back(activity_.id);
        ids.insert(ids.end(), activity_.waiting.begin(), activity_.waiting.end());
        const int count = static_cast<int>(ids.size());
        queue_focus_ = std::clamp(queue_focus_, 0, std::max(0, count - 1));
        if (step && count)
        {
            const int next = queue_focus_ + step;
            if (next < 0 || next >= count)
                return refuse(feedback, input.nav_repeat, 0.0f, static_cast<float>(step));
            queue_focus_ = next;
            feedback.play(audio::Cue::focus);
        }
        else if (input.is_pressed(Action::confirm) && count)
        {
            pending_order = {};
            pending_order.kind = Order::Kind::cancel;
            pending_order.id = ids[static_cast<std::size_t>(queue_focus_)];
            feedback.play(audio::Cue::select);
        }
        return;
    }
    if (panel_tab_ == 2)
    {
        about_.handle(input, feedback);
        return;
    }
    constexpr int kRows = 5;
    if (step)
    {
        const int next = setting_focus_ + step;
        if (next < 0 || next >= kRows)
            return refuse(feedback, input.nav_repeat, 0.0f, static_cast<float>(step));
        setting_focus_ = next;
        feedback.play(audio::Cue::focus);
        return;
    }
    const int turn = input.nav == Direction::right || input.is_pressed(Action::confirm) ? 1
                     : input.nav == Direction::left                                     ? -1
                                                                                        : 0;
    if (!turn)
        return;
    if (setting_focus_ == 0)
    {
        if (locations_.size() < 2)
            return refuse(feedback, input.nav_repeat, static_cast<float>(turn), 0.0f);
        const auto current =
            std::find_if(locations_.begin(), locations_.end(),
                         [&](const auto &place) { return place.first == settings_.location; });
        const auto count = static_cast<int>(locations_.size());
        const int index =
            current == locations_.end() ? 0 : static_cast<int>(current - locations_.begin());
        settings_.location =
            locations_[static_cast<std::size_t>((index + turn + count) % count)].first;
    }
    else if (setting_focus_ == 1)
        settings_.check_updates = !settings_.check_updates;
    else if (setting_focus_ == 2)
        settings_.sounds = !settings_.sounds;
    else if (setting_focus_ == 3)
        settings_.vibration = !settings_.vibration;
    else
    {
        // The store's own page says what can be done about its version.
        if (!input.is_pressed(Action::confirm) || !self_app())
            return refuse(feedback, input.nav_repeat, 0.0f, 0.0f);
        panel_ = false;
        open_app(self_id_);
        feedback.play(audio::Cue::open);
        return;
    }
    settings_changed = true;
    feedback.play(audio::Cue::toggle);
}

void Screen::draw_panel(const ui::Fonts &fonts, std::uint32_t glass)
{
    const float t = panel_value_.value;
    if (t <= 0.004f)
        return;
    auto &list = overlay_;
    const float veil = tween::clamp01(t * 1.6f);
    list.glass(glass, {0, 0, kWidth, kHeight}, 0, kWhite.with_alpha(veil));
    list.rounded_rect({0, 0, kWidth, kHeight}, 0,
                      gfx::mix(kCoal, kDeep, 0.4f).with_alpha(0.9f * veil));
    list.push_opacity(tween::smoothstep(t));
    list.push_transform(1.0f, 0, 0, 0, 24.0f * (1.0f - tween::cubic_out(t)));

    // The three tabs, as the home page's chips are drawn.
    constexpr const char *kTabs[] = {"Queue", "Settings", "About"};
    float x = kMargin + ui::button_width(ui::Button::l1, 30) + 16.0f;
    const float cy = 140.0f;
    ui::draw_button(list, fonts, ui::GlyphStyle::dark(), ui::Button::l1, kMargin, cy, 30);
    for (int i = 0; i < 3; ++i)
    {
        const float w = 48.0f + fonts.semibold.measure(kTabs[i], 22);
        const Rect chip{x, cy - kChipH * 0.5f, w, kChipH};
        const bool on = i == panel_tab_;
        list.bordered_rect(chip, kChipH * 0.5f, on ? kInk : kClear, 1.5f,
                           kInk.with_alpha(on ? 1.0f : 0.2f));
        ui::text(list, fonts.semibold, kTabs[i], chip.x + 24.0f, centred(cy, 22), 22,
                 on ? kCoal : kInk.with_alpha(0.78f));
        x += w + 12.0f;
    }
    ui::draw_button(list, fonts, ui::GlyphStyle::dark(), ui::Button::r1, x + 4.0f, cy, 30);

    const float top = 232.0f;
    const auto megabytes = [](std::uint64_t bytes) { return size_text(bytes); };
    const auto name_of = [&](const std::string &id)
    {
        for (const auto &app : apps_)
            if (app.title_id == id)
                return app.name;
        return id;
    };
    if (panel_tab_ == 0)
    {
        std::vector<std::string> ids;
        if (!activity_.id.empty())
            ids.push_back(activity_.id);
        ids.insert(ids.end(), activity_.waiting.begin(), activity_.waiting.end());
        float y = top;
        for (std::size_t i = 0; i < ids.size() && i < 6; ++i)
        {
            const Rect row{kMargin, y, kWidth - 2.0f * kMargin, 104.0f};
            const bool focused = static_cast<int>(i) == queue_focus_;
            list.rounded_rect(row, 18, kPanel.with_alpha(0.85f));
            if (focused)
                list.bordered_rect(row.inset(-5.0f), 22, kClear, 3.0f, kInk.with_alpha(0.94f));
            ui::text(list, fonts.semibold, fonts.semibold.font->fit(name_of(ids[i]), 28, 900.0f),
                     row.x + 32.0f, row.y + 44.0f, 28, kInk);
            const bool working = i == 0 && !activity_.id.empty();
            float progress = -1.0f;
            const char *phase = "Waiting";
            if (working)
                busy_app(progress, phase);
            std::string line = phase;
            if (working && progress >= 0.0f)
                line += "  " + megabytes(activity_.done) + " of " + megabytes(activity_.total);
            ui::text(list, fonts.regular, line, row.x + 32.0f, row.y + 80.0f, 22,
                     kInk.with_alpha(0.62f));
            const Rect track{row.x + row.w - 560.0f, row.cy() - 4.0f, 400.0f, 8.0f};
            list.rounded_rect(track, 4, kInk.with_alpha(0.14f));
            if (progress >= 0.0f)
                list.rounded_rect({track.x, track.y, std::max(8.0f, track.w * progress), 8.0f}, 4,
                                  kAccent);
            if (focused)
                ui::text(list, fonts.semibold, "Cancel", row.x + row.w - 32.0f,
                         centred(row.cy(), 22), 22, kAccent, gfx::Align::right);
            y += 120.0f;
        }
        if (ids.empty())
        {
            ui::text(list, fonts.semibold, "Nothing is being installed", kMargin, top + 40.0f, 34,
                     kInk);
            ui::text(list, fonts.regular,
                     "Installs, updates and removals line up here, one at a time.", kMargin,
                     top + 84.0f, 24, kInk.with_alpha(0.62f));
            y = top + 150.0f;
        }
        // What finished since the store was opened, newest first.
        if (!history_.empty())
        {
            ui::text(list, fonts.semibold, "FINISHED", kMargin, y + 30.0f, 16,
                     kInk.with_alpha(0.55f), gfx::Align::left, 3.5f);
            y += 58.0f;
            for (auto done = history_.rbegin(); done != history_.rend() && y < 930.0f; ++done)
            {
                if (done->ok)
                    draw_check(list, kMargin + 14.0f, y + 6.0f, 22.0f, kOwned);
                else
                    list.ring(kMargin + 14.0f, y + 6.0f, 10.0f, 3.0f, Color::rgb(0xe5484d));
                ui::text(list, fonts.semibold, fonts.semibold.font->fit(done->title, 24, 700.0f),
                         kMargin + 44.0f, y + 14.0f, 24, kInk);
                ui::text(list, fonts.regular, fonts.regular.font->fit(done->body, 22, 900.0f),
                         kMargin + 780.0f, y + 14.0f, 22, kInk.with_alpha(0.62f));
                y += 52.0f;
            }
        }
    }
    else if (panel_tab_ == 1)
    {
        const App *self = self_app();
        const bool newer =
            self && catalog::update_available(self_version_, self->available_version);
        std::string place = settings_.location;
        for (const auto &[path, room] : locations_)
            if (path == settings_.location)
                place += "   " + megabytes(room) + " free";
        const struct Row
        {
            const char *label, *note;
            std::string value;
            bool on;
        } rows[] = {
            {"Install location",
             "Where new apps go. A folder ShadowMountPlus scans on this console.", place, true},
            {"Check for a newer ProsperoStore",
             "Asked once at start; a notice appears when there is one.",
             settings_.check_updates ? "On" : "Off", settings_.check_updates},
            {"Sounds", "The interface's own sounds.", settings_.sounds ? "On" : "Off",
             settings_.sounds},
            {"Vibration", "A light answer from the controller.", settings_.vibration ? "On" : "Off",
             settings_.vibration},
            {"ProsperoStore", "Open its page to update it.",
             restart_needed_ ? "Restart to finish the update"
             : newer         ? "Version " + self->available_version + " is available"
                             : "Version " + self_version_ + (self ? ", up to date" : ""),
             newer || restart_needed_},
        };
        float y = top;
        for (int i = 0; i < 5; ++i)
        {
            const Rect row{kMargin, y, kWidth - 2.0f * kMargin, 112.0f};
            list.rounded_rect(row, 18, kPanel.with_alpha(0.85f));
            if (i == setting_focus_)
                list.bordered_rect(row.inset(-5.0f), 22, kClear, 3.0f, kInk.with_alpha(0.94f));
            ui::text(list, fonts.semibold, rows[i].label, row.x + 32.0f, row.y + 48.0f, 28, kInk);
            ui::text(list, fonts.regular, rows[i].note, row.x + 32.0f, row.y + 84.0f, 22,
                     kInk.with_alpha(0.62f));
            ui::text(list, fonts.semibold, fonts.semibold.font->fit(rows[i].value, 26, 760.0f),
                     row.x + row.w - 32.0f, centred(row.cy(), 26), 26,
                     rows[i].on ? kAccent : kInk.with_alpha(0.62f), gfx::Align::right);
            y += 128.0f;
        }
    }
    else
    {
        ui::text(list, fonts.display, "ProsperoStore", kMargin - 3.0f, top + 44.0f, 60, kInk);
        float words = ui::text(list, fonts.regular, "Apps from ", kMargin, top + 92.0f, 26,
                               kInk.with_alpha(0.62f));
        ui::text(list, fonts.semibold, "homebrew.page", kMargin + words, top + 92.0f, 26, kAccent);
        ui::Canvas canvas{list, fonts, glass, time_};
        about_.draw(canvas);
    }
    const ui::Hint queue[] = {{ui::Button::cross, "Cancel"},
                              {ui::Button::l1, "Tabs", ui::Button::r1},
                              {ui::Button::circle, "Close"}};
    const ui::Hint change[] = {{ui::Button::cross, "Change"},
                               {ui::Button::l1, "Tabs", ui::Button::r1},
                               {ui::Button::circle, "Close"}};
    const ui::Hint read[] = {{ui::Button::dpad, "Scroll"},
                             {ui::Button::l1, "Tabs", ui::Button::r1},
                             {ui::Button::circle, "Close"}};
    const bool jobs = !activity_.id.empty() || !activity_.waiting.empty();
    if (panel_tab_ == 0)
        ui::draw_hints(list, fonts, ui::GlyphStyle::dark(), queue + (jobs ? 0 : 1), jobs ? 3 : 2,
                       kRight, true);
    else
        ui::draw_hints(list, fonts, ui::GlyphStyle::dark(), panel_tab_ == 1 ? change : read, 3,
                       kRight, true);
    list.pop_transform();
    list.pop_opacity();
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
