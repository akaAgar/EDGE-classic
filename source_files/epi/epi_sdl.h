//----------------------------------------------------------------------------
//  EDGE Platform Interface Header - SDL
//----------------------------------------------------------------------------
//
//  Copyright (c) 2023  The EDGE Team.
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 3
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//----------------------------------------------------------------------------

#ifndef __EPI_SDL_H__
#define __EPI_SDL_H__
// SDL2 includes (right now only for FS_OpenDir)
#ifdef _MSC_VER
#include "SDL.h"
#elif __APPLE__
#include <SDL.h>
#else
#include <SDL2/SDL.h>
#endif
#endif /*__EPI_SDL_H__*/