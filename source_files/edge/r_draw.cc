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


static int glbsp_last_prog_time = 0;


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
		(textmap && (textmap->special & COLSP_Whiten)) ? font_whiten_map : palremap);

	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, tex_id);
 
	if (alpha >= 0.99f && image->opacity == OPAC_Solid)
		glDisable(GL_ALPHA_TEST);
	else
	{
		glEnable(GL_ALPHA_TEST);

		if (! (alpha < 0.11f || image->opacity == OPAC_Complex))
			glAlphaFunc(GL_GREATER, alpha * 0.66f);
	}

	if (image->opacity == OPAC_Complex || alpha < 0.99f)
		glEnable(GL_BLEND);

	if (textmap)
	{
		rgbcol_t col = V_GetFontColor(textmap);

		r = RGB_RED(col) / 255.0;
		g = RGB_GRN(col) / 255.0;
		b = RGB_BLU(col) / 255.0;
	}

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	GLint image_vertices[] =
	{
		x1, y1,
		x2, y1,
		x2, y2,
		x1, y2
	};
	GLfloat image_texcoords[] =
	{
		tx1, ty1,
		tx2, ty1,
		tx2, ty2,
		tx1, ty2
	};
	GLfloat image_colors[] =
	{
		r, g, b, alpha,
		r, g, b, alpha,
		r, g, b, alpha,
		r, g, b, alpha
	};
	glColorPointer(4, GL_FLOAT, 0, image_colors);
	glVertexPointer(2, GL_INT, 0, image_vertices);
	glTexCoordPointer(2, GL_FLOAT, 0, image_texcoords);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);

	glDisable(GL_TEXTURE_2D);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_BLEND);

	glAlphaFunc(GL_GREATER, 0);
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


static void ProgressSection(const byte *logo_lum, int lw, int lh,
	const byte *text_lum, int tw, int th,
	float cr, float cg, float cb,
	int *y, int perc, float alpha)
{
	float zoom = 1.0f;

	(*y) -= (int)(lh * zoom);

	glRasterPos2i(20, *y);
	glPixelZoom(zoom, zoom);
	glDrawPixels(lw, lh, GL_LUMINANCE, GL_UNSIGNED_BYTE, logo_lum);

	(*y) -= th + 20;

	glRasterPos2i(20, *y);
	glPixelZoom(1.0f, 1.0f);
	glDrawPixels(tw, th, GL_LUMINANCE, GL_UNSIGNED_BYTE, text_lum);

	int px = 20;
	int pw = SCREENWIDTH - 80;
	int ph = 30;
	int py = *y - ph - 20;

	int x = (pw-8) * perc / 100;

	std::vector<GLint> progress_vertices;
	std::vector<GLfloat> progress_colors;

  	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	progress_vertices =
	{
		px, py,
		px, py+ph,
		px+pw, py+ph,
		px+pw, py,
		px, py,
	};
	progress_colors =
	{
		0.6f, 0.6f, 0.6f, alpha,
		0.6f, 0.6f, 0.6f, alpha,
		0.6f, 0.6f, 0.6f, alpha,
		0.6f, 0.6f, 0.6f, alpha,
		0.6f, 0.6f, 0.6f, alpha,
	};
	glVertexPointer(2, GL_INT, 0, progress_vertices.data());
	glColorPointer(4, GL_FLOAT, 0, progress_colors.data());
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	progress_vertices.clear();
	progress_colors.clear();
	progress_vertices =
	{
		px+2, py+2,
		px+2, py+ph-2,
		px+pw-2, py+ph-2,
		px+pw-2, py+2,
	};
	progress_colors =
	{
		0.0f, 0.0f, 0.0f, alpha,
		0.0f, 0.0f, 0.0f, alpha,
		0.0f, 0.0f, 0.0f, alpha,
		0.0f, 0.0f, 0.0f, alpha,
	};
	glVertexPointer(2, GL_INT, 0, progress_vertices.data());
	glColorPointer(4, GL_FLOAT, 0, progress_colors.data());
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	progress_vertices.clear();
	progress_colors.clear();
	progress_vertices =
	{
		px+4, py+4,
		px+4, py+ph-4,
		px+4+x, py+ph-4,
		px+4+x, py+4
	};
	progress_colors =
	{
		cr, cg, cb, alpha,
		cr, cg, cb, alpha,
		cr, cg, cb, alpha,
		cr, cg, cb, alpha,
	};
	glVertexPointer(2, GL_INT, 0, progress_vertices.data());
	glColorPointer(4, GL_FLOAT, 0, progress_colors.data());
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);

	(*y) = py;
}


void RGL_DrawProgress(int perc, int glbsp_perc)
{
	/* show EDGE logo and a progress indicator */

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glEnable(GL_BLEND);

	int y = SCREENHEIGHT - 20;
	
	const byte *logo_lum; int lw, lh;
	const byte *text_lum; int tw, th;

	logo_lum = RGL_LogoImage(&lw, &lh);
	text_lum = RGL_InitImage(&tw, &th);

	ProgressSection(logo_lum, lw, lh, text_lum, tw, th,
		0.4f, 0.6f, 1.0f, &y, perc, 1.0f);

	y -= 10;

	if (glbsp_perc >= 0 || glbsp_last_prog_time > 0)
	{
		// logic here is to avoid the brief flash of progress
		int tim = I_GetTime();
		float alpha = 1.0f;

		if (glbsp_perc >= 0)
			glbsp_last_prog_time = tim;
		else
		{
			alpha = 1.0f - float(tim - glbsp_last_prog_time) / (TICRATE*3/2);

			if (alpha < 0)
			{
				alpha = 0;
				glbsp_last_prog_time = 0;
			}

			glbsp_perc = 100;
		}

		text_lum = RGL_BuildImage(&tw, &th);

		ProgressSection(0, 0, 0, text_lum, tw, th,
			1.0f, 0.2f, 0.1f, &y, glbsp_perc, alpha);
	}

	glDisable(GL_BLEND);

	I_FinishFrame();
	I_StartFrame();
}


//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab
