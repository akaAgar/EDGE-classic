//----------------------------------------------------------------------------
//  EDGE OpenGL Rendering (Unit system)
//----------------------------------------------------------------------------
// 
//  Copyright (c) 1999-2009  The EDGE Team.
// 
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 2
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//----------------------------------------------------------------------------
//
//  Based on the DOOM source code, released by Id Software under the
//  following copyright:
//
//    Copyright (C) 1993-1996 by id Software, Inc.
//
//----------------------------------------------------------------------------

#ifndef __R_UNITS_H__
#define __R_UNITS_H__

#define MAX_PLVERT  64
#define MAX_L_VERT  64000

// a single vertex to pass to the GL 
typedef struct local_gl_vert_s
{
	GLfloat rgba[4];
	vec3_t pos;
	vec2_t texc[2];
	vec3_t normal;
}
local_gl_vert_t;

// a single unit (polygon, quad, etc) to pass to the GL
typedef struct local_gl_unit_s
{
	// unit mode (e.g. GL_TRIANGLE_FAN)
	GLuint shape;

	// environment modes (GL_REPLACE, GL_MODULATE, GL_DECAL, GL_ADD)
	GLuint env[2];

	// texture(s) used
	GLuint tex[2];

	// pass number (multiple pass rendering)
	int pass;

	// blending flags
	int blending;

	// Only used with GL_LINES
	float line_thickness = 1.0f;

	// Used as alpha target
	float alpha_test_value = 0.0f;

	// vertex information
	int v_count;
	int i_count;
	std::vector<GLushort> indices;
}
local_gl_unit_t;

extern local_gl_vert_t local_verts[MAX_L_VERT];

extern GLfloat cull_fog_color[4];

void RGL_InitUnits(void);
void RGL_SoftInitUnits(void);

void RGL_StartUnits(bool sort_em);
void RGL_FinishUnits(void);
void RGL_DrawUnits(void);

typedef enum
{
	BL_NONE        = 0,
	BL_Masked      = (1<<0),  // drop fragments when alpha == 0
	BL_Less        = (1<<1),  // drop fragments when alpha < color.a
	BL_Greater     = (1<<2),  // drop fragments when alpha > color.a
	BL_GEqual      = (1<<3),  // drop fragments when alpha >= color.a
	BL_Alpha       = (1<<4),  // alpha-blend with the framebuffer
	BL_Add         = (1<<5),  // additive-blend with the framebuffer
	BL_Invert      = (1<<6),  // invert color-blend
	BL_SmoothLines = (1<<7),  // Smooth GL_LINES
	BL_CullBack    = (1<<8),  // enable back-face culling
	BL_CullFront   = (1<<9),  // enable front-face culling
	BL_NoZBuf      = (1<<10), // don't update the Z buffer
	BL_ClampY      = (1<<11), // force texture to be Y clamped
	BL_RepeatX     = (1<<12), // force texture to repeat on X axis
	BL_RepeatY     = (1<<13), // force texture to repeat on Y axis
	BL_Foggable    = (1<<14), // allow fog to affect texture in multipass renders
	BL_NoFog       = (1<<15)  // force disable fog for this unit regardless 
}
blending_mode_e;

#define BL_CULL_BOTH  (BL_CullBack | BL_CullFront)

#define CUSTOM_ENV_BEGIN  0xED9E0000
#define CUSTOM_ENV_END    0xED9E00FF

typedef enum
{
	ENV_NONE = 0,
	// the texture unit is disabled (complete pass-through).

	ENV_SKIP_RGB = CUSTOM_ENV_BEGIN+1,
	// causes the RGB of the texture to be skipped, i.e. the
	// output of the texture unit is the same as the input
	// for the RGB components.  The alpha component is treated
	// normally, i.e. passed on to next texture unit.
}
edge_environment_e;

local_gl_unit_t *RGL_BeginUnit(GLuint shape, int max_vert, int max_index,
		                       GLuint env1, GLuint tex1,
							   GLuint env2, GLuint tex2,
							   int pass, int blending, int *first_vert_index);
void RGL_EndUnit(int actual_vert);


#endif /* __R_UNITS_H__ */

//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab
