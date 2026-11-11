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

std::string issueUrl(std::string_view repository, const Problem& problem, std::size_t maxLength) {
    std::string base(repository);
    base += "/issues/new?template=bug_report.yml&labels=bug";
    base += "&version=" + urlEncode(problem.version);
    base += "&system=" + urlEncode(problem.system);
    std::string_view log = problem.log;
    for (;;) {
        std::string url = base;
        if (!log.empty()) {
            url += "&log=" + urlEncode(log);
        }
        if (url.size() <= maxLength || log.empty()) {
            return url.size() <= maxLength ? url : base;
        }
        // Drop the oldest line (or, for one long line, its first half).
        const auto newline = log.find('\n');
        log.remove_prefix(newline != std::string_view::npos && newline + 1 < log.size() ? newline + 1
                                                                                         : (log.size() + 1) / 2);
    }
}

} // namespace das::report
