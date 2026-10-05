#include "DriverInstaller.hpp"

#include "Logger.hpp"
#include "Process.hpp"

#include <Windows.h>

#include <fstream>

namespace dk2vr {
namespace {

// Returns the directory containing the current executable.
std::filesystem::path executableDirectory()
{
    wchar_t buffer[MAX_PATH] {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return std::filesystem::current_path();
    }
    std::filesystem::path path(buffer);
    return path.parent_path();
}

} // namespace

bool DriverInstaller::isElevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }
    TOKEN_ELEVATION elevation {};
    DWORD size = 0;
    const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation,
        sizeof(elevation), &size);
    CloseHandle(token);
    return ok != FALSE && elevation.TokenIsElevated != 0;
}

bool DriverInstaller::writeInfFile(const std::filesystem::path& infPath, std::string& error)
{
    // WinUSB INF for the Oculus Rift DK2 tracking device (VID 2833, PID 0021).
    // PID 2021 is the hub inside the headset: binding WinUSB to it would cut
    // off the tracker behind it, so it must never be listed here.
    const std::string infContent =
        "[Version]\r\n"
        "Signature=\"$Windows NT$\"\r\n"
        "Class=USBDevice\r\n"
        "ClassGuid={88BA0320-625A-449f-A0C5-25DD775E89D3}\r\n"
        "Provider=%ProviderName%\r\n"
        "DriverVer=08/18/2026,1.0.0.0\r\n"
        "\r\n"
        "[Manufacturer]\r\n"
        "%ProviderName%=DeviceList,NTamd64\r\n"
        "\r\n"
        "[DeviceList.NTamd64]\r\n"
        "%DeviceName%=DriverInstall, USB\\VID_2833&PID_0021\r\n"
        "\r\n"
        "[DriverInstall]\r\n"
        "Include=winusb.inf\r\n"
        "Needs=WINUSB,CoInstallers_NT\r\n"
        "\r\n"
        "[DriverInstall.Services]\r\n"
        "Include=winusb.inf\r\n"
        "Needs=WINUSB,CoInstallers_NT_Services\r\n"
        "\r\n"
        "[DriverInstall.Wdf]\r\n"
        "KmdfService=WINUSB,WinUsb_Service\r\n"
        "\r\n"
        "[Strings]\r\n"
        "ProviderName=\"DK2 VR Player\"\r\n"
        "DeviceName=\"Oculus Rift DK2 Tracking\"\r\n";

    std::ofstream file(infPath, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "INF dosyasi yazilamadi: " + infPath.string();
        return false;
    }
    file.write(infContent.data(), static_cast<std::streamsize>(infContent.size()));
    file.close();
    if (!file) {
        error = "INF dosyasi kapatilamadi: " + infPath.string();
        return false;
    }
    return true;
}

bool DriverInstaller::installDk2WinUsbDriver(std::string& error)
{
    lastMessage_.clear();

    if (!isElevated()) {
        error = "Surucu kurulumu icin yonetici haklari gerekir. "
            "Uygulamayi 'Yonetici olarak calistir' ile yeniden baslatin.";
        log::error("DriverInstaller: " + error);
        return false;
    }

    // Write the INF file next to the executable.
    const std::filesystem::path infPath = executableDirectory() / L"dk2-winusb.inf";
    if (!writeInfFile(infPath, error)) {
        log::error("DriverInstaller: " + error);
        return false;
    }
    log::info("DriverInstaller: INF dosyasi yazildi: " + infPath.string());

    // Use pnputil to add and install the driver.
    // pnputil /add-driver <inf> /install
    const std::filesystem::path pnputilPath = L"C:\\Windows\\System32\\pnputil.exe";
    const ProcessResult result = runProcess(pnputilPath,
        {L"/add-driver", infPath.wstring(), L"/install"});

    if (!result.started) {
        error = "pnputil baslatilamadi: " + result.error;
        log::error("DriverInstaller: " + error);
        return false;
    }

    log::info("DriverInstaller: pnputil ciktisi: " + result.output);

    // pnputil returns 0 on success. Some versions return 3010 (reboot required)
    // which is still a successful install.
    if (result.exitCode != 0 && result.exitCode != 3010) {
        error = "Surucu kurulumu basarisiz oldu (pnputil hata kodu "
            + std::to_string(result.exitCode) + ").\n" + result.output;
        log::error("DriverInstaller: " + error);
        return false;
    }

    if (result.exitCode == 3010) {
        lastMessage_ = "Surucu kuruldu ancak sistem yeniden baslatilmasi gerekebilir. "
            "DK2'yi yeniden taramayi deneyin.";
    } else {
        lastMessage_ = "WinUSB surucusu DK2 izleme cihazina basariyla kuruldu. "
            "DK2'yi yeniden taramayi deneyin.";
    }
    log::info("DriverInstaller: " + lastMessage_);
    return true;
}

const std::string& DriverInstaller::lastMessage() const noexcept
{
    return lastMessage_;
}

} // namespace dk2vr
