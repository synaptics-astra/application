// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Synaptics Incorporated

#include "ShaderProgram.h"

static void dumpInfoLog(char* infoLog)
{
    std::cout << "------------ Log Start ------------" << std::endl;
    std::cout << infoLog << std::endl;
    std::cout << "------------ Log End ------------" << std::endl;
}

ShaderProgram :: ShaderProgram()
{

}

bool ShaderProgram :: initializeShader()
{
    isShaderProgramReady = (createShaderProgram() < 0) ? false : true;
    return isShaderProgramReady;
}

GLint ShaderProgram :: compileShader(GLenum type, GLuint& shader)
{
    const char* vertexShaderSrc = R"(#version 300 es
        precision mediump float;
        layout(location = 0) in vec3 aPos;
        layout(location = 1) in vec2 aTex;
        uniform mat4 uMVP;
        out vec2 vTex;
        void main() {
            gl_Position = uMVP * vec4(aPos, 1.0);
            vTex = vec2(1.0 - aTex.x, 1.0 - aTex.y);
        }
    )";

#ifdef USE_MALI_GPU
    const char* fragmentShaderSrc = R"(#version 300 es
        #extension GL_OES_EGL_image_external_essl3 : require
        precision mediump float;

        uniform samplerExternalOES uTexture;
        in vec2 vTex;
        out vec4 fragColor;

        void main() {
            fragColor = texture(uTexture, vTex);
        }
    )";
#else
    const char* fragmentShaderSrc = R"(#version 300 es
        #extension GL_OES_EGL_image_external : require
        precision mediump float;

        uniform samplerExternalOES uTexture;
        in vec2 vTex;
        out vec4 fragColor;

        void main() {
            fragColor = texture2D(uTexture, vTex);
        }
    )";
#endif

    const char* shaderSource  = (type == GL_VERTEX_SHADER) ? vertexShaderSrc : fragmentShaderSrc;
    shader = glCreateShader(type);
    glShaderSource(shader, 1, &shaderSource, NULL);
    glCompileShader(shader);
    GLint isCompiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
    if(!isCompiled)
    {
        GLint infoLogLengthInBytes;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLengthInBytes);
        char* infoLog = new char[infoLogLengthInBytes];
        glGetShaderInfoLog(shader, infoLogLengthInBytes, NULL, infoLog);
        std::cout << "Shader Compilation Failed for " << ((type == GL_VERTEX_SHADER) ? "Vertex Shader" : "Fragment Shader") << std::endl;
        std::cout << infoLog << std::endl;
        dumpInfoLog(infoLog);
        delete[] infoLog;
        return -1;
    }
    return 0;
}

GLint ShaderProgram :: createShaderProgram()
{
    GLuint vShader, fShader;

    if (compileShader(GL_VERTEX_SHADER, vShader) < 0)
        goto EXIT;

    if (compileShader(GL_FRAGMENT_SHADER, fShader) < 0)
        goto EXIT;

    programID = glCreateProgram();

    glAttachShader(programID, vShader);
    glAttachShader(programID, fShader);

    glLinkProgram(programID);

    GLint isLinked;
    glGetProgramiv(programID, GL_LINK_STATUS, &isLinked);
    if(!isLinked)
    {
        GLint infoLogLengthInBytes;
        glGetProgramiv(programID, GL_INFO_LOG_LENGTH, &infoLogLengthInBytes);
        char* infoLog = new char[infoLogLengthInBytes];
        glGetProgramInfoLog(programID, infoLogLengthInBytes, NULL, infoLog);
        std::cout << "Shader Program Linking Failed." << std::endl;
        dumpInfoLog(infoLog);
        delete[] infoLog;
        return -1;
    }

    GLint isValidated;
    glValidateProgram(programID);
    glGetProgramiv(programID, GL_VALIDATE_STATUS, &isValidated);
    if(!isValidated)
    {
        GLint infoLogLengthInBytes;
        glGetProgramiv(programID, GL_INFO_LOG_LENGTH, &infoLogLengthInBytes);
        char* infoLog = new char[infoLogLengthInBytes];
        glGetProgramInfoLog(programID, infoLogLengthInBytes, NULL, infoLog);
        std::cout << "Shader Program Validation Failed." << std::endl;
        dumpInfoLog(infoLog);
        delete[] infoLog;
        return -1;
    }

#ifdef DEBUG_LOGS
    std::cout << "----- Vertex Shader -----" << std::end;
    std::cout << vertexShaderSrc << std::endl;
    std::cout << "----- Fragment Shader -----" << std::end;
    std::cout << fragmentShaderSrc << std::endl;
#endif

    return 0;

EXIT:
    return -1;
}

GLuint ShaderProgram :: getProgramID()
{
    return programID;
}

void ShaderProgram :: activate()
{
    glUseProgram(programID);
}

void ShaderProgram :: deActivate()
{
    glUseProgram(0);
}

ShaderProgram :: ~ShaderProgram()
{
    glDeleteProgram(programID);
}
