#include "BrowserIntegration.hpp"

#include "Process.hpp"

#include <Windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <fstream>

namespace dk2vr {
namespace {

constexpr wchar_t kProtocolKey[] = L"Software\\Classes\\dk2vr";
constexpr wchar_t kSingleInstanceMutex[] = L"Local\\DK2VRPlayer.SingleInstance";

bool startsWithNoCase(const std::string_view value, const std::string_view prefix)
{
    return value.size() >= prefix.size()
        && std::equal(prefix.begin(), prefix.end(), value.begin(), [](const char a, const char b) {
               return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
           });
}

// Value of one query parameter, percent-decoded; empty when absent.
std::string queryValue(const std::string_view query, const std::string_view name)
{
    std::size_t position = 0;
    while (position <= query.size()) {
        const std::size_t end = (std::min)(query.find('&', position), query.size());
        const std::string_view pair = query.substr(position, end - position);
        const std::size_t equals = pair.find('=');
        if (equals != std::string_view::npos && pair.substr(0, equals) == name) {
            return percentDecode(pair.substr(equals + 1));
        }
        position = end + 1;
    }
    return {};
}

std::wstring commandFor(const std::filesystem::path& executable)
{
    return L"\"" + executable.wstring() + L"\" \"%1\"";
}

bool setStringValue(HKEY key, const wchar_t* name, const std::wstring& value)
{
    return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
               static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)))
        == ERROR_SUCCESS;
}

bool writeKey(const std::wstring& path, const wchar_t* name, const std::wstring& value)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr)
        != ERROR_SUCCESS) {
        return false;
    }
    const bool written = setStringValue(key, name, value);
    RegCloseKey(key);
    return written;
}

std::string htmlEscape(const std::string& value)
{
    std::string out;
    for (const char character : value) {
        switch (character) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        default:
            out.push_back(character);
            break;
        }
    }
    return out;
}

} // namespace

bool isWebUrl(const std::string_view value)
{
    if (!startsWithNoCase(value, "http://") && !startsWithNoCase(value, "https://")) {
        return false;
    }
    return std::none_of(value.begin(), value.end(), [](const char character) {
        const auto byte = static_cast<unsigned char>(character);
        return byte <= 0x20 || byte == 0x7F || character == '"';
    });
}

std::string percentDecode(const std::string_view value)
{
    const auto hex = [](const char character) -> int {
        if (character >= '0' && character <= '9') {
            return character - '0';
        }
        if (character >= 'a' && character <= 'f') {
            return character - 'a' + 10;
        }
        if (character >= 'A' && character <= 'F') {
            return character - 'A' + 10;
        }
        return -1;
    };
    std::string decoded;
    decoded.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        // '+' is left alone: encodeURIComponent writes spaces as %20, and a
        // literal '+' inside a page URL must survive.
        if (value[index] == '%' && index + 2 < value.size()
            && hex(value[index + 1]) >= 0 && hex(value[index + 2]) >= 0) {
            decoded.push_back(static_cast<char>(hex(value[index + 1]) * 16 + hex(value[index + 2])));
            index += 2;
        } else {
            decoded.push_back(value[index]);
        }
    }
    return decoded;
}

bool isDk2vrLink(const std::string_view argument)
{
    return startsWithNoCase(argument, "dk2vr:");
}

std::optional<LaunchRequest> parseLaunchUrl(const std::string_view argument)
{
    LaunchRequest request;
    if (isDk2vrLink(argument)) {
        std::string_view rest = argument.substr(6); // after "dk2vr:"
        const std::size_t query = rest.find('?');
        if (query != std::string_view::npos) {
            // dk2vr://open?url=...&video=... (browsers may add "/" before "?")
            rest = rest.substr(query + 1);
            request.pageUrl = queryValue(rest, "url");
            request.videoUrl = queryValue(rest, "video");
        } else {
            // dk2vr:<url> or dk2vr://<percent-encoded url>
            while (!rest.empty() && rest.front() == '/') {
                rest.remove_prefix(1);
            }
            request.pageUrl = percentDecode(rest);
        }
    } else {
        request.pageUrl = std::string(argument);
    }

    if (!isWebUrl(request.videoUrl)) {
        request.videoUrl.clear(); // blob:, data: or empty
    }
    if (!isWebUrl(request.pageUrl)) {
        if (request.videoUrl.empty()) {
            return std::nullopt;
        }
        request.pageUrl = request.videoUrl;
    }
    return request;
}

