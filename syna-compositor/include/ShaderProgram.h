// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Synaptics Incorporated

#ifndef SHADER_PROGRAM_H
#define SHADER_PROGRAM_H

#include "common.h"

class ShaderProgram
{
    public:
        ShaderProgram();
        ~ShaderProgram();
        void activate();
        void deActivate();
        bool initializeShader();
        GLuint getProgramID();

    private:
        GLuint programID;
        bool isShaderProgramReady;
        GLint compileShader(GLenum type, GLuint& shader);
        GLint createShaderProgram();
};

#endif /* SHADER_PROGRAM_H */
