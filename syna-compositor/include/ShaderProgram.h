// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Synaptics Incorporated

#ifndef SHADER_PROGRAM_H
#define SHADER_PROGRAM_H

#include "common.h"

class ShaderProgram
{
    public:
        ShaderProgram(char* vertexShader, char* fragmentShader);
        ShaderProgram() {};
        ~ShaderProgram();
        void activate();
        bool getStatus();
        bool initializeShader(std::vector<LayoutItem>& compositorLayoutItems);
        GLuint getProgramID() {
            return this->programID;
        }

    private:
        GLuint programID;
        bool isShaderProgramReady;
        char* vertexShader;
        char* fragmentShader;
        void createVertexShader();
        void createFragmentShader(std::vector<LayoutItem>& compositorLayoutItems);
        GLint compileShader(GLenum type, GLuint& shader);
        GLint createShaderProgram();
};

#endif /* SHADER_PROGRAM_H */
