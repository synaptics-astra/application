// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Synaptics Incorporated

#ifndef EGL_MANAGER_H
#define EGL_MANAGER_H

#include "common.h"
#include "GBMManager.h"
#include <map>

class EGLManager
{
    public:
        explicit EGLManager(GBMManagerPtr gbmManager);
        ~EGLManager();
        EGLDisplay getDisplay() const;
        EGLContext getContext() const;
        int createImageKHR(const std::array<int32_t, GBM_MAX_PLANES> sharedFDs, EGLImageKHR& image, int width, int height, PixelFormat pixelFormat, bool storeInMap);
        int destroyImageKHR(EGLImageKHR& image);

    private:
        EGLDisplay display;
        EGLContext context;
        GBMManagerPtr gbmManager;
        int drmFD;
        std::map<GLint, EGLImageKHR> imageMap;

        void initializeEGLFunctions();
        int initializeEGLDisplay();
        PFNEGLCREATEIMAGEKHRPROC pfneglCreateImageKHR;
        PFNGLEGLIMAGETARGETTEXTURE2DOESPROC pfnglEGLImageTargetTexture2DOES;
        PFNEGLDESTROYIMAGEKHRPROC pfneglDestroyImageKHR;
        PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC pfnglEGLImageTargetRenderbufferStorageOES;
};

#endif /* EGL_MANAGER_H */