std::string bookmarkletUrl()
{
    // Sends the page, plus the <video> element's own http(s) source when it
    // has one (not a blob: stream), as a fallback for sites yt-dlp lacks.
    return "javascript:(function(){var v=document.querySelector('video');"
           "var s=v?(v.currentSrc||v.src||''):'';if(!/^https?:/i.test(s))s='';"
           "location.href='dk2vr://open?url='+encodeURIComponent(location.href)"
           "+(s?'&video='+encodeURIComponent(s):'');})();";
}

bool isProtocolRegistered(const std::filesystem::path& executable)
{
    wchar_t value[2048] {};
    DWORD size = sizeof(value);
    const std::wstring commandKey = std::wstring(kProtocolKey) + L"\\shell\\open\\command";
    if (RegGetValueW(HKEY_CURRENT_USER, commandKey.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, value, &size)
        != ERROR_SUCCESS) {
        return false;
    }
    return _wcsicmp(value, commandFor(executable).c_str()) == 0;
}

bool registerProtocol(const std::filesystem::path& executable, std::string& error)
{
    const std::wstring root(kProtocolKey);
    const bool ok = writeKey(root, nullptr, L"URL:DK2 VR Player")
        && writeKey(root, L"URL Protocol", L"")
        && writeKey(root + L"\\DefaultIcon", nullptr, L"\"" + executable.wstring() + L"\",0")
        && writeKey(root + L"\\shell\\open\\command", nullptr, commandFor(executable));
    if (!ok) {
        error = "dk2vr:// baglantisi kaydedilemedi (HKCU\\Software\\Classes\\dk2vr).";
    }
    return ok;
}

bool unregisterProtocol(std::string& error)
{
    const LSTATUS status = RegDeleteTreeW(HKEY_CURRENT_USER, kProtocolKey);
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
        error = "dk2vr:// kaydi kaldirilamadi.";
        return false;
    }
    return true;
}

