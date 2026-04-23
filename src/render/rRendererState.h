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

#ifndef RRENDERERSTATE_H
#define RRENDERERSTATE_H

//! Render context for shader effects
//! Shaders can use this to apply effects only in appropriate contexts
//! More specific contexts allow fine-grained control over effects
enum class rRenderContext
{
    Unknown = 0,       //!< Default/unknown context
    TitleScreen = 1,   //!< Title screen/logo
    Menu = 2,          //!< Menu screens, UI
    Game3D = 3,        //!< In-game 3D world (generic fallback)
    Game3D_Sky = 4,    //!< Sky/background
    Game3D_Floor = 5,  //!< Arena floor
    Game3D_RimWalls = 6,  //!< Arena rim walls
    Game3D_PlayerWalls = 7,  //!< Player cycle walls/trails
    Game3D_Cycles = 8,    //!< Player cycles
    Game3D_Zones = 9,     //!< Win zones, death zones, etc.
    Game3D_Effects = 10,  //!< Sparks, explosions, effects
    HUD = 11           //!< In-game HUD overlay
};

//! Set the current render context for shader effects
//! Call this before rendering to indicate what type of content is being rendered
//! @warning THREADING: Must only be called from the main rendering thread.
//! @param context The render context (Menu, Game3D, HUD, etc.)
void sr_SetRenderContext(rRenderContext context);

//! Get the current render context
rRenderContext sr_GetRenderContext();

//! Set arena bounds for shader effects
//! @param lowX, lowY Lower-left corner of arena
//! @param highX, highY Upper-right corner of arena
void sr_SetArenaBounds(float lowX, float lowY, float highX, float highY);

#endif // RRENDERERSTATE_H
