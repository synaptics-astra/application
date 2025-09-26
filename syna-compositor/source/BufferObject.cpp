// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Synaptics Incorporated

#include "BufferObject.h"

BufferObject :: BufferObject()
{

}

void BufferObject :: initialize()
{
    float quadVertices[] =
    {
        -0.5f, -0.5f, 0.0f,  1.0f, 1.0f,
         0.5f, -0.5f, 0.0f,  0.0f, 1.0f,
         0.5f,  0.5f, 0.0f,  0.0f, 0.0f,
        -0.5f,  0.5f, 0.0f,  1.0f, 0.0f
    };

    unsigned int quadIndices[] =
    {
        0, 1, 2,
        2, 3, 0
    };

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    bind(GL_VERTEX_ARRAY_OBJECT);

    bind(GL_VERTEX_BUFFER_OBJECT);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    bind(GL_ELEMENT_BUFFER_OBJECT);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIndices), quadIndices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    unBind(GL_ELEMENT_BUFFER_OBJECT);
    unBind(GL_VERTEX_BUFFER_OBJECT);
    unBind(GL_VERTEX_ARRAY_OBJECT);
}

void BufferObject :: bind(BufferType bufferType)
{
    if (bufferType == GL_VERTEX_ARRAY_OBJECT)
        glBindVertexArray(VAO);
    else if (bufferType == GL_VERTEX_BUFFER_OBJECT)
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
    else if (bufferType == GL_ELEMENT_BUFFER_OBJECT)
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
}

void BufferObject :: unBind(BufferType bufferType)
{
    if (bufferType == GL_VERTEX_ARRAY_OBJECT)
        glBindVertexArray(0);
    else if (bufferType == GL_VERTEX_BUFFER_OBJECT)
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    else if (bufferType == GL_ELEMENT_BUFFER_OBJECT)
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

BufferObject :: ~BufferObject()
{
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
}
