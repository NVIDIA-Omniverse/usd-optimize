// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "omni/scene.optimizer/core/CudaUtils.h"

#include <cstring>
#include <mutex>

// clang-format off
// The order of includes is important for Windows.
#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#    include <setupapi.h>
#    pragma comment(lib, "setupapi.lib")
#else
#    include <dirent.h>
#    include <dlfcn.h>
#    include <fstream>
#    include <string>
#endif
// clang-format on


namespace omni::scene::optimizer
{

/// Check if any NVIDIA GPU is present by querying hardware device information.
/// This avoids calling CUDA functions which may crash if no GPU is present.
static bool hasNvidiaGpu()
{
#if defined(_WIN32)
    // GUID for display adapters
    static const GUID GUID_DEVCLASS_DISPLAY = { 0x4d36e968,
                                                0xe325,
                                                0x11ce,
                                                { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } };

    HDEVINFO deviceInfoSet = SetupDiGetClassDevsA(&GUID_DEVCLASS_DISPLAY, NULL, NULL, DIGCF_PRESENT);
    if (deviceInfoSet == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    SP_DEVINFO_DATA deviceInfoData;
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    bool foundNvidia = false;
    for (DWORD i = 0; SetupDiEnumDeviceInfo(deviceInfoSet, i, &deviceInfoData); i++)
    {
        char buffer[256]{};
        if (SetupDiGetDeviceRegistryPropertyA(deviceInfoSet,
                                              &deviceInfoData,
                                              SPDRP_HARDWAREID,
                                              NULL,
                                              (PBYTE)buffer,
                                              sizeof(buffer),
                                              NULL))
        {
            // Check if this is an NVIDIA device (VEN_10DE is NVIDIA's vendor ID)
            if (strstr(buffer, "VEN_10DE") != NULL || strstr(buffer, "ven_10de") != NULL)
            {
                foundNvidia = true;
                break;
            }
        }
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);
    return foundNvidia;

#elif defined(__linux__)
    // Scan sysfs PCI devices for an NVIDIA GPU.
    // Each device exposes a "vendor" file (e.g. "0x10de") and a "class" file
    // (e.g. "0x030000" for VGA, "0x030200" for 3D controller).
    // We check for NVIDIA vendor ID 0x10de with display class 0x03xxxx.
    const char* sysPath = "/sys/bus/pci/devices";
    DIR* dir = opendir(sysPath);
    if (!dir)
    {
        return false;
    }

    bool foundNvidia = false;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        if (entry->d_name[0] == '.')
        {
            continue;
        }

        std::string devicePath = std::string(sysPath) + "/" + entry->d_name;

        // Read vendor ID
        std::ifstream vendorFile(devicePath + "/vendor");
        if (!vendorFile.is_open())
        {
            continue;
        }
        std::string vendor;
        std::getline(vendorFile, vendor);
        vendorFile.close();

        // 0x10de is NVIDIA's PCI vendor ID
        if (vendor != "0x10de")
        {
            continue;
        }

        // Read PCI class to ensure this is a display device (class 0x03xxxx)
        // and not e.g. an audio controller on the GPU
        std::ifstream classFile(devicePath + "/class");
        if (!classFile.is_open())
        {
            continue;
        }
        std::string pciClass;
        std::getline(classFile, pciClass);
        classFile.close();

        // PCI class is a 24-bit value like "0x030000" (VGA) or "0x030200" (3D controller).
        // The top byte (0x03) is the base class for display controllers.
        if (pciClass.size() >= 4 && pciClass.substr(0, 4) == "0x03")
        {
            foundNvidia = true;
            break;
        }
    }

    closedir(dir);
    return foundNvidia;

#else
    return false;
#endif
}

bool isCudaAvailable()
{
    static std::once_flag initFlag;
    static bool available = false;

    std::call_once(initFlag,
                   []()
                   {
                       // Driver API function pointer types
                       typedef int (*PFN_cuInit)(unsigned int);
                       typedef int (*PFN_cuDeviceGetCount)(int*);

                       PFN_cuInit pfn_cuInit = nullptr;
                       PFN_cuDeviceGetCount pfn_cuDeviceGetCount = nullptr;

                       // First check if any NVIDIA GPU is present using OS APIs.
                       // This avoids calling cuInit which can crash if driver is
                       // installed but no GPU is present.
                       if (!hasNvidiaGpu())
                       {
                           SO_LOG_WARN("No NVIDIA GPU found. GPU acceleration disabled.");
                           return;
                       }

#if defined(_WIN32)
                       HMODULE hCudaDriver = LoadLibraryA("nvcuda.dll");
                       if (hCudaDriver == NULL)
                       {
                           SO_LOG_WARN("Could not load nvcuda.dll. GPU acceleration disabled.");
                           return;
                       }

                       pfn_cuInit = (PFN_cuInit)GetProcAddress(hCudaDriver, "cuInit");
                       pfn_cuDeviceGetCount = (PFN_cuDeviceGetCount)GetProcAddress(hCudaDriver, "cuDeviceGetCount");

                       if (!pfn_cuInit || !pfn_cuDeviceGetCount)
                       {
                           SO_LOG_WARN("Could not get CUDA driver functions. GPU acceleration disabled.");
                           FreeLibrary(hCudaDriver);
                           return;
                       }

                       int err = pfn_cuInit(0);
                       if (err != 0)
                       {
                           FreeLibrary(hCudaDriver);
                           return;
                       }

                       int deviceCount = 0;
                       err = pfn_cuDeviceGetCount(&deviceCount);
                       available = (err == 0 && deviceCount > 0);
                       FreeLibrary(hCudaDriver);

#elif defined(__linux__)
            void* hCudaDriver = dlopen("libcuda.so", RTLD_NOW);
            if (hCudaDriver == NULL)
            {
                // WSL and possibly other systems might require the .1 suffix
                hCudaDriver = dlopen("libcuda.so.1", RTLD_NOW);
                if (hCudaDriver == NULL)
                {
                    SO_LOG_WARN("Could not load CUDA driver. GPU acceleration disabled.");
                    return;
                }
            }

            pfn_cuInit = (PFN_cuInit)dlsym(hCudaDriver, "cuInit");
            pfn_cuDeviceGetCount = (PFN_cuDeviceGetCount)dlsym(hCudaDriver, "cuDeviceGetCount");

            if (!pfn_cuInit || !pfn_cuDeviceGetCount)
            {
                SO_LOG_WARN("Could not get CUDA driver functions. GPU acceleration disabled.");
                dlclose(hCudaDriver);
                return;
            }

            int err = pfn_cuInit(0);
            if (err != 0)
            {
                dlclose(hCudaDriver);
                return;
            }

            int deviceCount = 0;
            err = pfn_cuDeviceGetCount(&deviceCount);
            available = (err == 0 && deviceCount > 0);
            dlclose(hCudaDriver);
#endif
                   });

    return available;
}

} // namespace omni::scene::optimizer
