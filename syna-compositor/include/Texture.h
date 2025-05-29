// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Synaptics Incorporated

#ifndef TEXTURE_H
#define TEXTURE_H

#include "common.h"
#include "ShaderProgram.h"

class Texture
{
    public:
        Texture(GLuint shaderProgramID, EGLImageKHR imageFD, GLenum textureSlot, GLuint textureIndex, const char* textureUniformName);
        Texture(GLenum textureSlot, GLuint textureIndex, const char* textureUniformName);
        ~Texture();
        void bind();
        void unBind();
        void activate();
        void updateTextureUniform();
        void activateForRendering();
        GLuint getTextureID();
        void setTextureImage(EGLImageKHR& textureImage);
        void setTextureUniformLocation(GLuint& textureUniformLocation);
        bool isVisible;
        bool isEnabled;

    private:
        GLuint textureID;
        GLuint textureType;
        GLenum textureSlot;
        GLuint textureLocation;
        GLuint textureIndex;
        const char* textureUniformName;
};

#endif /* TEXTURE_H */
