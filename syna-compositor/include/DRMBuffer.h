// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Synaptics Incorporated

#ifndef DRM_BUFFER_H
#define DRM_BUFFER_H

#include "common.h"

class DRMBuffer
{
    public:
        DRMBuffer();
        ~DRMBuffer();

        int getSharedFD()
        {
            return sharedFD;
        }
        void* getVirtualAddress()
        {
            return virtualAddress;
        }
        int createBuffer(struct gbm_device *gbmDevice, int width, int height, int pixelFormat);
        int freeBuffer();

    private:
        void* virtualAddress;
        uint bufferSize;
        void* physicalAddress;
        int sharedFD;
        struct gbm_bo *gbmBufferObject;
};

#endif /* DRM_BUFFER_H */