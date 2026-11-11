// SPDX-License-Identifier: GPL-3.0-or-later
#include "das/util/report.hpp"

namespace das::report {

std::string urlEncode(std::string_view text) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
            c == '_' || c == '~') {
            out += ch;
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 15];
        }
    }
    return out;
}

namespace {

// Drops the oldest line of a text (or, for one long line, its first half).
void dropFront(std::string_view& text) {
    const auto newline = text.find('\n');
    text.remove_prefix(newline != std::string_view::npos && newline + 1 < text.size() ? newline + 1
                                                                                       : (text.size() + 1) / 2);
}

// Drops the last line of a text (or, for one long line, its second half).
void dropBack(std::string_view& text) {
    std::string_view rest = text;
    while (!rest.empty() && rest.back() == '\n') {
        rest.remove_suffix(1);
    }
    const auto newline = rest.rfind('\n');
    text = newline != std::string_view::npos ? text.substr(0, newline + 1) : text.substr(0, rest.size() / 2);
}

} // namespace

std::string issueUrl(std::string_view repository, const Problem& problem, std::size_t maxLength) {
    std::string base(repository);
    base += "/issues/new?template=bug_report.yml&labels=bug";
    base += "&version=" + urlEncode(problem.version);
    base += "&system=" + urlEncode(problem.system);
    if (!problem.what.empty()) {
        base += "&what=" + urlEncode(problem.what);
    }
    std::string_view snapshot = problem.snapshot;
    std::string_view log = problem.log;
    for (;;) {
        std::string url = base;
        if (!snapshot.empty()) {
            url += "&snapshot=" + urlEncode(snapshot);
        }
        if (!log.empty()) {
            url += "&log=" + urlEncode(log);
        }
        if (url.size() <= maxLength) {
            return url;
        }
        if (!log.empty()) {
            dropFront(log);
        } else if (!snapshot.empty()) {
            dropBack(snapshot);
        } else {
            return base;
        }
    }
}

} // namespace das::report
