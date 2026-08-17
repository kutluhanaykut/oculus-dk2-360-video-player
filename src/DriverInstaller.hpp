#pragma once

#include <filesystem>
#include <string>

namespace dk2vr {

// Installs the WinUSB driver for the Oculus Rift DK2 tracking device using
// pnputil and a generated INF file. This replaces the manual Zadig workflow.
class DriverInstaller {
public:
    DriverInstaller() = default;

    // Returns true if the current process is running with administrator rights.
    [[nodiscard]] static bool isElevated();

    // Checks whether the DK2 tracking device is currently bound to WinUSB.
    // Returns true if the device is present and bound to WinUSB.
    [[nodiscard]] static bool isDk2WinUsbBound();

    // Installs the WinUSB driver for the DK2 tracking device.
    // Returns true on success, false on failure (error is filled in).
    [[nodiscard]] bool installDk2WinUsbDriver(std::string& error);

    // Returns a human-readable description of the last operation result.
    [[nodiscard]] const std::string& lastMessage() const noexcept;

private:
    // Writes the DK2 WinUSB INF file next to the executable.
    [[nodiscard]] bool writeInfFile(const std::filesystem::path& infPath, std::string& error);

    std::string lastMessage_;
};

} // namespace dk2vr
