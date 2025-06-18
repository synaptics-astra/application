// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Synaptics Incorporated

#include "DRMBuffer.h"

DRMBuffer :: DRMBuffer()
{

}

DRMBuffer :: ~DRMBuffer()
{
    /* TBD */
}

int DRMBuffer :: createBuffer(struct gbm_device *gbmDevice, int width, int height, int pixelFormat)
{
    int deviceFD = 0;
    int ret = 0;
    uint ePixelFormat = GBM_FORMAT_ARGB8888;

    switch(pixelFormat)
    {
        case GBM_FORMAT_NV12:
            bufferSize = (width * height * 3) / 2;
            ePixelFormat = GBM_FORMAT_NV12;
            break;
        case GBM_FORMAT_ARGB8888:
            bufferSize = (width * height * 4);
            ePixelFormat = GBM_FORMAT_ARGB8888;
            break;
        default:
            std::cout << "Error : Unknown Format." << std::endl;
    }

    if (!bufferSize)
    {
        std::cout << "Error : Unable to Create Buffer of size " << bufferSize << " for format " << pixelFormat << std::endl;
        return -1;
    }

    gbmBufferObject = gbm_bo_create(gbmDevice, width, height, ePixelFormat, GBM_BO_USE_RENDERING|GBM_BO_USE_SCANOUT);
    if (gbmBufferObject == NULL)
    {
        std::cout << "Error : Unable to create GBM Buffer Object" << std::endl;
        return -1;
    }

    const union gbm_bo_handle handle = gbm_bo_get_handle_for_plane(gbmBufferObject, 0);
    deviceFD = gbm_device_get_fd(gbmDevice);
    ret = drmPrimeHandleToFD(deviceFD, handle.u32, DRM_RDWR | DRM_CLOEXEC, &sharedFD);
    if (ret)
    {
        std::cout << "ERROR: Unable to get Prime Handle to FD\n" << std::endl;
        return NULL;
    }

    virtualAddress = mmap(NULL, bufferSize, PROT_WRITE | PROT_READ, MAP_SHARED, sharedFD, 0);
    if (virtualAddress == MAP_FAILED) {
        std::cout << "ERROR: mmap failed \n" << std::endl;
        return NULL;
    }

    return 0;
}

int DRMBuffer :: freeBuffer()
{
    int returnValue;
    if(gbmBufferObject != NULL)
        gbm_bo_destroy(gbmBufferObject);
    gbmBufferObject = NULL;
    returnValue = munmap(virtualAddress, bufferSize);
    if(returnValue != 0)
    {
        std::cout << "Error : Unmapping Failed" << std::endl;
        return -1;
    }
    return 0;
}