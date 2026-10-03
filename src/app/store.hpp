// ProsperoStore - Controller-driven store screens assembled from Homebrew UI.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/renderer.hpp"
#include "catalog/catalog.hpp"
#include "system/inventory.hpp"
#include "ui/components/text_view.hpp"
#include "ui/components/toast.hpp"
#include "ui/motion.hpp"

#include <string>
#include <vector>
#include <optional>

namespace store
{

struct App
{
    std::string title_id;
    std::string name;
    std::string author;
    std::string description;
    std::string kind;
    std::string version;
    std::string badge;
    std::uint32_t icon = 0;
    std::string released, updated;
    std::optional<catalog::Entry> detail = {};
    std::string detail_error = {};
    std::string catalog_badge = {}, available_version = {};
    std::vector<system::InstalledApp> installed = {};
    bool local_only = false;
};

// The layout follows the UI library's "Storefront" design: a featured banner,
// section chips, a grid of cards and a product page. The colours are those of
// "Glass Orchard" as the Aurora Shelf design shows it.
class Screen
{
  public:
    Screen();
    void set_catalog(std::vector<App> apps, std::string status, bool current = false);
    void set_inventory(system::Inventory inventory);
    void set_detail(const catalog::Entry &entry);
    void set_detail_error(const std::string &id, std::string message);
    void set_icon(const std::string &id, std::uint32_t texture);
    void set_qr(std::string id, std::uint32_t texture, int width);
    // The picture shown for a coming-soon app that has no artwork of its own.
    void set_coming_soon_art(std::uint32_t texture)
    {
        coming_soon_art_ = texture;
    }
    // A floating notice in the top-right corner; it leaves after ten seconds.
    void notify(std::string title, std::string body);
    std::vector<std::string> artwork() const;
    void set_query(std::string query);
    const std::string &query() const
    {
        return query_;
    }
    void set_status(std::string status)
    {
        status_ = std::move(status);
    }
    std::string pending_detail;
    bool pending_search = false;
    void update(const hui::InputFrame &input, float dt, hui::ui::Feedback &feedback);
    void draw(hui::gfx::Renderer &renderer, const hui::ui::Fonts &fonts);
    bool wants_quit() const
    {
        return quit_;
    }

  private:
    enum class Zone
    {
        banner,
        chips,
        grid
    };
    enum class Sort
    {
        name,
        released,
        updated
    };
    static constexpr int kSections = 7;
    // What the card and the banner print, fitted once per catalog, not per frame.
    struct Fit
    {
        std::string title, author;
    };

    bool in_section(const App &app, int section) const;
    void rebuild();
    void refresh_detail();
    const App *focused() const;
    const App *page_app() const;
    bool banner_shown() const;
    std::uint32_t art(const App &app) const;
    float chips_rest() const;
    float chips_y() const;
    float grid_top() const;
    float window_top() const;
    float fade_at(float y) const;
    hui::gfx::Rect card_rect(int index) const;
    hui::gfx::Rect focus_target() const;
    float focus_radius() const;
    float scroll_target() const;
    void refuse(hui::ui::Feedback &feedback, bool repeat, float dx, float dy);
    void step_section(int delta, bool repeat, hui::ui::Feedback &feedback);
    void show_banner(int slot);
    void update_home(const hui::InputFrame &input, hui::ui::Feedback &feedback);
    void update_page(const hui::InputFrame &input, hui::ui::Feedback &feedback);
    void layout(const hui::ui::Fonts &fonts);
    void draw_top_bar(const hui::ui::Fonts &fonts);
    void draw_banner(const hui::ui::Fonts &fonts);
    void draw_chips(const hui::ui::Fonts &fonts);
    void draw_card(const hui::ui::Fonts &fonts, int index, unsigned layers);
    void draw_grid(const hui::ui::Fonts &fonts);
    void draw_page(const hui::ui::Fonts &fonts, std::uint32_t glass);
    void draw_action_box(const hui::ui::Fonts &fonts, std::uint32_t glass, const App &app,
                         float content);

    hui::ui::Theme theme_;
    hui::ui::TextView article_;
    hui::ui::ToastStack toasts_;
    hui::gfx::DrawList scene_, overlay_;
    std::vector<App> apps_;
    std::vector<std::size_t> visible_, featured_;
    std::vector<Fit> fits_;
    bool fits_stale_ = true, placed_ = false;
    int counts_[kSections] = {};
    hui::gfx::Rect chips_[kSections] = {};
    std::string status_ = "Connecting to the catalog...";
    system::Inventory inventory_;
    bool inventory_ready_ = false, catalog_current_ = false;
    std::string query_;
    std::string qr_id_;
    std::uint32_t qr_texture_ = 0, coming_soon_art_ = 0;
    int qr_width_ = 0;
    Sort sort_ = Sort::name;

    Zone zone_ = Zone::grid;
    int section_ = 0, focus_ = 0, banner_ = 0, banner_previous_ = 0;
    bool details_ = false, quit_ = false;
    float time_ = 0.0f, banner_clock_ = 0.0f;
    hui::tween::Spring scroll_, page_, banner_focus_, plate_;
    hui::tween::Timer banner_fade_, swap_;
    hui::ui::SpringRect ring_, chip_pill_;
    hui::tween::Spring ring_radius_;
    hui::ui::Pulse nudge_, press_;
    float nudge_x_ = 0.0f, nudge_y_ = 0.0f;
    std::vector<hui::tween::Spring> lift_;
};

class Fonts
{
  public:
    bool load(hui::gfx::Renderer &renderer, const std::string &assets);
    hui::ui::Fonts refs;

  private:
    hui::gfx::Font faces_[6];
};

} // namespace store
