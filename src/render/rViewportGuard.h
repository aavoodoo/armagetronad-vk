#ifndef RVIEWPORTGUARD_H
#define RVIEWPORTGUARD_H

#include "rRender.h"

// RAII guard: saves current viewport on construction, restores it on destruction.
// Use before global UI rendering (menus, console) after a multi-viewport composite
// to prevent stale FBO viewport from leaking into fullscreen draws.
struct rViewportGuard
{
    int saved[4];
    rViewportGuard()  { RenderGetViewport(saved); }
    ~rViewportGuard() { RenderViewport(saved[0], saved[1], saved[2], saved[3]); }

    rViewportGuard(const rViewportGuard&) = delete;
    rViewportGuard& operator=(const rViewportGuard&) = delete;
};

#endif // RVIEWPORTGUARD_H
