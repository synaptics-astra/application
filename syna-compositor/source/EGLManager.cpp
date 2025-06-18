// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Synaptics Incorporated

#include "EGLManager.h"

EGLManager :: EGLManager()
{

}

int EGLManager :: initializeEGLDisplay(const char* devicePath)
{
    EGLConfig config;
    EGLint num_config;
    EGLint nativeVisualID, colorSpace;
    drmFD = 0;
    initializeEGLFunctions();
    EGLint attribs[] =
    {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_NONE
    };

    if (initGBM(devicePath) < 0)
        return -1;

    display = eglGetDisplay((EGLNativeDisplayType)(gbmDevice));
    if ((display == EGL_NO_DISPLAY) && (eglGetError() != EGL_SUCCESS)) {
        std::cout << "Error : Failed to get EGL Display. (Error Code : " << eglGetError() << " )" << std::endl;
        return -1;
    }

    if ((!eglInitialize(display, NULL, NULL)) && (eglGetError() != EGL_SUCCESS))
    {
        std::cout << "Error : Failed to Initialize EGL. (Error Code : " << eglGetError() << " )" << std::endl;
        return -1;
    }

    eglChooseConfig(display, attribs, &config, 1, &num_config);

    EGLint context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribs);
    if ((context == EGL_NO_CONTEXT) && (eglGetError() != EGL_SUCCESS))
    {
        std::cout << "Error : Failed to Create EGL Context. (Error Code : " << eglGetError() << " )" << std::endl;
        return -1;
    }

    if ((eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context) == EGL_FALSE) && (eglGetError() != EGL_SUCCESS))
    {
        std::cout << "Error : Failed to make EGL Current. (Error Code : " << eglGetError() << " )" << std::endl;
        return -1;
    }

    return 0;
}

int EGLManager :: initGBM(const char* devicePath)
{
    if (!drmFD)
    {
        drmFD = open(devicePath, O_RDWR | FD_CLOEXEC);
        if (drmFD < 0)
        {
            std::cout << "Error : Failed to open Card (" << devicePath << ")" << std::endl;
            return -1;
        }
        else
        {
            drmDropMaster(drmFD);
            gbmDevice = gbm_create_device(drmFD);
            if (!gbmDevice)
            {
                std::cout << "Error: Failed to create GBM Device" << std::endl;
                close(drmFD);
                drmFD = 0;
                return -1;
            }
        }
    }
    return 0;
}

void EGLManager :: initializeEGLFunctions()
{
    pfneglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    if (!pfneglCreateImageKHR)
        std::cout << "Error for pfneglCreateImageKHR->eglGetProcAddress" << std::endl;

    pfnglEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    pfnglEGLImageTargetTexture2DOES_temp = pfnglEGLImageTargetTexture2DOES;
    if (!pfnglEGLImageTargetTexture2DOES)
        std::cout << "Error for pfnglEGLImageTargetTexture2DOES->eglGetProcAddress" << std::endl;

    pfneglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    if (!pfneglDestroyImageKHR)
        std::cout << "Error for pfneglDestroyImageKHR->eglGetProcAddress" << std::endl;

    pfnglEGLImageTargetRenderbufferStorageOES = (PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC)eglGetProcAddress("glEGLImageTargetRenderbufferStorageOES");
    if (!pfnglEGLImageTargetRenderbufferStorageOES)
        std::cout << "Error for pfnglEGLImageTargetRenderbufferStorageOES->eglGetProcAddress" << std::endl;
}

int EGLManager::createImageKHR(int sharedFD1, int sharedFD2, EGLImageKHR& image, int width, int height, PixelFormat pixelFormat)
{
    EGLint* attributes;

    auto foundImage = imageMap.find(sharedFD1);
    if (foundImage != imageMap.end())
    {
        image = foundImage->second;
        return 0;
    }
    attributes = new EGLint[20];
    attributes[0] = EGL_WIDTH;
    attributes[1] = width;
    attributes[2] = EGL_HEIGHT;
    attributes[3] = height;
    attributes[4] = EGL_LINUX_DRM_FOURCC_EXT;
    attributes[6] = EGL_DMA_BUF_PLANE0_FD_EXT;
    attributes[7] = sharedFD1;
    attributes[8] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
    attributes[9] = 0;
    attributes[10] = EGL_DMA_BUF_PLANE0_PITCH_EXT;

    switch (pixelFormat)
    {
        case ARGB:
            attributes[5] = DRM_FORMAT_ARGB8888;
            attributes[11] = (width * 4);
            attributes[12] = EGL_NONE;
            break;
        case NV12:
            attributes[5] = DRM_FORMAT_NV12;
            attributes[11] = width;
            attributes[12] = EGL_DMA_BUF_PLANE1_FD_EXT;
            attributes[13] = sharedFD2;
            attributes[14] = EGL_DMA_BUF_PLANE1_OFFSET_EXT;
            attributes[15] = 0;
            attributes[16] = EGL_DMA_BUF_PLANE1_PITCH_EXT;
            attributes[17] = width;
            attributes[18] = EGL_NONE;
            break;
        case NV12M:
            attributes[5] = 0x3231564D;
            attributes[11] = width;
            attributes[12] = EGL_DMA_BUF_PLANE1_FD_EXT;
            attributes[13] = sharedFD2;
            attributes[14] = EGL_DMA_BUF_PLANE1_OFFSET_EXT;
            attributes[15] = 0;
            attributes[16] = EGL_DMA_BUF_PLANE1_PITCH_EXT;
            attributes[17] = width;
            attributes[18] = EGL_NONE;
            break;
        case YUY2:
            attributes[5] = DRM_FORMAT_YUYV;
            attributes[11] = (width * 2);
            attributes[12] = EGL_NONE;
            break;
        default:
            break;
    }

    image = pfneglCreateImageKHR(display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attributes);

    if (image == EGL_NO_IMAGE_KHR)
    {
        std::cout << "CreateImageKHR Failed. Error Code - 0x" << std::hex << eglGetError() << std::endl;
        return -1;
    }

    std::map<GLint, EGLImageKHR> imageMapLocal;
    imageMapLocal = imageMap;
    imageMapLocal[sharedFD1] = image;
    imageMap = imageMapLocal;

    delete[] attributes;

    return 0;
}

int EGLManager :: destroyImageKHR(EGLImageKHR& image)
{
    pfneglDestroyImageKHR(display, image);
    image = EGL_NO_IMAGE_KHR;
    return 0;
}

EGLManager :: ~EGLManager()
{
    if (gbmDevice)
        gbm_device_destroy(gbmDevice);
    gbmDevice = nullptr;

    if (drmFD)
        close(drmFD);
    drmFD = 0;
}