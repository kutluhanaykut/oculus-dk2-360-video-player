#include "Logger.hpp"

#include <Windows.h>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

namespace dk2vr::log {
namespace {

std::mutex g_mutex;
std::ofstream g_file;

void write(const std::string_view level, const std::string_view message)
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t timestamp = std::chrono::system_clock::to_time_t(now);
    std::tm localTime {};
    localtime_s(&localTime, &timestamp);

    std::ostringstream line;
    line << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S") << " [" << level << "] " << message << '\n';
    const std::string text = line.str();

    std::scoped_lock lock(g_mutex);
    if (g_file.is_open()) {
        g_file << text;
        g_file.flush();
    }
    OutputDebugStringA(text.c_str());
}

} // namespace

void initialize(const std::filesystem::path& directory)
{
    std::scoped_lock lock(g_mutex);
    g_file.open(directory / L"DK2VRPlayer.log", std::ios::out | std::ios::trunc);
}

void shutdown()
{
    std::scoped_lock lock(g_mutex);
    if (g_file.is_open()) {
        g_file.close();
    }
}

void info(const std::string_view message)
{
    write("INFO", message);
}

void warning(const std::string_view message)
{
    write("WARN", message);
}

void error(const std::string_view message)
{
    write("ERROR", message);
}

} // namespace dk2vr::log
