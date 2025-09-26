// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Synaptics Incorporated

#ifndef GBM_MANAGER
#define GBM_MANAGER

#include "common.h"

class GBMManager
{
    public:
        GBMManager();
        ~GBMManager();
        gbm_device* getGBMDevice() const;

    private:
        std::string gbmDevicePath;
        gbm_device *gbmDevice;
        int deviceFD;

        bool findDmaBufCapableDrmPath();
};

using GBMManagerPtr = std::shared_ptr<GBMManager>;

#endif /* GBM_MANAGER */
