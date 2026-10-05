#include "Application.hpp"
#include "BrowserIntegration.hpp"

#include "Process.hpp"

#include <Windows.h>
#include <shellapi.h>

#include <exception>
#include <string>

int APIENTRY wWinMain(HINSTANCE /*instance*/, HINSTANCE /*previousInstance*/,
    wchar_t* /*commandLine*/, int /*showCommand*/)
{
    try {
        // DK2VRPlayer.exe <url | dk2vr: link | video file>. When a player is
        // already running (e.g. a "DK2'de ac" click while watching), hand
        // the argument to it instead of opening a second window.
        std::wstring argument;
        int argumentCount = 0;
        if (wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount)) {
            if (argumentCount > 1) {
                argument = arguments[1];
            }
            LocalFree(arguments);
        }
        if (dk2vr::forwardToRunningInstance(argument)) {
            return 0;
        }

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
        if (!argument.empty()) {
            application.openFromCommandLine(argument);
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
