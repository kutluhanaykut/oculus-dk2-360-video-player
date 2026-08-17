#include "YouTubeHistory.hpp"

#include "Logger.hpp"

#include <algorithm>
#include <exception>
#include <fstream>
#include <nlohmann/json.hpp>

namespace dk2vr {
namespace {

using Json = nlohmann::json;

constexpr std::size_t maximumEntries = 20;

} // namespace

YouTubeHistory::YouTubeHistory(std::filesystem::path file)
    : file_(std::move(file))
{
}

void YouTubeHistory::load()
{
    entries_.clear();
    std::ifstream stream(file_, std::ios::binary);
    if (!stream) {
        return;
    }
    try {
        const Json root = Json::parse(stream);
        const auto array = root.find("entries");
        if (array == root.end() || !array->is_array()) {
            return;
        }
        for (const auto& item : *array) {
            if (!item.is_object()) {
                continue;
            }
            const std::string title = item.value("title", std::string {});
            const std::string url = item.value("url", std::string {});
            if (url.empty()) {
                continue;
            }
            entries_.push_back({title, url});
            if (entries_.size() >= maximumEntries) {
                break;
            }
        }
    } catch (const std::exception& exception) {
        log::warning(std::string("YouTube gecmisi okunamadi: ") + exception.what());
        entries_.clear();
    }
}

void YouTubeHistory::add(const std::string& title, const std::string& url)
{
    if (url.empty()) {
        return;
    }
    // Ayni URL zaten varsa listeden cikar (en ustte yeniden eklenecek).
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                        [&url](const YouTubeHistoryEntry& entry) { return entry.url == url; }),
        entries_.end());
    entries_.insert(entries_.begin(), YouTubeHistoryEntry {title, url});
    if (entries_.size() > maximumEntries) {
        entries_.resize(maximumEntries);
    }
    save();
}

void YouTubeHistory::remove(const std::string& url)
{
    const std::size_t before = entries_.size();
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                        [&url](const YouTubeHistoryEntry& entry) { return entry.url == url; }),
        entries_.end());
    if (entries_.size() != before) {
        save();
    }
}

void YouTubeHistory::clear()
{
    if (entries_.empty()) {
        return;
    }
    entries_.clear();
    save();
}

const std::vector<YouTubeHistoryEntry>& YouTubeHistory::entries() const noexcept
{
    return entries_;
}

bool YouTubeHistory::empty() const noexcept
{
    return entries_.empty();
}

std::size_t YouTubeHistory::size() const noexcept
{
    return entries_.size();
}

void YouTubeHistory::save() const
{
    try {
        Json root;
        root["entries"] = Json::array();
        for (const auto& entry : entries_) {
            root["entries"].push_back(Json {{"title", entry.title}, {"url", entry.url}});
        }
        std::ofstream stream(file_, std::ios::binary | std::ios::trunc);
        if (!stream) {
            log::warning("YouTube gecmisi dosyaya yazilamadi: " + file_.string());
            return;
        }
        stream << root.dump(2);
    } catch (const std::exception& exception) {
        log::warning(std::string("YouTube gecmisi kaydedilemedi: ") + exception.what());
    }
}

} // namespace dk2vr
