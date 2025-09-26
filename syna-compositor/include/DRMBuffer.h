// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Synaptics Incorporated

#ifndef DRM_BUFFER_H
#define DRM_BUFFER_H

#include "common.h"
#include "GBMManager.h"

typedef struct {
    struct gbm_bo *gbmBufferObject;
    int fd;
    int size;
    int width;
    int height;
    void* virtualAddress;
} SharedPlane;

class DRMBuffer
{
    public:
        explicit DRMBuffer(GBMManagerPtr gbmManager);
        ~DRMBuffer();

        const std::array<SharedPlane, GBM_MAX_PLANES>& getSharedPlanes() const;
        uint getNumPlanes() const;
        int createBuffer(int width, int height, PixelFormat pFormat);

    private:
        GBMManagerPtr gbmManager;

        uint bufferSize;
        uint nPlanes;
        std::array<SharedPlane, GBM_MAX_PLANES> sharedPlanes;

        void freeBuffer();
};

#endif /* DRM_BUFFER_H */
