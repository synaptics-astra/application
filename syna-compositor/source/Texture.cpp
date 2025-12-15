// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Synaptics Incorporated

#include "Texture.h"
#include <iomanip>

PFNGLEGLIMAGETARGETTEXTURE2DOESPROC pfnglEGLImageTargetTexture2DOES_temp;

#ifdef DEBUG_LOGS
void printMatrix(const std::array<GLfloat, 16>& mvpMatrix, const char* message) {
    std::cout << "Matrix (" << message << ")" << std::endl;
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            std::cout << std::setw(10) << mvpMatrix[col * 4 + row] << " ";
        }
        std::cout << "\n";
    }
}
#endif

Texture :: Texture(GLenum textureSlot, LayoutItem& layoutItem, GLfloat& screenWidth, GLfloat& screenHeight, GLenum textureType)
{
    this->textureSlot = textureSlot;
    this->textureType = textureType;
    this->textureIndex = layoutItem.index;
    this->active = true;
    generateMVPMatrix(layoutItem, screenWidth, screenHeight);
}

void Texture::matrixMultiply(std::array<GLfloat, 16>& out,
    const std::array<GLfloat, 16>& a,
    const std::array<GLfloat, 16>& b)
{
    std::array<GLfloat, 16> result = {0.0f};
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            result[col * 4 + row] = 0.0f;
            for (int k = 0; k < 4; ++k)
            {
                result[col * 4 + row] += a[k * 4 + row] * b[col * 4 + k];
            }
        }
    }
    out = result;
}

void Texture::generateMVPMatrix(LayoutItem& layoutItem, GLfloat& screenWidth, GLfloat& screenHeight)
{
    std::array<GLfloat, 16> scaleMatrix, translationMatrix, modelMatrix, projectionMatrix;

    scaleMatrix.fill(0.0f);
    scaleMatrix[0] = layoutItem.width;
    scaleMatrix[5] = layoutItem.height;
    scaleMatrix[10] = 1.0f;
    scaleMatrix[15] = 1.0f;

    float zNear = 0.1f;
    float zFar = 100.0f;
    float cameraZ = zNear + (zFar - zNear) * (1.0f - (layoutItem.zPos / 9999.0f));

    translationMatrix.fill(0.0f);
    translationMatrix[0] = 1.0f;
    translationMatrix[5] = 1.0f;
    translationMatrix[10] = 1.0f;
    translationMatrix[15] = 1.0f;
    translationMatrix[12] = layoutItem.xPos + layoutItem.width / 2.0f;
    translationMatrix[13] = layoutItem.yPos + layoutItem.height / 2.0f;
    translationMatrix[14] = -(cameraZ);

    modelMatrix.fill(0.0f);
    matrixMultiply(modelMatrix, translationMatrix, scaleMatrix);

    projectionMatrix.fill(0.0f);
    float left = 0.0f, right = screenWidth;
    float bottom = 0.0f, top = screenHeight;

    projectionMatrix[0] = 2.0f / (right - left);
    projectionMatrix[5] = 2.0f / (top - bottom);
    projectionMatrix[10] = -2.0f / (zFar - zNear);
    projectionMatrix[12] = -(right + left) / (right - left);
    projectionMatrix[13] = -(top + bottom) / (top - bottom);
    projectionMatrix[14] = -(zFar + zNear) / (zFar - zNear);
    projectionMatrix[15] = 1.0f;

    mvpMatrix.fill(0.0f);
    matrixMultiply(mvpMatrix, projectionMatrix, modelMatrix);

#ifdef DEBUG_LOGS

    std::cout   << "Layout:"
                << " index = " << layoutItem.index
                << ", x=" << layoutItem.xPos
                << ", y=" << layoutItem.yPos
                << ", z=" << layoutItem.zPos
                << ", w=" << layoutItem.width
                << ", h=" << layoutItem.height << "\n";

    printMatrix(scaleMatrix, "Scale Matrix");
    printMatrix(translationMatrix, "Translation Matrix");
    printMatrix(modelMatrix, "Model Matrix");
    printMatrix(projectionMatrix, "Projection Matrix");
    printMatrix(mvpMatrix, "MVP Matrix");

#endif
}

void Texture :: setTextureImage(EGLImageKHR& textureImage)
{
    if (textureID != 0) {
       glDeleteTextures(1, &textureID);
    }
    glGenTextures(1, &textureID);
    activate();
    glTexParameteri(textureType, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(textureType, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(textureType, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(textureType, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    pfnglEGLImageTargetTexture2DOES_temp(textureType, textureImage);
    deActivate();
}

GLuint Texture :: getTextureID()
{
    return textureID;
}

void Texture :: activate()
{
    glActiveTexture(textureSlot);
    glBindTexture(textureType, textureID);
}

void Texture :: deActivate()
{
    glBindTexture(textureType, 0);
}

void Texture :: setActive(bool isActive)
{
    this->active = isActive;
}

bool Texture :: isActive()
{
    return active;
}

void Texture :: updateUniformLocations(GLuint& textureLocation, GLuint& mvpLocation)
{
    this->textureLocation = textureLocation;
    this->mvpLocation = mvpLocation;
}

void Texture :: setTextureUniforms()
{
    glUniformMatrix4fv(mvpLocation, 1, GL_FALSE, mvpMatrix.data());
    glUniform1i(textureLocation, textureIndex);
}

Texture :: ~Texture()
{
    glDeleteTextures(1, &textureID);
}