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

#include "rRendererState.h"

#ifndef DEDICATED

static rRenderContext s_currentRenderContext = rRenderContext::Unknown;

extern void sr_vkSetRenderContext(int);
extern void sr_vkSetArenaBounds(float, float, float, float);

void sr_SetRenderContext(rRenderContext context)
{
    s_currentRenderContext = context;
    sr_vkSetRenderContext(static_cast<int>(context));
}

rRenderContext sr_GetRenderContext()
{
    return s_currentRenderContext;
}

void sr_SetArenaBounds(float lowX, float lowY, float highX, float highY)
{
    sr_vkSetArenaBounds(lowX, lowY, highX, highY);
}

extern void sr_vkSetCameraWorldPos(float x, float y, float z);
void sr_SetCameraWorldPos(float x, float y, float z)
{
    sr_vkSetCameraWorldPos(x, y, z);
}

extern void sr_vkGetCameraWorldPos(float& x, float& y, float& z);
void sr_GetCameraWorldPos(float& x, float& y, float& z)
{
    sr_vkGetCameraWorldPos(x, y, z);
}

#endif // DEDICATED
