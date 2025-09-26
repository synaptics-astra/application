// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Synaptics Incorporated

#include "DRMBuffer.h"

DRMBuffer :: DRMBuffer(GBMManagerPtr gbmManager)
{
    this->gbmManager = std::move(gbmManager);
}

int DRMBuffer :: createBuffer(int width, int height, PixelFormat pFormat)
{
    int deviceFD = 0;
    bufferSize = 0;
    uint ePixelFormat = GBM_FORMAT_ARGB8888;

    /*
    * Calculate exact buffer size and plane layout per format,
    * since gbm_bo_create allocates correctly only for ARGB8888.
    *
    * Size formulas:
    *   - ARGB8888        : width * height * 4 (4 bytes per pixel)
    *   - NV12/NV12M/I420 : width * height * 3 / 2 (Y + subsampled UV)
    *   - YUY2/UYVY       : width * height * 2 (2 bytes per pixel packed YUV)
    *
    * Note: sharedPlanes[index].width/height represent per-plane dimensions
    * used only for buffer size calculation, and may not match the
    * actual image resolution for all formats.
    */

    switch(pFormat)
    {
        case NV12:
            bufferSize = (width * height * 3) / 2;
            nPlanes = 1;

            sharedPlanes[0].width  = width;
            sharedPlanes[0].height = height;
            sharedPlanes[0].size   = bufferSize;
            break;
        case NV12M:
            bufferSize = (width * height * 3) / 2;
            nPlanes = 2;

            sharedPlanes[0].width  = width;
            sharedPlanes[0].height = height;
            sharedPlanes[0].size   = width * height;

            sharedPlanes[1].width  = width;
            sharedPlanes[1].height = height / 2;
            sharedPlanes[1].size   = (width * height) / 2;
            break;
        case ARGB:
            bufferSize = (width * height * 4);
            nPlanes = 1;
            sharedPlanes[0].width = width;
            sharedPlanes[0].height = height;
            sharedPlanes[0].size = bufferSize;
            break;
        case I420:
            bufferSize = (width * height * 3) / 2;
            nPlanes = 3;

            sharedPlanes[0].width  = width;
            sharedPlanes[0].height = height;
            sharedPlanes[0].size   = width * height;

            sharedPlanes[1].width  = width / 2;
            sharedPlanes[1].height = height / 2;
            sharedPlanes[1].size   = (width * height) / 4;

            sharedPlanes[2].width  = width / 2;
            sharedPlanes[2].height = height / 2;
            sharedPlanes[2].size   = (width * height) / 4;
            break;
        case YUY2:
        case UYVY:
            bufferSize = width * height * 2;
            nPlanes = 1;

            sharedPlanes[0].width  = width;
            sharedPlanes[0].height = height;
            sharedPlanes[0].size   = bufferSize;
            break;
        default:
            std::cout << "Error : Format Not Supported" << std::endl;
            return -1;
    }

    if (!bufferSize)
    {
        std::cout << "Error : Unable to Create Buffer of size " << bufferSize << " for format " << pFormat << std::endl;
        return -1;
    }

    deviceFD = gbm_device_get_fd(gbmManager->getGBMDevice());

    for (int i = 0; i < nPlanes; i++)
    {
        sharedPlanes[i].gbmBufferObject = gbm_bo_create(gbmManager->getGBMDevice(), sharedPlanes[i].width, sharedPlanes[i].height, ePixelFormat, GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR);
        if (sharedPlanes[i].gbmBufferObject == NULL)
        {
            std::cout << "Error : Unable to create GBM Buffer Object [" << errno << "]" << std::endl;
            return -1;
        }
        const union gbm_bo_handle handle = gbm_bo_get_handle_for_plane(sharedPlanes[i].gbmBufferObject, 0);
        int ret = drmPrimeHandleToFD(deviceFD, handle.u32, DRM_RDWR | DRM_CLOEXEC, &sharedPlanes[i].fd);
        if (ret)
        {
            std::cout << "ERROR: Unable to get Prime Handle to FD\n" << std::endl;
            return -1;
        }

        sharedPlanes[i].virtualAddress = mmap(NULL, sharedPlanes[i].size, PROT_WRITE | PROT_READ, MAP_SHARED, sharedPlanes[i].fd, 0);
        if (sharedPlanes[i].virtualAddress == MAP_FAILED) {
            std::cout << "ERROR: mmap failed \n" << std::endl;
            return -1;
        }
    }

    return 0;
}

const std::array<SharedPlane, GBM_MAX_PLANES>& DRMBuffer :: getSharedPlanes() const
{
    return sharedPlanes;
}

uint DRMBuffer :: getNumPlanes() const
{
    return nPlanes;
}

void DRMBuffer :: freeBuffer()
{
    for (int i = 0; i < nPlanes; i++)
    {
        if (sharedPlanes[i].fd > 0)
        {
            close(sharedPlanes[i].fd);
            sharedPlanes[i].fd = -1;
        }

        if ((sharedPlanes[i].virtualAddress != nullptr) && (sharedPlanes[i].size > 0))
        {
            if (munmap(sharedPlanes[i].virtualAddress, sharedPlanes[i].size) != 0)
            {
                std::cout << "Error : Unmapping Failed" << std::endl;
            }
            sharedPlanes[i].virtualAddress = NULL;
            sharedPlanes[i].size = 0;
        }

        if(sharedPlanes[i].gbmBufferObject != NULL)
        {
            gbm_bo_destroy(sharedPlanes[i].gbmBufferObject);
            sharedPlanes[i].gbmBufferObject = NULL;
        }
    }

    bufferSize = 0;
    nPlanes = 0;
}

DRMBuffer :: ~DRMBuffer()
{
    freeBuffer();
}
