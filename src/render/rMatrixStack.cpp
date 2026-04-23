/*

*************************************************************************

ArmageTron -- Just another Tron Lightcycle Game in 3D.
Copyright (C) 2000  Manuel Moos (manuel@moosnet.de)

**************************************************************************

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

***************************************************************************

*/

#include "rMatrixStack.h"
#include "tError.h"
#include "tConsole.h"
#if defined(__APPLE__) || defined(__linux__)
#include <execinfo.h>
#endif

#ifndef DEDICATED

rMatrixStack::rMatrixStack()
    : current_(1.0f)  // Identity matrix
{
}

// Issue 9: Maximum stack depth to prevent overflow (matches typical GL implementation)
static const size_t MAX_MATRIX_STACK_DEPTH = 32;

void rMatrixStack::Push()
{
    // Issue 9: Stack overflow protection
    if (stack_.size() >= MAX_MATRIX_STACK_DEPTH)
    {
        tERR_WARN("rMatrixStack::Push: stack depth limit (" << MAX_MATRIX_STACK_DEPTH
                  << ") exceeded - push ignored");
        return;
    }
    stack_.push(current_);
}

void rMatrixStack::Pop()
{
    if (!stack_.empty())
    {
        current_ = stack_.top();
        stack_.pop();
    }
    else
    {
        tERR_WARN("rMatrixStack::Pop called on empty stack");
    }
}

void rMatrixStack::LoadIdentity()
{
    current_ = glm::mat4(1.0f);
}

void rMatrixStack::Mult(const float* m)
{
    // m is column-major (OpenGL convention)
    glm::mat4 matrix = glm::make_mat4(m);
    current_ = current_ * matrix;
}

void rMatrixStack::MultRowMajor(const float m[4][4])
{
    // glMultMatrixf reads float[4][4] as 16 consecutive column-major floats
    // Don't transpose - match legacy glMultMatrixf behavior exactly
    glm::mat4 matrix = glm::make_mat4(&m[0][0]);
    current_ = current_ * matrix;
}

void rMatrixStack::Load(const float* m)
{
    current_ = glm::make_mat4(m);
}

void rMatrixStack::Translate(float x, float y, float z)
{
    current_ = glm::translate(current_, glm::vec3(x, y, z));
}

void rMatrixStack::Scale(float s)
{
    current_ = glm::scale(current_, glm::vec3(s, s, s));
}

void rMatrixStack::Scale(float x, float y, float z)
{
    current_ = glm::scale(current_, glm::vec3(x, y, z));
}

void rMatrixStack::Rotate(float angleDegrees, float x, float y, float z)
{
    current_ = glm::rotate(current_, glm::radians(angleDegrees), glm::vec3(x, y, z));
}

void rMatrixStack::Perspective(float fovyDegrees, float aspect, float zNear, float zFar)
{
    // gluPerspective multiplies onto current matrix
    current_ = current_ * glm::perspective(glm::radians(fovyDegrees), aspect, zNear, zFar);
}

void rMatrixStack::Ortho(float left, float right, float bottom, float top, float zNear, float zFar)
{
    // glOrtho multiplies onto current matrix
    current_ = current_ * glm::ortho(left, right, bottom, top, zNear, zFar);
}

void rMatrixStack::LookAt(float eyeX, float eyeY, float eyeZ,
                          float centerX, float centerY, float centerZ,
                          float upX, float upY, float upZ)
{
    current_ = current_ * glm::lookAt(
        glm::vec3(eyeX, eyeY, eyeZ),
        glm::vec3(centerX, centerY, centerZ),
        glm::vec3(upX, upY, upZ)
    );
}

void rMatrixStack::Ortho2D(float left, float right, float bottom, float top)
{
    // gluOrtho2D multiplies onto current matrix
    current_ = current_ * glm::ortho(left, right, bottom, top, -1.0f, 1.0f);
}

const float* rMatrixStack::Get() const
{
    return glm::value_ptr(current_);
}

bool rMatrixStack::IsEmpty() const
{
    return stack_.empty();
}

size_t rMatrixStack::Depth() const
{
    return stack_.size();
}

#else // DEDICATED

// Stub implementation for dedicated server
rMatrixStack::rMatrixStack() {}
void rMatrixStack::Push() {}
void rMatrixStack::Pop() {}
void rMatrixStack::LoadIdentity() {}
void rMatrixStack::Mult(const float*) {}
void rMatrixStack::MultRowMajor(const float[4][4]) {}
void rMatrixStack::Load(const float*) {}
void rMatrixStack::Translate(float, float, float) {}
void rMatrixStack::Scale(float) {}
void rMatrixStack::Scale(float, float, float) {}
void rMatrixStack::Rotate(float, float, float, float) {}
void rMatrixStack::Perspective(float, float, float, float) {}
void rMatrixStack::Ortho(float, float, float, float, float, float) {}
void rMatrixStack::LookAt(float, float, float, float, float, float, float, float, float) {}
void rMatrixStack::Ortho2D(float, float, float, float) {}
const float* rMatrixStack::Get() const { static float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}; return identity; }
bool rMatrixStack::IsEmpty() const { return true; }
size_t rMatrixStack::Depth() const { return 0; }

#endif // DEDICATED

// rMatrixGuard implementation — uses global PushMatrix/PopMatrix
#include "rRender.h"

rMatrixGuard::rMatrixGuard()  { PushMatrix(); }
rMatrixGuard::~rMatrixGuard() { PopMatrix(); }
