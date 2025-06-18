// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Synaptics Incorporated

#include "common.h"

PFNEGLCREATEIMAGEKHRPROC pfneglCreateImageKHR = NULL;
PFNGLEGLIMAGETARGETTEXTURE2DOESPROC pfnglEGLImageTargetTexture2DOES = NULL;
PFNEGLDESTROYIMAGEKHRPROC pfneglDestroyImageKHR = NULL;
PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC pfnglEGLImageTargetRenderbufferStorageOES = NULL;


int initializeEGLFunctions()
{
    pfneglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    if (!pfneglCreateImageKHR)
    {
        std::cout << "Error for pfneglCreateImageKHR->eglGetProcAddress" << std::endl;
        return -1;
    }

    pfnglEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    if (!pfnglEGLImageTargetTexture2DOES)
    {
        std::cout << "Error for pfnglEGLImageTargetTexture2DOES->eglGetProcAddress" << std::endl;
    }

    pfneglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    if (!pfneglDestroyImageKHR)
    {
        std::cout << "Error for pfneglDestroyImageKHR->eglGetProcAddress" << std::endl;
    }

    pfnglEGLImageTargetRenderbufferStorageOES = (PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC)eglGetProcAddress("glEGLImageTargetRenderbufferStorageOES");
    if (!pfnglEGLImageTargetRenderbufferStorageOES)
    {
        std::cout << "Error for pfnglEGLImageTargetRenderbufferStorageOES->eglGetProcAddress" << std::endl;
    }

    return 0;
}
