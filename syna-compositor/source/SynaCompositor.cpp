// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Synaptics Incorporated

#include "SynaCompositor.h"

void printEGLInfo()
{
    const char* version = (const char*)glGetString(GL_VERSION);
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    const char* vendor = (const char*)glGetString(GL_VENDOR);
    const char* shadingLang = (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);

    std::cout << "GL_VERSION: " << version << std::endl;
    std::cout << "GL_RENDERER: " << renderer << std::endl;
    std::cout << "GL_VENDOR: " << vendor << std::endl;
    std::cout << "GLSL_VERSION: " << shadingLang << std::endl;
}

void checkAllErrors(int lineNum, const char* funcName) {
    return;
    EGLint eglErr = eglGetError();
    GLenum glErr = glGetError();

    if (eglErr != EGL_SUCCESS)
        std::cerr << "[EGL][" << funcName << " :: " << lineNum << "] Error: 0x" << std::hex << eglErr << std::endl;

    if (glErr != GL_NO_ERROR)
        std::cerr << "[GL][" << funcName << " :: " << lineNum << "] Error: 0x" << std::hex << glErr << std::endl;
}

bool ensureContext()
{
    if (eglGetCurrentContext() == EGL_NO_CONTEXT) {
        return false;
    }
    return true;
}

SynaCompositor :: SynaCompositor()
{
    textureMap.clear();
}

void SynaCompositor :: setScreenDimensions(GLfloat width, GLfloat height)
{
    screenWidth = width;
    screenHeight = height;
}

void SynaCompositor :: registerCompositorLayoutItems(std::initializer_list<LayoutItem> layoutItems)
{
    for (LayoutItem layoutItem : layoutItems)
    {
        compositorLayoutItems.push_back(layoutItem);
    }
}

void SynaCompositor :: createShader()
{
    shaderProgram = new ShaderProgram();

    if (!(shaderProgram->initializeShader(compositorLayoutItems)))
        std::cout << "Shader Program Initialization Failed." << std::endl;
}

void SynaCompositor :: createBufferObjects()
{
    bufferObject = new BufferObject();
}

void SynaCompositor :: initializeBufferObjects()
{
    GLint a_position, a_texCoord, a_alpha;
    GLuint shaderProgramID = shaderProgram->getProgramID();
    a_position = glGetAttribLocation(shaderProgramID, "a_position");
    a_texCoord = glGetAttribLocation(shaderProgramID, "a_texCoord");
    a_alpha = glGetAttribLocation(shaderProgramID, "a_alpha");
    bufferObject->setVertexShaderVariables(a_position, a_texCoord, a_alpha);
    bufferObject->initialize(compositorLayoutItems, screenWidth, screenHeight);
}

void SynaCompositor :: createTextures()
{
    std::map<GLint, Texture> textureMapLocal;
    textureMapLocal.clear();
    std::string textureUniformBaseName = "u_texture_";
    GLuint i = 0;
    GLuint shaderProgramID = shaderProgram->getProgramID();
    for (LayoutItem& layoutItem : compositorLayoutItems)
    {
        const char* texUniName = (textureUniformBaseName + std::to_string(layoutItem.index)).c_str();
        Texture texture((GL_TEXTURE0 + i), layoutItem.index, texUniName);
        GLuint texUniLocation = glGetUniformLocation(shaderProgramID, texUniName);
        texture.setTextureUniformLocation(texUniLocation);
        textureMapLocal.insert(std::make_pair(layoutItem.index, std::move(texture)));
        i++;
    }
    textureMap = textureMapLocal;
}

void SynaCompositor :: createOutputTexture()
{
    int textureIndex = textureMap.size();
    outputTexture = new Texture((GL_TEXTURE0 + textureIndex), textureIndex, "u_outputTexture");
}

void SynaCompositor :: createFrameBuffer()
{
    glGenFramebuffers(1, &frameBuffer);
}

void SynaCompositor :: initializeCompositor()
{
    createShader();

    createBufferObjects();

    initializeBufferObjects();

    createTextures();

    createOutputTexture();

    createFrameBuffer();
}

void SynaCompositor :: setFrameBufferTexture()
{
    glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_EXTERNAL_OES, outputTexture->getTextureID(), 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

    if (status != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "Framebuffer could not be completed Status Code - " << status << std::endl;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SynaCompositor :: setTextureImage(GLuint textureIndex, EGLImageKHR& image)
{
    auto texture = textureMap.find(textureIndex);
    texture->second.setTextureImage(image);
}

void SynaCompositor :: setOutputTextureImage(EGLImageKHR& image)
{
    outputTexture->setTextureImage(image);
    setFrameBufferTexture();
}

void SynaCompositor :: startRendering()
{
    glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    shaderProgram->activate();

    int index = 0;
    for (auto& texturePair : textureMap)
    {
        texturePair.second.activateForRendering();
        bufferObject->bind(GL_VERTEX_ARRAY_OBJECT);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, (void*)((index * 6) * sizeof(GLuint)));
        bufferObject->unBind(GL_VERTEX_ARRAY_OBJECT);
        index++;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SynaCompositor :: stopRendering()
{

}

SynaCompositor :: ~SynaCompositor()
{

}

