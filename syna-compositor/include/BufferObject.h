// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Synaptics Incorporated

#ifndef BUFFER_OBJECT_H
#define BUFFER_OBJECT_H

#include "common.h"

enum BufferType
{
    GL_VERTEX_ARRAY_OBJECT,
    GL_VERTEX_BUFFER_OBJECT,
    GL_ELEMENT_BUFFER_OBJECT,
    GL_NONE_OBJECT
};

struct Vertex
{
    GLfloat x, y, z;
    GLfloat tx, ty;
    GLfloat alpha;
};

class BufferObject
{
    public:
        BufferObject();
        ~BufferObject();
        void bind(BufferType bufferType);
        void unBind(BufferType bufferType);
        void initialize(std::vector<LayoutItem>& compositorLayoutItems, GLfloat screenWidth, GLfloat screenHeight);
        void setVertexShaderVariables(GLint& a_position, GLint& a_texCoord, GLint& a_alpha);

    private:
        std::vector<Vertex> vertices;
        std::vector<GLuint> indices;
        GLfloat screenWidth, screenHeight;
        GLuint VAO, VBO, EBO;
        GLint a_position, a_texCoord, a_alpha;

        void generateVertices(std::vector<LayoutItem>& compositorLayoutItems, GLfloat screenWidth, GLfloat screenHeight);
        void generateIndices();
        void createBufferObjects();

#ifdef DEBUG_LOGS
        void printVertices();
#endif
};

#endif /* BUFFER_OBJECT_H */
