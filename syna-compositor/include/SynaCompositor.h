// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Synaptics Incorporated

#ifndef SYNA_COMPOSITOR_H
#define SYNA_COMPOSITOR_H

#include "common.h"
#include "ShaderProgram.h"
#include "BufferObject.h"
#include "Texture.h"
#include "DRMBuffer.h"
#include "EGLManager.h"
#include "GBMManager.h"
#include <unordered_map>

typedef struct
{
    GLfloat b;
    GLfloat g;
    GLfloat r;
    GLfloat a;
} Color;

class SynaCompositor
{
    public:
        SynaCompositor();
        ~SynaCompositor();

        void setScreenDimensions(GLfloat width, GLfloat height);
        void registerCompositorLayoutItems(std::initializer_list<LayoutItem> layoutItems);
        void initializeCompositor();
        void setTextureImage(GLuint textureIndex, EGLImageKHR& image);
        void setOutputTextureImage(EGLImageKHR& image);
        void compositeFrame();
        void startRendering();
        void stopRendering();
        void setActive(GLuint textureIndex, bool isActive);
        void setBackgroundColor(GLuint color);

    private:
        ShaderProgram* shaderProgram;
        BufferObject* bufferObject;
        Texture* outputTexture;
        std::unordered_map<GLuint,Texture> textureMap;
        std::vector<LayoutItem> compositorLayoutItems;
        bool isRenderingActive;
        GLfloat screenWidth, screenHeight;
        GLuint frameBuffer;
        GLuint renderBuffer;
        Color backgroundColor;

        void createShader();
        void createTextures();
        void createBufferObjects();
        void initializeBufferObjects();
        void createFrameBuffer();
        void createOutputTexture();
        void setFrameBufferTexture();
        void deleteFrameBuffer();
};

#endif /* SYNA_COMPOSITOR_H */
