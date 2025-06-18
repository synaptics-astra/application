// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Synaptics Incorporated

#include "BufferObject.h"

BufferObject :: BufferObject()
{

}

void BufferObject :: generateIndices()
{
    size_t baseIndex = vertices.size() - 4;

    indices.push_back(baseIndex + 0);
    indices.push_back(baseIndex + 1);
    indices.push_back(baseIndex + 2);

    indices.push_back(baseIndex + 0);
    indices.push_back(baseIndex + 2);
    indices.push_back(baseIndex + 3);
}

void BufferObject :: generateVertices(std::vector<LayoutItem>& compositorLayoutItems, GLfloat screenWidth, GLfloat screenHeight)
{
    for (LayoutItem& layoutItem : compositorLayoutItems)
    {
        /* Normalize coordinates from local spcae to screen space*/

        GLfloat x1 = ((layoutItem.xPos / screenWidth) * 2.0f) - 1.0f;
        GLfloat y1 = ((layoutItem.yPos / screenHeight) * 2.0f) - 1.0f;
        GLfloat x2  = (((layoutItem.xPos + layoutItem.width) / screenWidth) * 2.0f) - 1.0f;
        GLfloat y2  = (((layoutItem.yPos + layoutItem.height) / screenHeight) * 2.0f) - 1.0f;
        GLfloat z = layoutItem.zPos;

        vertices.push_back({x1, y1, z, 0.0f, 0.0f, layoutItem.alpha});
        vertices.push_back({x2, y1, z, 1.0f, 0.0f, layoutItem.alpha});
        vertices.push_back({x2, y2, z, 1.0f, 1.0f, layoutItem.alpha});
        vertices.push_back({x1, y2, z, 0.0f, 1.0f, layoutItem.alpha});

        generateIndices();
    }
}

void BufferObject :: createBufferObjects()
{
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(a_position, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(a_position);

    glVertexAttribPointer(a_texCoord, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray(a_texCoord);

    glVertexAttribPointer(a_alpha, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(5 * sizeof(GLfloat)));
    glEnableVertexAttribArray(a_alpha);

    glBindVertexArray(0);
}

#ifdef DEBUG_LOGS
void BufferObject :: printVertices()
{
    std::cout << "Vertices[] = {" << std::endl;
    for (const auto& v : vertices)
    {
        std::cout << "\t" << v.x << "\t" << v.y << "\t" << v.z << "\t\t" << v.tx << "\t" << v.ty << "\t\t" << v.alpha;
        std::cout << std::endl;
    }
    std::cout << "}" << std::endl;
    std::cout << std::endl << std::endl;

    std::cout << "Indices[] = {" << std::endl;
    int count = 1;
    for (const auto& v : indices)
    {
        std::cout << "\t" << v;
        if (count % 6 == 0)
            std::cout << std::endl << std::endl;
        else if (count % 3 == 0)
            std::cout << std::endl;
        count++;
    }
    std::cout << "}" << std::endl;
}
#endif

void BufferObject :: initialize(std::vector<LayoutItem>& compositorLayoutItems, GLfloat screenWidth, GLfloat screenHeight)
{
    generateVertices(compositorLayoutItems, screenWidth, screenHeight);
    createBufferObjects();

#ifdef DEBUG_LOGS
    printVertices();
#endif
}

void BufferObject :: setVertexShaderVariables(GLint& a_position, GLint& a_texCoord, GLint& a_alpha)
{
    this->a_position = a_position;
    this->a_texCoord = a_texCoord;
    this->a_alpha = a_alpha;
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
    vertices.clear();
    indices.clear();
}
