// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Synaptics Incorporated

#ifndef EGL_MANAGER_H
#define EGL_MANAGER_H

#include "common.h"

enum PixelFormat
{
    ARGB,
    NV12,
    NV12M,
    YUY2
};

class EGLManager
{
    public:
        EGLManager();
        ~EGLManager();
        struct gbm_device *getGBMDevice()
        {
            return gbmDevice;
        }
        void setPixelFormat(PixelFormat pixelFormat)
        {
            this->pixFormat = pixelFormat;
        }
        EGLDisplay getDisplay() const { return display; }
        EGLContext getContext() const { return context; }
        int initializeEGLDisplay(const char* devicePath);
        int createImageKHR(int sharedFD1, int sharedFD2, EGLImageKHR& image, int width, int height, PixelFormat pixelFormat);
        int destroyImageKHR(EGLImageKHR& image);
        PFNEGLCREATEIMAGEKHRPROC pfneglCreateImageKHR;
        PFNGLEGLIMAGETARGETTEXTURE2DOESPROC pfnglEGLImageTargetTexture2DOES;
        PFNEGLDESTROYIMAGEKHRPROC pfneglDestroyImageKHR;
        PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC pfnglEGLImageTargetRenderbufferStorageOES;

    private:
        EGLDisplay display;
        EGLContext context;
        struct gbm_device *gbmDevice;
        int drmFD;
        PixelFormat pixFormat;
        std::map<GLint, EGLImageKHR> imageMap;
        int initGBM(const char* devicePath);
        void initializeEGLFunctions();
};

#endif /* EGL_MANAGER_H */
