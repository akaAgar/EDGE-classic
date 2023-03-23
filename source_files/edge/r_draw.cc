//----------------------------------------------------------------------------
//  EDGE 2D DRAWING STUFF
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

#include "i_defs.h"
#include "i_defs_gl.h"

#include "g_game.h"
#include "r_misc.h"
#include "r_gldefs.h"
#include "r_units.h"
#include "r_colormap.h"
#include "r_draw.h"
#include "r_modes.h"
#include "r_image.h"

#include <vector>

void RGL_NewScreenSize(int width, int height, int bits)
{
	//!!! quick hack
	RGL_SetupMatrices2D();

	// prevent a visible border with certain cards/drivers
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);
}


void RGL_DrawImage(float x, float y, float w, float h, const image_c *image, 
				   float tx1, float ty1, float tx2, float ty2,
				   const colourmap_c *textmap, float alpha,
				   const colourmap_c *palremap)
{
	int x1 = I_ROUND(x);
	int y1 = I_ROUND(y);
	int x2 = I_ROUND(x+w+0.25f);
	int y2 = I_ROUND(y+h+0.25f);

	if (x1 == x2 || y1 == y2)
		return;

	float r = 1.0f, g = 1.0f, b = 1.0f;

	GLuint tex_id = W_ImageCache(image, true,
		(textmap && (textmap->special & COLSP_Whiten)) ? NULL : palremap, (textmap && (textmap->special & COLSP_Whiten)) ? true : false);

	int blending = BL_NONE;
 
	if (alpha >= 0.99f && image->opacity == OPAC_Solid)
	{ /* Nothing needed */ }
	else
	{
		blending |= BL_Alpha;

		if (! (alpha < 0.11f || image->opacity == OPAC_Complex))
			blending |= BL_Less;
	}

	if (image->opacity == OPAC_Complex || alpha < 0.99f)
		blending |= BL_Less; // Redundant? - Dasho

	if (textmap)
	{
		rgbcol_t col = V_GetFontColor(textmap);

		r = RGB_RED(col) / 255.0;
		g = RGB_GRN(col) / 255.0;
		b = RGB_BLU(col) / 255.0;
	}

	int first_vert_index = 0;

	RGL_StartUnits(false);

	local_gl_unit_t * glunit = RGL_BeginUnit(
			 GL_TRIANGLES, 4, 6,
			 GL_MODULATE, tex_id,
			 ENV_NONE, 0, 0, blending, &first_vert_index);

	glunit->indices[0] = first_vert_index;
	glunit->indices[1] = first_vert_index + 1;
	glunit->indices[2] = first_vert_index + 2;
	glunit->indices[3] = first_vert_index;
	glunit->indices[4] = first_vert_index + 2;
	glunit->indices[5] = first_vert_index + 3;

	local_verts[first_vert_index].texc->Set(tx1, ty1);
	local_verts[first_vert_index].pos = {(float)x1, (float)y1, 0.0f};
	local_verts[first_vert_index+1].texc->Set(tx2, ty1);
	local_verts[first_vert_index+1].pos = {(float)x2, (float)y1, 0.0f};
	local_verts[first_vert_index+2].texc->Set(tx2, ty2);
	local_verts[first_vert_index+2].pos = {(float)x2, (float)y2, 0.0f};
	local_verts[first_vert_index+3].texc->Set(tx1, ty2);
	local_verts[first_vert_index+3].pos = {(float)x1, (float)y2, 0.0f};
	for (int i=0; i < 4; i++)
	{
		local_verts[first_vert_index+i].rgba[0] = r;
		local_verts[first_vert_index+i].rgba[1] = g;
		local_verts[first_vert_index+i].rgba[2] = b;
		local_verts[first_vert_index+i].rgba[3] = 1.0f;
	}
	RGL_EndUnit(4);

	RGL_FinishUnits();
}


void RGL_ReadScreen(int x, int y, int w, int h, byte *rgb_buffer)
{
	glFlush();

	glPixelZoom(1.0f, 1.0f);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	for (; h > 0; h--, y++)
	{
		glReadPixels(x, y, w, 1, GL_RGB, GL_UNSIGNED_BYTE, rgb_buffer);

		rgb_buffer += w * 3;
	}
}

//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab
