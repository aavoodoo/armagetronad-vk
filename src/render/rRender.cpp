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

#include "rRender.h"

//! Global renderer pointer - NOT THREAD SAFE
//! CRITICAL: Must only be accessed from the main rendering thread.
//! See header file for detailed threading and lifecycle documentation.
rRenderer *renderer;

void sr_RendererCleanup(){
    if ( renderer )
    {
        renderer->End(true);
    }
    delete renderer;
    renderer = 0;
}

//! Constructor - self-registers as the global renderer
//! @warning CRITICAL: Do not create multiple renderer instances or temporary
//! @warning renderer objects, as they will overwrite the global pointer.
//! @warning This is NOT thread-safe - must only be called from main thread.
rRenderer::rRenderer(){
    // CRITICAL: Self-register as global renderer
    // This is NOT thread-safe and assumes single-threaded usage
    renderer = this;

    // Clear the flag stack

    int i;

    stackpos = 0;

    for (i = STACK_DEPTH-1; i>=0; i--)
        flagstack[i] = 0;
}

//! Destructor - unregisters from global pointer if currently active
//! @warning CRITICAL: If this renderer is the active global renderer,
//! @warning the global pointer is set to null. Code checking the global
//! @warning pointer must be aware it can become null at any time.
rRenderer::~rRenderer(){
    // CRITICAL: Only unregister if we're the active renderer
    // Protects against temporary renderer objects clobbering the global state
    if (renderer == this)
        renderer = 0;
}


void rRenderer::SetFlag(flag f, bool c)
{
    ReallySetFlag(f, c);
}


void rRenderer::ChangeFlags(int before, int after) const{

}

#ifndef DEDICATED
void sr_InitRenderer(bool /*useGL3*/)
{
    // Clean up any existing renderer
    sr_RendererCleanup();

    // Create Vulkan renderer
    sr_vkRendererInit();
}
#endif


