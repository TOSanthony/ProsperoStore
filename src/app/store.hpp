// ProsperoStore - Controller-driven store screens assembled from Homebrew UI.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/renderer.hpp"
#include "catalog/catalog.hpp"
#include "ui/components/grid.hpp"
#include "ui/components/tabs.hpp"

#include <string>
#include <vector>

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
};

class Screen
{
  public:
    Screen();
    void set_catalog(std::vector<App> apps, std::string status);
    void set_detail(const catalog::Entry &entry);
    void set_icon(const std::string &id, std::uint32_t texture);
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
    hui::ui::Theme theme_;
    hui::ui::GridView grid_;
    hui::ui::TabBar tabs_;
    hui::gfx::DrawList scene_;
    std::vector<App> apps_;
    std::vector<std::size_t> visible_;
    std::string status_ = "Connecting to the catalog...";
    float time_ = 0;
    bool details_ = false;
    bool quit_ = false;
    std::string query_;
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
