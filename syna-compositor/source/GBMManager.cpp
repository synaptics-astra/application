// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Synaptics Incorporated

#include "GBMManager.h"
#include <fstream>

bool GBMManager :: findDmaBufCapableDrmPath()
{
    for (int i = 0; i < MAX_DRI_CARD; ++i)
    {
        std::string namePath = "/sys/kernel/debug/dri/" + std::to_string(i) + "/name";
        std::ifstream nameFile(namePath);
        if (!nameFile)
            continue;

        std::string name;
        std::getline(nameFile, name);

        if (name.find("synaptics") != std::string::npos)
        {
            gbmDevicePath = "/dev/dri/card" + std::to_string(i);
            return true;
        }
    }

    return false;
}

GBMManager :: GBMManager()
{
    if (findDmaBufCapableDrmPath())
    {
        deviceFD = open(gbmDevicePath.c_str(), O_RDWR | FD_CLOEXEC);
        if (deviceFD < 0)
        {
            std::cout << "Error : Failed to open Card (" << gbmDevicePath << ")" << std::endl;
        }
        else
        {
            drmDropMaster(deviceFD);
            gbmDevice = gbm_create_device(deviceFD);

            if (!gbmDevice)
            {
                std::cout << "Error: Failed to create GBM Device" << std::endl;
                close(deviceFD);
                deviceFD = 0;
            }
        }
    }
    else {
        gbmDevice = nullptr;
        std::cerr << "No DRM device supports DMA-BUF export" << std::endl;
    }
}

gbm_device* GBMManager :: getGBMDevice() const
{
    return gbmDevice;
}

GBMManager :: ~GBMManager()
{
    if (gbmDevice)
    {
        gbm_device_destroy(gbmDevice);
        gbmDevice = nullptr;
    }
    if (deviceFD)
    {
        close(deviceFD);
        deviceFD = 0;
    }
}