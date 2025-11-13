// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Synaptics Incorporated

#include "EGLManager.h"

EGLManager :: EGLManager(GBMManagerPtr gbmManager)
{
    this->gbmManager = std::move(gbmManager);
    initializeEGLFunctions();
    initializeEGLDisplay();
}

int EGLManager :: initializeEGLDisplay()
{
    EGLConfig config;
    EGLint num_config;
    EGLint nativeVisualID, colorSpace;
    EGLint attribs[] =
    {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_NONE
    };
#ifdef USE_MALI_GPU
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
#else
    display = eglGetDisplay((EGLNativeDisplayType)(gbmManager->getGBMDevice()));
#endif
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

int EGLManager::createImageKHR(const std::array<int32_t, GBM_MAX_PLANES> sharedFDs, EGLImageKHR& image, int width, int height, PixelFormat pixelFormat, bool storeInMap)
{
    EGLint* attributes;

    if (storeInMap)
    {
        auto foundImage = imageMap.find(sharedFDs[0]);
        if (foundImage != imageMap.end())
        {
            image = foundImage->second;
            return 0;
        }
    }
    attributes = new EGLint[30];
    attributes[0] = EGL_WIDTH;
    attributes[1] = width;
    attributes[2] = EGL_HEIGHT;
    attributes[3] = height;
    attributes[4] = EGL_LINUX_DRM_FOURCC_EXT;
    attributes[6] = EGL_DMA_BUF_PLANE0_FD_EXT;
    attributes[7] = sharedFDs[0];
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
            attributes[13] = sharedFDs[0];
            attributes[14] = EGL_DMA_BUF_PLANE1_OFFSET_EXT;
            attributes[15] = (width * height);
            attributes[16] = EGL_DMA_BUF_PLANE1_PITCH_EXT;
            attributes[17] = width;
            attributes[18] = EGL_NONE;
            break;
        case NV12M:
            attributes[5] = DRM_FORMAT_NV12;
            attributes[11] = width;
            attributes[12] = EGL_DMA_BUF_PLANE1_FD_EXT;
            attributes[13] = sharedFDs[1];
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
        case UYVY:
            attributes[5] = DRM_FORMAT_UYVY;
            attributes[11] = (width * 2);
            attributes[12] = EGL_NONE;
            break;
        case I420:
            attributes[5] = DRM_FORMAT_YUV420;
            attributes[11] = width;
            attributes[12] = EGL_DMA_BUF_PLANE1_FD_EXT;
            attributes[13] = sharedFDs[1];
            attributes[14] = EGL_DMA_BUF_PLANE1_OFFSET_EXT;
            attributes[15] = 0;
            attributes[16] = EGL_DMA_BUF_PLANE1_PITCH_EXT;
            attributes[17] = (width / 2);
            attributes[18] = EGL_DMA_BUF_PLANE2_FD_EXT;
            attributes[19] = sharedFDs[2];
            attributes[20] = EGL_DMA_BUF_PLANE2_OFFSET_EXT;
            attributes[21] = 0;
            attributes[22] = EGL_DMA_BUF_PLANE2_PITCH_EXT;
            attributes[23] = (width / 2);
            attributes[24] = EGL_NONE;
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

    if (storeInMap)
    {
        imageMap[sharedFDs[0]] = image;
    }

    delete[] attributes;

    return 0;
}

int EGLManager :: destroyImageKHR(EGLImageKHR& image)
{
    pfneglDestroyImageKHR(display, image);
    image = EGL_NO_IMAGE_KHR;
    return 0;
}

EGLDisplay EGLManager :: getDisplay() const
{
    return display;
}

EGLContext EGLManager :: getContext() const
{
    return context;
}

EGLManager :: ~EGLManager()
{
    for (auto& imagePair : imageMap)
    {
        destroyImageKHR(imagePair.second);
    }

    imageMap.clear();
}