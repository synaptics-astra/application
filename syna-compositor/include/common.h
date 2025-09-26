// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Synaptics Incorporated

#ifndef COMMON_H
#define COMMON_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <drm_fourcc.h>
#include <iostream>
#include <vector>
#include <initializer_list>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <gbm.h>
#include <memory>
#include <array>

extern "C" {
    #include <drm/drm.h>
}

extern "C" int drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd);
extern "C" int drmDropMaster(int fd);

// #define DEBUG_LOGS
#define MAX_DRI_CARD 16

enum PixelFormat
{
    ARGB,
    BGRA,
    NV12,
    NV12M,
    YUY2,
    UYVY,
    I420
};

struct LayoutItem
{
    GLuint index;
    GLfloat xPos;
    GLfloat yPos;
    GLfloat zPos;
    GLfloat width;
    GLfloat height;

    LayoutItem(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w, GLfloat h)
        : index(index), xPos(x), yPos(y), zPos(z), width(w), height(h) {}
};

extern PFNGLEGLIMAGETARGETTEXTURE2DOESPROC pfnglEGLImageTargetTexture2DOES_temp;
#endif /* COMMON_H */
