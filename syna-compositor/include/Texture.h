// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Synaptics Incorporated

#ifndef TEXTURE_H
#define TEXTURE_H

#include "common.h"
#include "ShaderProgram.h"
#include <array>

class Texture
{
    public:
        Texture(GLenum textureSlot, LayoutItem& layoutItem, GLfloat& screenWidth, GLfloat& screenHeight);
        ~Texture();
        void activate();
        void deActivate();
        GLuint getTextureID();
        void setTextureUniforms();
        void updateUniformLocations(GLuint& textureLocation, GLuint& mvpLocation);
        void setTextureImage(EGLImageKHR& textureImage);
        void setActive(bool isActive);
        bool isActive();

    private:
        GLuint textureID;
        GLuint textureType;
        GLuint textureIndex;
        GLenum textureSlot;
        GLuint mvpLocation;
        GLuint textureLocation;
        bool active;
        std::array<float, 16> mvpMatrix;

        void matrixMultiply(std::array<GLfloat, 16>& out,
            const std::array<GLfloat, 16>& a,
            const std::array<GLfloat, 16>& b);
        void generateMVPMatrix(LayoutItem& layoutItem, GLfloat& screenWidth, GLfloat& screenHeight);
};

#endif /* TEXTURE_H */
