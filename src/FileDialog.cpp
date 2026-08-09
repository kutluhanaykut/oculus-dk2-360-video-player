#include "FileDialog.hpp"

#include <Windows.h>
#include <commdlg.h>

#include <array>

namespace dk2vr {

std::optional<std::filesystem::path> openVideoFileDialog()
{
    std::array<wchar_t, 32768> fileName {};
    constexpr wchar_t filter[] =
        L"Video dosyalari\0*.mp4;*.mkv;*.webm;*.mov;*.avi;*.m4v;*.ts;*.mts;*.m2ts\0"
        L"Tum dosyalar\0*.*\0\0";

    OPENFILENAMEW dialog {};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = fileName.data();
    dialog.nMaxFile = static_cast<DWORD>(fileName.size());
    dialog.lpstrTitle = L"360 VR video secin";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;

    if (GetOpenFileNameW(&dialog) == TRUE) {
        return std::filesystem::path(fileName.data());
    }
    return std::nullopt;
}

std::filesystem::path executableDirectory()
{
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        return std::filesystem::current_path();
    }
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}

} // namespace dk2vr
