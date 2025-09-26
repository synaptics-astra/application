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

class BufferObject
{
    public:
        BufferObject();
        ~BufferObject();
        void bind(BufferType bufferType);
        void unBind(BufferType bufferType);
        void initialize();

    private:
        GLuint VAO, VBO, EBO;
};

#endif /* BUFFER_OBJECT_H */
