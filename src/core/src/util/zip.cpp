// SPDX-License-Identifier: GPL-3.0-or-later
#include "das/util/zip.hpp"

#include "das/util/zlib.hpp"

#include <algorithm>

namespace das::zip {

using data::ByteReader;
using data::ByteView;
using data::DataError;

namespace {

constexpr std::uint32_t kEndOfDirectory = 0x06054b50;
constexpr std::uint32_t kDirectoryEntry = 0x02014b50;
constexpr std::uint32_t kLocalHeader = 0x04034b50;

std::uint32_t u32At(ByteView bytes, std::size_t at) {
    ByteReader r(bytes.subspan(at, 4));
    return r.u32();
}

} // namespace

std::vector<Entry> read(ByteView archive) {
    // The end-of-directory record sits at the end, before an optional comment.
    if (archive.size() < 22) {
        throw DataError("not a zip archive");
    }
    std::size_t end = archive.size() - 22;
    const std::size_t lowest = archive.size() > 22 + 65535 ? archive.size() - 22 - 65535 : 0;
    while (u32At(archive, end) != kEndOfDirectory) {
        if (end == lowest) {
            throw DataError("not a zip archive");
        }
        --end;
    }
    ByteReader eocd(archive.subspan(end + 4));
    const std::uint16_t disk = eocd.u16();
    const std::uint16_t directoryDisk = eocd.u16();
    eocd.skip(2);
    const std::uint16_t count = eocd.u16();
    const std::uint32_t directorySize = eocd.u32();
    const std::uint32_t directoryOffset = eocd.u32();
    if (disk != 0 || directoryDisk != 0) {
        throw DataError("multi-part zip archives are not supported");
    }
    if (directoryOffset == 0xffffffffu || count == 0xffff) {
        throw DataError("zip64 archives are not supported");
    }
    if (static_cast<std::size_t>(directoryOffset) + directorySize > archive.size()) {
        throw DataError("zip directory lies outside the archive");
    }

    std::vector<Entry> entries;
    ByteReader dir(archive.subspan(directoryOffset, directorySize));
    for (std::uint16_t i = 0; i < count; ++i) {
        if (dir.u32() != kDirectoryEntry) {
            throw DataError("bad zip directory entry");
        }
        dir.skip(4); // versions
        const std::uint16_t flags = dir.u16();
        const std::uint16_t method = dir.u16();
        dir.skip(4); // time, date
        const std::uint32_t crc = dir.u32();
        const std::uint32_t packedSize = dir.u32();
        const std::uint32_t size = dir.u32();
        const std::uint16_t nameLength = dir.u16();
        const std::uint16_t extraLength = dir.u16();
        const std::uint16_t commentLength = dir.u16();
        dir.skip(8); // disk, attributes
        const std::uint32_t localOffset = dir.u32();
        const auto nameBytes = dir.bytes(nameLength);
        dir.skip(static_cast<std::size_t>(extraLength) + commentLength);

        std::string name(nameBytes.begin(), nameBytes.end());
        std::replace(name.begin(), name.end(), '\\', '/');
        if (name.empty() || name.back() == '/') {
            continue; // a folder
        }
        if (flags & 1) {
            throw DataError("encrypted zip entries are not supported: " + name);
        }
        if (static_cast<std::size_t>(localOffset) + 30 > archive.size() ||
            u32At(archive, localOffset) != kLocalHeader) {
            throw DataError("bad zip entry header: " + name);
        }
        ByteReader local(archive.subspan(localOffset + 26, 4));
        const std::size_t dataStart =
            static_cast<std::size_t>(localOffset) + 30 + local.u16() + static_cast<std::size_t>(local.u16());
        if (dataStart + packedSize > archive.size()) {
            throw DataError("zip entry lies outside the archive: " + name);
        }
        const ByteView packed = archive.subspan(dataStart, packedSize);
        Entry entry{name, {}};
        if (method == 0) {
            entry.content.assign(packed.begin(), packed.end());
        } else if (method == 8) {
            entry.content = zlib::inflate(packed);
        } else {
            throw DataError("unsupported zip compression method for " + name);
        }
        if (entry.content.size() != size || zlib::crc32(entry.content) != crc) {
            throw DataError("zip entry is damaged: " + name);
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

} // namespace das::zip
