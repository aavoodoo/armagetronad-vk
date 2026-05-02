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

#ifndef RMATRIXSTACK_H
#define RMATRIXSTACK_H

#include "defs.h"
#include <stack>

#ifndef DEDICATED
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#endif

//! Software matrix stack for GL3 renderer
//! Replaces GL's built-in matrix stack (glPushMatrix, glPopMatrix, etc.)
class rMatrixStack
{
public:
    rMatrixStack();

    //! Push current matrix onto stack
    void Push();

    //! Pop matrix from stack
    void Pop();

    //! Load identity matrix
    void LoadIdentity();

    //! Multiply current matrix by given 4x4 matrix (column-major)
    void Mult(const float* m);

    //! Multiply current matrix by given 4x4 matrix (row-major, as used by game code)
    void MultRowMajor(const float m[4][4]);

    //! Load a 4x4 matrix directly (column-major)
    void Load(const float* m);

    //! Apply translation
    void Translate(float x, float y, float z);

    //! Apply uniform scale
    void Scale(float s);

    //! Apply non-uniform scale
    void Scale(float x, float y, float z);

    //! Apply rotation (angle in degrees)
    void Rotate(float angleDegrees, float x, float y, float z);

    //! Set up perspective projection
    void Perspective(float fovyDegrees, float aspect, float zNear, float zFar);

    //! Set up orthographic projection
    void Ortho(float left, float right, float bottom, float top, float zNear, float zFar);

    //! Set up view matrix (like gluLookAt)
    void LookAt(float eyeX, float eyeY, float eyeZ,
                float centerX, float centerY, float centerZ,
                float upX, float upY, float upZ);

    //! Set up 2D orthographic projection for UI
    void Ortho2D(float left, float right, float bottom, float top);

    //! Get current matrix as column-major float[16]
    [[nodiscard]] const float* Get() const;

    //! Get current matrix as glm::mat4 reference
#ifndef DEDICATED
    [[nodiscard]] const glm::mat4& GetMat4() const { return current_; }
#endif

    //! Check if stack is empty (only has initial matrix)
    [[nodiscard]] bool IsEmpty() const;

    //! Get stack depth
    [[nodiscard]] size_t Depth() const;

private:
#ifndef DEDICATED
    glm::mat4 current_;
    std::stack<glm::mat4> stack_;
#endif
};

//! RAII guard for matrix push/pop. Guarantees PopMatrix() on scope exit,
//! preventing stack imbalance from early returns, exceptions, or code path bugs.
//! Operates on whichever matrix stack is current (Model/Proj/Tex) at construction.
//!
//! Usage:
//!   ModelMatrix();
//!   rMatrixGuard guard;  // pushes on model stack, pops on destruction
//!   TranslateMatrix(x, y, z);
//!   // ... guard destructor pops automatically
class rMatrixGuard
{
public:
    rMatrixGuard();
    ~rMatrixGuard();

    // Non-copyable, non-movable
    rMatrixGuard(const rMatrixGuard&) = delete;
    rMatrixGuard& operator=(const rMatrixGuard&) = delete;
};

#endif // RMATRIXSTACK_H
