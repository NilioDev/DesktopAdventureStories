// SPDX-License-Identifier: GPL-3.0-or-later
//
// The original games store text as Windows-1252 bytes. Our own files use
// UTF-8. The mapping is reversible for every byte value: the five bytes that
// Windows-1252 leaves undefined map to the matching C1 control characters.
#pragma once

#include <string>
#include <string_view>

namespace das::text {

std::string windows1252ToUtf8(std::string_view bytes);
// Throws std::invalid_argument for characters Windows-1252 cannot hold.
std::string utf8ToWindows1252(std::string_view utf8);

} // namespace das::text
