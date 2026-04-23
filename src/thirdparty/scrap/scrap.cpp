#include "aa_config.h"

#include <SDL3/SDL.h>

// SDL3 clipboard implementation

/*
        This file is part of Warzone 2100.
        Copyright (C) 1999-2004 Eidos Interactive
        Copyright (C) 2005-2013 Warzone 2100 Project

        Warzone 2100 is free software; you can redistribute it and/or modify
        it under the terms of the GNU General Public License as published by
        the Free Software Foundation; either version 2 of the License, or
        (at your option) any later version.

        Warzone 2100 is distributed in the hope that it will be useful,
        but WITHOUT ANY WARRANTY; without even the implied warranty of
        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
        GNU General Public License for more details.

        You should have received a copy of the GNU General Public License
        along with Warzone 2100; if not, write to the Free Software
        Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/
/** @file
* Handle clipboard text and data in arbitrary formats
*/

#include <iostream>
#include <SDL3/SDL_clipboard.h>

#include "scrap.h"
#include <memory.h>

int        init_scrap(void)
{
        return(true);
}

int lost_scrap(void)
{
        bool result = SDL_HasClipboardText();

        return !result;        // Um... fix this.

}

void put_scrap(int type, int srclen, char *src)
{
        int result = SDL_SetClipboardText(src);
        if (result)
        {
                std::cerr << "Could not put clipboard text because : " << SDL_GetError() << std::endl;
        }
}

void get_scrap(int type, int *dstlen, char **dst)
{
        char* cliptext = SDL_GetClipboardText();
        if (!cliptext)
        {
                std::cerr << "Could not put clipboard text because : " << SDL_GetError() << std::endl;
                *dstlen = -1;
        }
        *dst = cliptext;
        *dstlen = strlen(cliptext);
}

