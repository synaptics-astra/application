// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Synaptics Incorporated

#include "ShaderProgram.h"
#include <cstring>
#include <iomanip>
#include <numeric>

static void dumpInfoLog(char* infoLog)
{
    std::cout << "------------ Log Start ------------" << std::endl;
    std::cout << infoLog << std::endl;
    std::cout << "------------ Log End ------------" << std::endl;
}

ShaderProgram :: ShaderProgram(char* vertexShader, char* fragmentShader)
            : vertexShader(vertexShader), fragmentShader(fragmentShader)
{
    isShaderProgramReady = (createShaderProgram() < 0) ? false : true;
}

bool ShaderProgram :: getStatus()
{
    return isShaderProgramReady;
}

bool ShaderProgram :: initializeShader(std::vector<LayoutItem>& compositorLayoutItems)
{
    createVertexShader();
    createFragmentShader(compositorLayoutItems);
    isShaderProgramReady = (createShaderProgram() < 0) ? false : true;
    return isShaderProgramReady;
}

void ShaderProgram :: createVertexShader()
{
    std::stringstream vertexShaderCode;

    vertexShaderCode << "#version 300 es\n";
    vertexShaderCode << "in vec4 a_position;\n";
    vertexShaderCode << "in vec2 a_texCoord;\n";
    vertexShaderCode << "in vec3 a_alpha;\n";
    vertexShaderCode << "out vec2 v_texCoord;\n";
    vertexShaderCode << "out vec3 v_alpha;\n";
    vertexShaderCode << "void main() \n{\n";
    vertexShaderCode << "    gl_Position = a_position;\n";
    vertexShaderCode << "    v_texCoord = a_texCoord;\n";
    vertexShaderCode << "    v_alpha = a_alpha;\n";
    vertexShaderCode << "}\n";

    std::string vShader = vertexShaderCode.str();

    vertexShader = new char[vShader.size() + 1];
    std::strcpy(vertexShader, vShader.c_str());
    vertexShader[vShader.size()] = '\0';

#ifdef DEBUG_LOGS
    std::cout << std::endl << "**** VERTEX SHADER ****" << std::endl;
    std::cout << vertexShader << std::endl << std::endl;
#endif
}

void ShaderProgram :: createFragmentShader(std::vector<LayoutItem>& compositorLayoutItems)
{
    GLuint numberOfComponents = compositorLayoutItems.size();
    std::vector<std::string> fragmentShaderCode;

    fragmentShaderCode.push_back("#version 300 es\n");
    fragmentShaderCode.push_back("#extension GL_OES_EGL_image_external : require\n");
    fragmentShaderCode.push_back("precision highp float;\n");
    fragmentShaderCode.push_back("in vec2 v_texCoord;\n");

    for (LayoutItem& layoutItem : compositorLayoutItems)
    {
        fragmentShaderCode.push_back("uniform samplerExternalOES u_texture_" + std::to_string(layoutItem.index) + ";\n");
    }

    fragmentShaderCode.push_back("out vec4 FragColor;\n");
    fragmentShaderCode.push_back("void main() \n{\n");
    fragmentShaderCode.push_back("    ");

    for (LayoutItem& layoutItem : compositorLayoutItems)
    {
        std::ostringstream condition;
        condition << std::fixed << std::setprecision(2);

        condition << "if ((gl_FragCoord.x >= " << layoutItem.xPos
                << " && gl_FragCoord.x < " << (layoutItem.xPos + layoutItem.width) << ")\n"
                << "    && (gl_FragCoord.y >= " << layoutItem.yPos
                << " && gl_FragCoord.y < " << (layoutItem.yPos + layoutItem.height) << "))\n"
                << "    {\n"
                << "        FragColor = texture2D(u_texture_" << layoutItem.index << ", v_texCoord);\n"
                << "    }\n"
                << "    else ";

        fragmentShaderCode.push_back(condition.str());
    }

    fragmentShaderCode.push_back("\n"
                                "    {\n"
                                "        FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n"
                                "    }\n");
    fragmentShaderCode.push_back("}\n");

    std::string fShader = std::accumulate(
        fragmentShaderCode.begin(),
        fragmentShaderCode.end(),
        std::string{}
    );

    fragmentShader = new char[fShader.size() + 1];
    std::strcpy(fragmentShader, fShader.c_str());
    fragmentShader[fShader.size()] = '\0';

#ifdef DEBUG_LOGS
    std::cout << std::endl << "**** FRAGMENT SHADER ****" << std::endl;
    std::cout << fragmentShader << std::endl << std::endl;
#endif
}

GLint ShaderProgram :: compileShader(GLenum type, GLuint& shader)
{
    const char* shaderSource  = (type == GL_VERTEX_SHADER) ? vertexShader : fragmentShader;
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

    return 0;

EXIT:
    return -1;
}

void ShaderProgram :: activate()
{
    glUseProgram(programID);
}

ShaderProgram :: ~ShaderProgram()
{
    glDeleteProgram(programID);
}
