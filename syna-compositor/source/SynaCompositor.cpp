// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Synaptics Incorporated

#include "SynaCompositor.h"
#include <algorithm>

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

    if (!(shaderProgram->initializeShader()))
        std::cout << "Shader Program Initialization Failed." << std::endl;
}

void SynaCompositor :: createBufferObjects()
{
    bufferObject = new BufferObject();
}

void SynaCompositor :: initializeBufferObjects()
{
    shaderProgram->activate();
    bufferObject->initialize();
    shaderProgram->deActivate();
}

void SynaCompositor :: createTextures()
{
    std::unordered_map<GLuint,Texture> textureMapLocal;
    textureMapLocal.clear();
    textureMap.clear();
    shaderProgram->activate();
    GLuint mvpLocation = glGetUniformLocation(shaderProgram->getProgramID(), "uMVP");
    GLuint textureLocation = glGetUniformLocation(shaderProgram->getProgramID(), "uTexture");
    for (LayoutItem& layoutItem : compositorLayoutItems)
    {
        Texture texture((GL_TEXTURE0 + layoutItem.index), layoutItem, screenWidth, screenHeight, GL_TEXTURE_EXTERNAL_OES);
        texture.updateUniformLocations(textureLocation, mvpLocation);
        textureMapLocal.insert(std::make_pair(layoutItem.index, std::move(texture)));
    }
    shaderProgram->deActivate();

    textureMap = textureMapLocal;

    for(auto& texturePair : textureMapLocal)
    {
        textureMap.insert(texturePair);
    }
}

void SynaCompositor :: createOutputTexture()
{
    int textureIndex = textureMap.size();
    LayoutItem layoutItem(textureIndex, 0, 0, 0, screenWidth, screenHeight);
    GLenum textureType = GL_TEXTURE_EXTERNAL_OES;
#ifdef USE_MALI_GPU
    textureType = GL_TEXTURE_2D;
#endif
    outputTexture = new Texture((GL_TEXTURE0 + textureIndex), layoutItem, screenWidth, screenHeight, textureType);
}

void SynaCompositor :: createFrameBuffer()
{
    glGenFramebuffers(1, &frameBuffer);
    glGenRenderbuffers(1, &renderBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, renderBuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, screenWidth, screenHeight);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
}

void SynaCompositor :: initializeCompositor()
{

    std::sort(compositorLayoutItems.begin(), compositorLayoutItems.end(), [](const LayoutItem &layout1, const LayoutItem &layout2) {
        return layout1.zPos < layout2.zPos;
    });

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

#ifdef USE_MALI_GPU
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outputTexture->getTextureID(), 0);
#else
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_EXTERNAL_OES, outputTexture->getTextureID(), 0);
#endif

    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, renderBuffer);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

    if (status != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "Framebuffer could not be completed Status Code - " << status << std::endl;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SynaCompositor :: setTextureImage(GLuint textureIndex, EGLImageKHR& image)
{
    shaderProgram->activate();
    auto texture = textureMap.find(textureIndex);
    texture->second.setTextureImage(image);
    shaderProgram->deActivate();
}

void SynaCompositor :: setOutputTextureImage(EGLImageKHR& image)
{
    shaderProgram->activate();
    outputTexture->setTextureImage(image);
    setFrameBufferTexture();
    shaderProgram->deActivate();
}

void SynaCompositor :: compositeFrame()
{
    shaderProgram->activate();
    outputTexture->activate();

    glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);

    glClearColor(backgroundColor.r, backgroundColor.g, backgroundColor.b, backgroundColor.a);
    glClearDepthf(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    for (auto& texturePair : textureMap)
    {
        if (texturePair.second.isActive())
        {
            texturePair.second.activate();
            texturePair.second.setTextureUniforms();
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
            texturePair.second.deActivate();
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    outputTexture->deActivate();
    shaderProgram->deActivate();
}

void SynaCompositor :: setActive(GLuint textureIndex, bool isActive)
{
    shaderProgram->activate();
    auto texture = textureMap.find(textureIndex);
    texture->second.setActive(isActive);
    shaderProgram->deActivate();
}

void SynaCompositor :: setBackgroundColor(GLuint color)
{
    this->backgroundColor.b = ((color >> 24) & 0xFF) / 255.0f;
    this->backgroundColor.g = ((color >> 16) & 0xFF) / 255.0f;
    this->backgroundColor.r = ((color >> 8)  & 0xFF) / 255.0f;
    this->backgroundColor.a = ((color >> 0)  & 0xFF) / 255.0f;
}

void SynaCompositor :: startRendering()
{
    glViewport(0, 0, screenWidth, screenHeight);
    outputTexture->activate();

    bufferObject->bind(GL_VERTEX_ARRAY_OBJECT);
    bufferObject->bind(GL_VERTEX_BUFFER_OBJECT);
    bufferObject->bind(GL_ELEMENT_BUFFER_OBJECT);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);
}

void SynaCompositor :: stopRendering()
{
    outputTexture->deActivate();

    bufferObject->unBind(GL_VERTEX_ARRAY_OBJECT);

    glDisable(GL_DEPTH_TEST);
}

void SynaCompositor :: deleteFrameBuffer()
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glDeleteFramebuffers(1, &frameBuffer);
    glDeleteRenderbuffers(1, &renderBuffer);
}

SynaCompositor :: ~SynaCompositor()
{
    deleteFrameBuffer();
    textureMap.clear();
    compositorLayoutItems.clear();
    delete outputTexture;
    delete bufferObject;
    delete shaderProgram;
}