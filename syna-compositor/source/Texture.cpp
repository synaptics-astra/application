// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Synaptics Incorporated

#include "Texture.h"

PFNGLEGLIMAGETARGETTEXTURE2DOESPROC pfnglEGLImageTargetTexture2DOES_temp;

Texture :: Texture(GLuint shaderProgramID, EGLImageKHR imageFD, GLenum textureSlot, GLuint textureIndex, const char* textureUniformName)
{
    this->textureSlot = textureSlot;
    this->textureType = GL_TEXTURE_EXTERNAL_OES;
    this->textureIndex = textureIndex;
    this->textureUniformName = textureUniformName;
    glGenTextures(1, &textureID);
    activate();
    bind();
    glTexParameteri(textureType, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(textureType, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(textureType, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(textureType, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    pfnglEGLImageTargetTexture2DOES_temp(textureType, imageFD);
    if (textureUniformName != "")
    {
        textureLocation = glGetUniformLocation(shaderProgramID, textureUniformName);
        glUniform1i(textureLocation, textureIndex);
    }
    unBind();
}

Texture :: Texture(GLenum textureSlot, GLuint textureIndex, const char* textureUniformName)
{
    this->textureSlot = textureSlot;
    this->textureType = GL_TEXTURE_EXTERNAL_OES;
    this->textureIndex = textureIndex;
    this->textureUniformName = textureUniformName;
    glGenTextures(1, &textureID);
    activate();
    bind();
    glTexParameteri(textureType, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(textureType, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(textureType, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(textureType, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    unBind();
}

void Texture :: activate()
{
    glActiveTexture(textureSlot);
}

void Texture :: bind()
{
    glBindTexture(textureType, textureID);
}

void Texture :: unBind()
{
    glBindTexture(textureType, 0);
}

void Texture :: setTextureUniformLocation(GLuint& textureUniformLocation)
{
    textureLocation = textureUniformLocation;
}

void Texture :: updateTextureUniform()
{
    glUniform1i(textureLocation, textureIndex);
}

void Texture :: setTextureImage(EGLImageKHR& textureImage)
{
    activate();
    bind();
    pfnglEGLImageTargetTexture2DOES_temp(textureType, textureImage);
    unBind();
}

GLuint Texture :: getTextureID()
{
    return textureID;
}

void Texture :: activateForRendering()
{
    activate();
    bind();
    updateTextureUniform();
}

Texture :: ~Texture()
{
    glDeleteTextures(1, &textureID);
}
