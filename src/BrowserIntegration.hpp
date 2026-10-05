#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dk2vr {

// Sending a web page from the browser to the player:
//   - the "dk2vr:" URL scheme, registered per user (HKCU only), launches
//     DK2VRPlayer.exe "dk2vr://open?url=<page>&video=<direct media>";
//   - a bookmarklet builds that link from the current page;
//   - a second DK2VRPlayer.exe hands its argument to the running one, so a
//     link never opens a second player.

// Title of the player's top-level window; the single-instance hand-off finds
// the running player by it.
inline constexpr wchar_t kPlayerWindowTitle[] = L"DK2 360 VR Player";
// WM_COPYDATA tag for a forwarded command-line argument ('DK2A').
inline constexpr unsigned long kForwardedArgumentTag = 0x444B3241UL;

struct LaunchRequest {
    std::string pageUrl;
    // Optional direct media URL the bookmarklet read from the page's <video>,
    // played when yt-dlp cannot resolve the page.
    std::string videoUrl;
};

// True for an http(s) URL without whitespace or control characters. Anything
// handed to yt-dlp must pass this, so a link can never smuggle in an option.
[[nodiscard]] bool isWebUrl(std::string_view value);

[[nodiscard]] std::string percentDecode(std::string_view value);

// Accepts "dk2vr://open?url=...&video=...", "dk2vr:<url>" and a bare http(s)
// URL. Returns nullopt for anything else (local files are the caller's).
[[nodiscard]] std::optional<LaunchRequest> parseLaunchUrl(std::string_view argument);

// True when the argument is meant for the dk2vr: scheme at all.
[[nodiscard]] bool isDk2vrLink(std::string_view argument);

// The bookmarklet's javascript: URL.
[[nodiscard]] std::string bookmarkletUrl();

// Per-user registration of the dk2vr: scheme (HKCU\Software\Classes\dk2vr).
[[nodiscard]] bool isProtocolRegistered(const std::filesystem::path& executable);
[[nodiscard]] bool registerProtocol(const std::filesystem::path& executable, std::string& error);
[[nodiscard]] bool unregisterProtocol(std::string& error);

struct InstalledBrowser {
    std::string name;
    std::filesystem::path executable;
};

// Brave, Chrome, Edge and Firefox when installed, Brave first: the bookmark
// has to go into the browser the user actually watches in, which is often
// not the Windows default.
[[nodiscard]] std::vector<InstalledBrowser> findInstalledBrowsers();

// Writes a small page with the draggable bookmarklet and opens it in the
// given browser (the Windows default when empty).
[[nodiscard]] bool openBookmarkletPage(const std::filesystem::path& file,
    const std::filesystem::path& browser, std::string& error);

// Single instance: returns true when another player is running and received
// the argument (this process should then exit). The first instance gets
// false and keeps the instance lock for its lifetime.
[[nodiscard]] bool forwardToRunningInstance(const std::wstring& argument);

} // namespace dk2vr
