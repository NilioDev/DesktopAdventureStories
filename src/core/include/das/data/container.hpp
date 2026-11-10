// SPDX-License-Identifier: GPL-3.0-or-later
//
// The outer structure of a game data file (YODESK.DTA, DESKTOP.DAW): a list
// of tagged sections. See docs/formats/container.md.
#pragma once

#include "das/data/binary.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace das::data {

// The two games store some things differently; the layout tells which way.
enum class Layout {
    // Indiana Jones and His Desktop Adventures: zone extras live in their
    // own top-level sections (ZAUX, ZAX2, ZAX3, ZAX4, HTSP, ACTN).
    Indy,
    // Star Wars: Yoda Stories: zone extras are stored inside each zone.
    Yoda,
};

std::string_view layoutName(Layout layout);

struct Section {
    std::string tag;
    // The section's content without its tag and (if any) length field.
    Bytes payload;
};

struct Container {
    Layout layout = Layout::Yoda;
    std::vector<Section> sections;

    const Section* find(std::string_view tag) const;
    Section* find(std::string_view tag);
};

Container readContainer(ByteView file);
Bytes writeContainer(const Container& container);

} // namespace das::data
