// ProsperoStore - Controller-driven store screens assembled from Homebrew UI.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/renderer.hpp"
#include "catalog/catalog.hpp"
#include "ui/components/grid.hpp"
#include "ui/components/tabs.hpp"
#include "ui/components/text_view.hpp"

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
};

class Screen
{
  public:
    Screen();
    void set_catalog(std::vector<App> apps, std::string status);
    void set_detail(const catalog::Entry &entry);
    void set_detail_error(const std::string &id, std::string message);
    void set_icon(const std::string &id, std::uint32_t texture);
    void set_qr(std::string id, std::uint32_t texture, int width);
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
    void refresh_grid();
    void refresh_detail();
    hui::ui::Theme theme_;
    hui::ui::GridView grid_;
    hui::ui::TabBar tabs_;
    hui::ui::TextView article_;
    hui::gfx::DrawList scene_;
    std::vector<App> apps_;
    std::vector<std::size_t> visible_;
    std::string status_ = "Connecting to the catalog...";
    float time_ = 0;
    bool details_ = false;
    bool quit_ = false;
    std::string query_;
    std::string qr_id_;
    std::uint32_t qr_texture_ = 0;
    int qr_width_ = 0;
    enum class Sort
    {
        name,
        released,
        updated
    } sort_ = Sort::name;
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
