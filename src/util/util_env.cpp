#include "util_env.h"
#include "util_string.h"
#include "util_log.h"

namespace dxvk::env {
    std::string getEnvVariable(const std::string& name) {
        std::vector<WCHAR> variable;
        variable.resize(MAX_PATH + 1);

        auto length = ::GetEnvironmentVariableW(str::tows(name.c_str()).c_str(), variable.data(), MAX_PATH);
        variable.resize(length);

        return str::fromws(variable.data());
    }

    std::string getExecutablePath() {
        std::vector<WCHAR> path;
        path.resize(MAX_PATH + 1);

        auto length = ::GetModuleFileNameW(nullptr, path.data(), MAX_PATH);
        path.resize(length);

        return str::fromws(path.data());
    }

    std::string getExecutableName() {
        auto path = getExecutablePath();
        auto name = path.find_last_of('\\');

        return (name != std::string::npos)
            ? path.substr(name + 1)
            : path;
    }

    std::string getCurrentDateTime() {
        auto currentDateTime = std::time(nullptr);
        std::stringstream stream;
        stream << std::put_time(std::localtime(&currentDateTime), "%Y-%m-%d %H:%M:%S");
        return stream.str();
    }

    // Function to check for WAR to DLSS Bug 3634851 if an affected DLSS DLL is
    // detected this function will return true; false will be returned for all
    // non-affected DLLs.
    static bool isDLSSVersion20To24(void* pReturnAddress) {
        // Get file path of caller DLL
        char modulePath[MAX_PATH];
        HMODULE hModule = nullptr;
        if (!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (const char*)pReturnAddress, &hModule)) {
            // Failed to get the path, won't try to WAR.
            return false;
        }
        uint32_t pathLen = GetModuleFileName(hModule, modulePath, MAX_PATH);

        // Check if module name is either nvngx_dlss.dll, or matches the OTA
        // hashing pattern app_???????.bin
        if (strcasecmp("nvngx_dlss.dll", modulePath + pathLen - 14) == 0) {
            // nvngx_dlss.dll detected
        } else if (strncasecmp("app_", modulePath + pathLen - 15, 4) == 0 && strcasecmp(".bin", modulePath + pathLen - 4) == 0) {
            // app_???????.bin matches OTA pattern
        } else {
            // Filename does not match known patterns
            return false;
        }

        // Get the version metadata from the DLL
        uint32_t infoSize = GetFileVersionInfoSizeA(modulePath, nullptr);
        if (infoSize == 0) {
            return false;
        }

        auto* verInfo = new uint8_t[infoSize];

        if (!GetFileVersionInfoA(modulePath, 0, infoSize, verInfo)) {
            delete[] verInfo;
            return false;
        }

        VS_FIXEDFILEINFO* fixedInfo = nullptr;
        uint32_t fixedSize = 0;

        VerQueryValueA(verInfo, "\\", (void**)&fixedInfo, &fixedSize);

        // Double-check that we are reading a VS_FIXEDFILEINFO structure
        if (fixedInfo->dwSignature != 0xFEEF04BD) {
            delete[] verInfo;
            return false;
        }

        // Only DLSS 2.x versions prior to 2.4 are affected by this bug
        uint32_t majorVersion = (fixedInfo->dwFileVersionMS & 0xFFFF0000) >> 16;
        uint32_t minorVersion = fixedInfo->dwFileVersionMS & 0x0000FFFF;
        if (majorVersion != 2 || minorVersion >= 4) {
            delete[] verInfo;
            return false;
        }

        // Lastly compare the product name to be absolutely certain we were
        // called by DLSS
        char* productName;
        uint32_t productNameSize;
        VerQueryValueA(verInfo, R"(\StringFileInfo\040904E4\ProductName)", (void**)&productName, &productNameSize);

        if (strcmp(productName, "NVIDIA Deep Learning SuperSampling") == 0) {
            // A DLSS version between 2.0 and 2.4 was detected, applying WAR
            delete[] verInfo;
            return true;
        } else {
            delete[] verInfo;
            return false;
        }
    }

    bool needsAmpereSpoofing(NV_GPU_ARCHITECTURE_ID architectureId, void* pReturnAddress) {
        // Check if we need to workaround NVIDIA Bug 3634851
        if (architectureId >= NV_GPU_ARCHITECTURE_AD100 && isDLSSVersion20To24(pReturnAddress)) {
            log::write("Spoofing Ampere for Ada and later due to DLSS version 2.0-2.4");
            return true;
        }

        return false;
    }
}
