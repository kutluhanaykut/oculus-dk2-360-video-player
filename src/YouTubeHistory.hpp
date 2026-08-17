#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace dk2vr {

// Tek bir YouTube gecmis kaydi: video basligi ve sayfa adresi.
struct YouTubeHistoryEntry {
    std::string title;
    std::string url;
};

// Daha once acilan YouTube videolarinin kalici gecmisini yonetir.
// Kayitlar JSON dosyasinda (youtube_history.json) saklanir ve uygulama
// baslatildiginda yeniden yuklenir.
class YouTubeHistory {
public:
    explicit YouTubeHistory(std::filesystem::path file);

    // Gecmisi dosyadan yukler. Dosya yoksa veya bozuksa bos liste ile baslar.
    void load();

    // Yeni bir kayit ekler (en yeni en ustte). Ayni URL varsa basligi
    // gunceller ve en ustte tasir. Degisiklik dosyaya yazilir.
    void add(const std::string& title, const std::string& url);

    // Bir kaydi listeden kaldirir ve dosyaya yazar.
    void remove(const std::string& url);

    // Gecmisi tamamen temizler ve dosyayi sifirlar.
    void clear();

    [[nodiscard]] const std::vector<YouTubeHistoryEntry>& entries() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    void save() const;

    std::filesystem::path file_;
    std::vector<YouTubeHistoryEntry> entries_;
};

} // namespace dk2vr
