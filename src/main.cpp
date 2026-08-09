#include "Application.hpp"

#include "Process.hpp"

#include <Windows.h>

#include <exception>
#include <string>

int APIENTRY wWinMain(HINSTANCE /*instance*/, HINSTANCE /*previousInstance*/,
    wchar_t* /*commandLine*/, int /*showCommand*/)
{
    try {
        dk2vr::Application application;
        std::string error;
        if (!application.initialize(error)) {
            const std::wstring wideError = dk2vr::utf8ToWide(
                "DK2 360 VR Player baslatilamadi.\n\n" + error
                + "\n\nAyrintilar icin DK2VRPlayer.log dosyasina bakin.");
            MessageBoxW(nullptr, wideError.c_str(), L"DK2 360 VR Player - Hata",
                MB_OK | MB_ICONERROR);
            return 1;
        }
        return application.run();
    } catch (const std::exception& exception) {
        const std::wstring message = dk2vr::utf8ToWide(
            std::string("Beklenmeyen hata:\n\n") + exception.what());
        MessageBoxW(nullptr, message.c_str(), L"DK2 360 VR Player - Kritik Hata",
            MB_OK | MB_ICONERROR);
        return 2;
    } catch (...) {
        MessageBoxW(nullptr, L"Bilinmeyen kritik hata.", L"DK2 360 VR Player - Kritik Hata",
            MB_OK | MB_ICONERROR);
        return 3;
    }
}
