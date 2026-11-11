// SPDX-License-Identifier: GPL-3.0-or-later
#include "problem_log.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <deque>
#include <mutex>
#include <system_error>

namespace das::app::problem_log {
namespace {

constexpr std::size_t kKeptLines = 200;
constexpr std::size_t kPreviousLines = 60;

std::mutex g_mutex;
std::deque<std::string> g_lines;
std::deque<std::string> g_previous; // the end of a run that stopped unexpectedly
std::filesystem::path g_folder;
FILE* g_file = nullptr;
SDL_LogOutputFunction g_default = nullptr;
void* g_defaultData = nullptr;

const char* priorityName(SDL_LogPriority priority) {
    switch (priority) {
    case SDL_LOG_PRIORITY_VERBOSE: return "VERBOSE";
    case SDL_LOG_PRIORITY_DEBUG: return "DEBUG";
    case SDL_LOG_PRIORITY_INFO: return "INFO";
    case SDL_LOG_PRIORITY_WARN: return "WARN";
    case SDL_LOG_PRIORITY_ERROR: return "ERROR";
    case SDL_LOG_PRIORITY_CRITICAL: return "CRITICAL";
    default: return "LOG";
    }
}

void SDLCALL output(void* /*userdata*/, int category, SDL_LogPriority priority, const char* message) {
    if (g_default) {
        g_default(g_defaultData, category, priority, message);
    }
    char stamp[16];
    const Uint64 ms = SDL_GetTicks();
    SDL_snprintf(stamp, sizeof stamp, "%llu.%03llu", static_cast<unsigned long long>(ms / 1000),
                 static_cast<unsigned long long>(ms % 1000));
    std::string line = std::string(stamp) + " " + priorityName(priority) + ": " + (message ? message : "");
    const std::lock_guard lock(g_mutex);
    if (g_file) {
        std::fputs(line.c_str(), g_file);
        std::fputc('\n', g_file);
        std::fflush(g_file); // a crash must not lose the last lines
    }
    g_lines.push_back(std::move(line));
    if (g_lines.size() > kKeptLines) {
        g_lines.pop_front();
    }
}

std::filesystem::path marker() { return g_folder / "running"; }

void writeMarker() {
    if (FILE* f = std::fopen(marker().string().c_str(), "wb")) {
        std::fclose(f);
    }
}

} // namespace

void begin() {
    SDL_GetLogOutputFunction(&g_default, &g_defaultData);
    SDL_SetLogOutputFunction(output, nullptr);
}

bool attach(const std::filesystem::path& folder) {
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    const std::lock_guard lock(g_mutex);
    g_folder = folder;
    const bool crashed = std::filesystem::exists(marker(), ec);
    const auto path = folder / "log.txt";
    const auto previous = folder / "log-previous.txt";
    std::filesystem::rename(path, previous, ec);
    if (crashed) {
        // A report made now is mostly about the run that went wrong: keep
        // the end of its log too.
        if (FILE* f = std::fopen(previous.string().c_str(), "rb")) {
            char buffer[1024];
            while (std::fgets(buffer, sizeof buffer, f)) {
                std::string line = buffer;
                while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
                    line.pop_back();
                }
                g_previous.push_back(std::move(line));
                if (g_previous.size() > kPreviousLines) {
                    g_previous.pop_front();
                }
            }
            std::fclose(f);
        }
    }
    g_file = std::fopen(path.string().c_str(), "wb");
    if (g_file) {
        for (const auto& line : g_lines) {
            std::fputs(line.c_str(), g_file);
            std::fputc('\n', g_file);
        }
        std::fflush(g_file);
    }
    writeMarker();
    return crashed;
}

void pause() {
    const std::lock_guard lock(g_mutex);
    if (!g_folder.empty()) {
        std::error_code ec;
        std::filesystem::remove(marker(), ec);
    }
}

void resume() {
    const std::lock_guard lock(g_mutex);
    if (!g_folder.empty()) {
        writeMarker();
    }
}

std::string tail() {
    const std::lock_guard lock(g_mutex);
    // A report cuts lines from the front, so this run
    // comes first and the end of a run that went wrong is kept longest.
    std::string out;
    for (const auto& line : g_lines) {
        out += line;
        out += '\n';
    }
    if (!g_previous.empty()) {
        out += "--- the run before, which stopped unexpectedly ---\n";
        for (const auto& line : g_previous) {
            out += line;
            out += '\n';
        }
    }
    return out;
}

std::filesystem::path file() {
    const std::lock_guard lock(g_mutex);
    return g_folder.empty() ? std::filesystem::path{} : g_folder / "log.txt";
}

} // namespace das::app::problem_log