std::vector<InstalledBrowser> findInstalledBrowsers()
{
    struct Candidate {
        const char* name;
        const wchar_t* exeName;
        const wchar_t* fallback; // relative to Program Files
    };
    constexpr Candidate candidates[] {
        {"Brave", L"brave.exe", L"BraveSoftware\\Brave-Browser\\Application\\brave.exe"},
        {"Chrome", L"chrome.exe", L"Google\\Chrome\\Application\\chrome.exe"},
        {"Edge", L"msedge.exe", L"Microsoft\\Edge\\Application\\msedge.exe"},
        {"Firefox", L"firefox.exe", L"Mozilla Firefox\\firefox.exe"},
    };
    std::vector<InstalledBrowser> browsers;
    for (const Candidate& candidate : candidates) {
        std::filesystem::path found;
        // Installers register their exe under App Paths.
        const std::wstring appPath = std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\")
            + candidate.exeName;
        for (const HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
            wchar_t value[MAX_PATH * 2] {};
            DWORD size = sizeof(value);
            if (RegGetValueW(root, appPath.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_SUCCESS) {
                found = value;
                break;
            }
        }
        if (found.empty()) {
            for (const wchar_t* variable : {L"ProgramFiles", L"ProgramFiles(x86)", L"LOCALAPPDATA"}) {
                wchar_t base[MAX_PATH] {};
                if (GetEnvironmentVariableW(variable, base, MAX_PATH) > 0) {
                    const std::filesystem::path candidatePath = std::filesystem::path(base) / candidate.fallback;
                    std::error_code error;
                    if (std::filesystem::is_regular_file(candidatePath, error)) {
                        found = candidatePath;
                        break;
                    }
                }
            }
        }
        std::error_code error;
        if (!found.empty() && std::filesystem::is_regular_file(found, error)) {
            browsers.push_back({candidate.name, found});
        }
    }
    return browsers;
}

bool openBookmarkletPage(const std::filesystem::path& file, const std::filesystem::path& browser,
    std::string& error)
{
    const std::string link = htmlEscape(bookmarkletUrl());
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    if (!stream) {
        error = "Yer imi sayfasi yazilamadi: " + wideToUtf8(file.wstring());
        return false;
    }
    stream << "<!doctype html><html lang=\"tr\"><head><meta charset=\"utf-8\">"
              "<title>DK2'de ac - yer imi</title><style>"
              "body{font-family:Segoe UI,sans-serif;max-width:640px;margin:40px auto;padding:0 16px;"
              "background:#11151c;color:#dde3ea;line-height:1.5}"
              "a.bm{display:inline-block;padding:10px 18px;border-radius:8px;background:#3a7bd5;"
              "color:#fff;font-weight:600;text-decoration:none}"
              "code,textarea{font-family:Consolas,monospace}"
              "textarea{width:100%;height:90px;background:#1b212b;color:#dde3ea;border:1px solid #2c3542}"
              "</style></head><body>"
              "<h1>DK2'de a&ccedil;</h1>"
              "<p>A&#351;a&#287;&#305;daki d&uuml;&#287;meyi <b>yer imi &ccedil;ubu&#287;una s&uuml;r&uuml;kleyip b&#305;rak&#305;n</b> "
              "(g&ouml;r&uuml;nm&uuml;yorsa Ctrl+Shift+B).</p>"
              "<p><a class=\"bm\" href=\""
           << link
           << "\">DK2'de a&ccedil;</a></p>"
              "<p>Bir video sayfas&#305;ndayken bu yer imine bas&#305;n; sayfa DK2 360 VR Player'da a&ccedil;&#305;l&#305;r. "
              "Taray&#305;c&#305; ilk seferde &laquo;DK2 VR Player a&ccedil;&#305;ls&#305;n m&#305;?&raquo; diye sorar.</p>"
              "<p>S&uuml;r&uuml;kleme olmazsa: yeni yer imi ekleyin ve adres olarak bunu yap&#305;&#351;t&#305;r&#305;n:</p>"
              "<textarea readonly onclick=\"this.select()\">"
           << link << "</textarea></body></html>";
    stream.close();
    if (!stream) {
        error = "Yer imi sayfasi yazilamadi.";
        return false;
    }
    const std::wstring quotedFile = L"\"" + file.wstring() + L"\"";
    const auto result = reinterpret_cast<INT_PTR>(browser.empty()
            ? ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL)
            : ShellExecuteW(nullptr, L"open", browser.c_str(), quotedFile.c_str(), nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        error = "Yer imi sayfasi tarayicida acilamadi.";
        return false;
    }
    return true;
}

bool forwardToRunningInstance(const std::wstring& argument)
{
    // Held for the life of the process; closing it would let the next
    // launch think no player is running.
    static HANDLE instanceMutex = nullptr;
    instanceMutex = CreateMutexW(nullptr, FALSE, kSingleInstanceMutex);
    if (instanceMutex == nullptr || GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
    }

    // The running player may still be starting (VLC's plugin scan takes a
    // while on first launch); give its window a few seconds to appear.
    for (int attempt = 0; attempt < 100; ++attempt) {
        const HWND window = FindWindowW(L"SDL_app", kPlayerWindowTitle);
        if (window != nullptr) {
            DWORD processId = 0;
            GetWindowThreadProcessId(window, &processId);
            AllowSetForegroundWindow(processId);
            COPYDATASTRUCT data {};
            data.dwData = kForwardedArgumentTag;
            data.cbData = static_cast<DWORD>((argument.size() + 1) * sizeof(wchar_t));
            data.lpData = const_cast<wchar_t*>(argument.c_str());
            DWORD_PTR result = 0;
            return SendMessageTimeoutW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
                       SMTO_ABORTIFHUNG, 5000, &result)
                != 0;
        }
        Sleep(100);
    }
    return false;
}

} // namespace dk2vr
