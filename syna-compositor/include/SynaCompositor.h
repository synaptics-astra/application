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
        void startRendering();
        void stopRendering();

    private:
        ShaderProgram* shaderProgram;
        BufferObject* bufferObject;
        Texture* outputTexture;
        std::map<GLint,Texture> textureMap;
        std::vector<LayoutItem> compositorLayoutItems;
        std::vector<Vertex> vertices;
        std::vector<GLuint> indices;
        bool isRenderingActive;
        GLfloat screenWidth, screenHeight;
        GLuint frameBuffer;

        void createShader();
        void createTextures();
        void createBufferObjects();
        void initializeBufferObjects();
        void createFrameBuffer();
        void createOutputTexture();
        void setFrameBufferTexture();
};

#endif /* SYNA_COMPOSITOR_H */
