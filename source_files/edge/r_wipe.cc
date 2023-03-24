//----------------------------------------------------------------------------
//  EDGE OpenGL Rendering (Wipes)
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

#include "i_defs.h"
#include "i_defs_gl.h"

#include "image_data.h"

#include "m_random.h"
#include "r_gldefs.h"
#include "r_wipe.h"
#include "r_image.h"
#include "r_modes.h"
#include "r_texgl.h"
#include "r_units.h"

extern cvar_c r_doubleframes;

// we're limited to one wipe at a time...
static int cur_wipe_reverse = 0;
static wipetype_e cur_wipe_effect = WIPE_None;

static int cur_wipe_progress;
static int cur_wipe_lasttime;

static GLuint cur_wipe_tex = 0;
static float cur_wipe_right;
static float cur_wipe_top;


#define MELT_DIVS  128
static int melt_yoffs[MELT_DIVS+1];


static inline byte SpookyAlpha(int x, int y)
{
	y += (x & 32) / 2;

	x = (x & 31) - 15;
	y = (y & 31) - 15;

	return (x*x + y * y) / 2;
}

static void CaptureScreenAsTexture(bool speckly, bool spooky)
{
	int total_w = W_MakeValidSize(SCREENWIDTH);
	int total_h = W_MakeValidSize(SCREENHEIGHT);

	epi::image_data_c img(total_w, total_h, 4);

	img.Clear();

	cur_wipe_right = SCREENWIDTH  / (float)total_w;
	cur_wipe_top   = SCREENHEIGHT / (float)total_h;

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	for (int y=0; y < SCREENHEIGHT; y++)
	{
		u8_t *dest = img.PixelAt(0, y);

		glReadPixels(0, y, SCREENWIDTH, 1, GL_RGBA, GL_UNSIGNED_BYTE, dest);

		int rnd_val = y;

		if (spooky)
		{
			for (int x=0; x < total_w; x++)
				dest[4*x+3] = SpookyAlpha(x, y);
		}
		else if (speckly)
		{
			for (int x=0; x < total_w; x++)
			{
				rnd_val = rnd_val * 1103515245 + 12345;

				dest[4*x+3] = (rnd_val >> 16);
			}
		}
	}

	cur_wipe_tex = R_UploadTexture(&img);
}

static void RGL_Init_Melt(void)
{
	int x, r;

	melt_yoffs[0] = - (M_Random() % 16);

	for (x=1; x <= MELT_DIVS; x++)
	{
		r = (M_Random() % 3) - 1;

		melt_yoffs[x] = melt_yoffs[x-1] + r;
		melt_yoffs[x] = MAX(-15, MIN(0, melt_yoffs[x]));
	}
}

static void RGL_Update_Melt(int tics)
{
	int x, r;

	for (; tics > 0; tics--)
	{
		for (x=0; x <= MELT_DIVS; x++)
		{
			r = melt_yoffs[x];

			if (r < 0)
				r = 1;
			else if (r > 15)
				r = 8;
			else
				r += 1;

			melt_yoffs[x] += r;
		}
	}
}


void RGL_InitWipe(int reverse, wipetype_e effect)
{
	cur_wipe_reverse  = reverse;
	cur_wipe_effect   = effect;

	cur_wipe_progress =  0;
	cur_wipe_lasttime = -1;

	if (cur_wipe_effect == WIPE_None)
		return;

	CaptureScreenAsTexture(effect == WIPE_Pixelfade,
		effect == WIPE_Spooky);

	if (cur_wipe_effect == WIPE_Melt)
		RGL_Init_Melt();
}


void RGL_StopWipe(void)
{
	cur_wipe_effect = WIPE_None;

	if (cur_wipe_tex != 0)
	{
		glDeleteTextures(1, &cur_wipe_tex);
		cur_wipe_tex = 0;
	}
}


//----------------------------------------------------------------------------

static void RGL_Wipe_Fading(float how_far)
{
	int first_vert_index = 0;

	local_gl_unit_t *glunit = RGL_BeginUnit(
			 GL_TRIANGLES, 4, 6,
			 GL_MODULATE, cur_wipe_tex,
			 ENV_NONE, 0, 0, BL_Alpha | BL_NoZBuf, &first_vert_index);

	glunit->indices[0] = first_vert_index;
	glunit->indices[1] = first_vert_index + 1;
	glunit->indices[2] = first_vert_index + 2;
	glunit->indices[3] = first_vert_index;
	glunit->indices[4] = first_vert_index + 2;
	glunit->indices[5] = first_vert_index + 3;

	local_verts[first_vert_index].texc->Set(0.0f, 0.0f);
	local_verts[first_vert_index].pos = {0.0f, 0.0f, 0.0f};
	local_verts[first_vert_index+1].texc->Set(0.0f, cur_wipe_top);
	local_verts[first_vert_index+1].pos = {0, (float)SCREENHEIGHT, 0.0f};
	local_verts[first_vert_index+2].texc->Set(cur_wipe_right, cur_wipe_top);
	local_verts[first_vert_index+2].pos = {(float)SCREENWIDTH, (float)SCREENHEIGHT, 0.0f};
	local_verts[first_vert_index+3].texc->Set(cur_wipe_right, 0.0f);
	local_verts[first_vert_index+3].pos = {(float)SCREENWIDTH, 0.0f, 0.0f};
	for (int i=0; i < 4; i++)
	{
		local_verts[first_vert_index+i].rgba[0] = 1.0f;
		local_verts[first_vert_index+i].rgba[1] = 1.0f;
		local_verts[first_vert_index+i].rgba[2] = 1.0f;
		local_verts[first_vert_index+i].rgba[3] = 1.0f - how_far;
	}
	RGL_EndUnit(4);
}

static void RGL_Wipe_Pixelfade(float how_far)
{
	int first_vert_index = 0;
	local_gl_unit_t *glunit = RGL_BeginUnit(
			 GL_TRIANGLES, 4, 6,
			 GL_MODULATE, cur_wipe_tex,
			 ENV_NONE, 0, 0, BL_Alpha | BL_GEqual | BL_NoZBuf, &first_vert_index);

	glunit->alpha_test_value = how_far;

	glunit->indices[0] = first_vert_index;
	glunit->indices[1] = first_vert_index + 1;
	glunit->indices[2] = first_vert_index + 2;
	glunit->indices[3] = first_vert_index;
	glunit->indices[4] = first_vert_index + 2;
	glunit->indices[5] = first_vert_index + 3;

	local_verts[first_vert_index].texc->Set(0.0f, 0.0f);
	local_verts[first_vert_index].pos = {0.0f, 0.0f, 0.0f};
	local_verts[first_vert_index+1].texc->Set(0.0f, cur_wipe_top);
	local_verts[first_vert_index+1].pos = {0.0f, (float)SCREENHEIGHT, 0.0f};
	local_verts[first_vert_index+2].texc->Set(cur_wipe_right, cur_wipe_top);
	local_verts[first_vert_index+2].pos = {(float)SCREENWIDTH, (float)SCREENHEIGHT, 0.0f};
	local_verts[first_vert_index+3].texc->Set(cur_wipe_right, 0.0f);
	local_verts[first_vert_index+3].pos = {(float)SCREENWIDTH, 0.0f, 0.0f};
	for (int i=0; i < 4; i++)
	{
		local_verts[first_vert_index+i].rgba[0] = 1.0f;
		local_verts[first_vert_index+i].rgba[1] = 1.0f;
		local_verts[first_vert_index+i].rgba[2] = 1.0f;
		local_verts[first_vert_index+i].rgba[3] = 1.0f - how_far;
	}
	RGL_EndUnit(4);
}

static void RGL_Wipe_Melt(void)
{
	int first_vert_index = 0;

	local_gl_unit_t * glunit = RGL_BeginUnit(GL_TRIANGLES, 258,
			768, GL_MODULATE, cur_wipe_tex, GL_MODULATE,
			0, 0, BL_Alpha | BL_NoZBuf, &first_vert_index);

	int ind = 0;

	for (int v_idx = 0; v_idx + 2 < 258; v_idx += 2)
	{
		glunit->indices[ind++] = first_vert_index + v_idx;
		glunit->indices[ind++] = first_vert_index + v_idx + 1;
		glunit->indices[ind++] = first_vert_index + v_idx + 3;
		glunit->indices[ind++] = first_vert_index + v_idx;
		glunit->indices[ind++] = first_vert_index + v_idx + 3;
		glunit->indices[ind++] = first_vert_index + v_idx + 2;
	}

	ind = 0;

	for (int x=0; x <= MELT_DIVS; x++)
	{
		int yoffs = MAX(0, melt_yoffs[x]);

		float sx = (float) x * SCREENWIDTH / MELT_DIVS;
		float sy = (float) (200 - yoffs) * SCREENHEIGHT / 200.0f;

		float tx = cur_wipe_right * (float) x / MELT_DIVS;

		local_verts[first_vert_index+ind].texc->Set(tx, cur_wipe_top);
		local_verts[first_vert_index+ind].pos = {sx, sy};
		local_verts[first_vert_index+ind].rgba[0] = 1.0f;
		local_verts[first_vert_index+ind].rgba[1] = 1.0f;
		local_verts[first_vert_index+ind].rgba[2] = 1.0f;
		local_verts[first_vert_index+ind].rgba[3] = 1.0f;
		ind++;
		local_verts[first_vert_index+ind].texc->Set(tx, 0.0f);
		local_verts[first_vert_index+ind].pos = {sx, sy - SCREENHEIGHT};
		local_verts[first_vert_index+ind].rgba[0] = 1.0f;
		local_verts[first_vert_index+ind].rgba[1] = 1.0f;
		local_verts[first_vert_index+ind].rgba[2] = 1.0f;
		local_verts[first_vert_index+ind].rgba[3] = 1.0f;
		ind++;
	}

	RGL_EndUnit(258);
}

static void RGL_Wipe_Slide(float how_far, float dx, float dy)
{
	dx *= how_far;
	dy *= how_far;

	int first_vert_index = 0;
	local_gl_unit_t *glunit = RGL_BeginUnit(
			 GL_TRIANGLES, 4, 6,
			 GL_MODULATE, cur_wipe_tex,
			 ENV_NONE, 0, 0, BL_Alpha | BL_NoZBuf, &first_vert_index);

	glunit->indices[0] = first_vert_index;
	glunit->indices[1] = first_vert_index + 1;
	glunit->indices[2] = first_vert_index + 2;
	glunit->indices[3] = first_vert_index;
	glunit->indices[4] = first_vert_index + 2;
	glunit->indices[5] = first_vert_index + 3;

	local_verts[first_vert_index].texc->Set(0.0f, 0.0f);
	local_verts[first_vert_index].pos = {dx, dy, 0.0f};
	local_verts[first_vert_index+1].texc->Set(0.0f, cur_wipe_top);
	local_verts[first_vert_index+1].pos = {dx, dy+(float)SCREENHEIGHT, 0.0f};
	local_verts[first_vert_index+2].texc->Set(cur_wipe_right, cur_wipe_top);
	local_verts[first_vert_index+2].pos = {dx+(float)SCREENWIDTH, dy+(float)SCREENHEIGHT, 0.0f};
	local_verts[first_vert_index+3].texc->Set(cur_wipe_right, 0.0f);
	local_verts[first_vert_index+3].pos = {dx+(float)SCREENWIDTH, dy, 0.0f};
	for (int i=0; i < 4; i++)
	{
		local_verts[first_vert_index+i].rgba[0] = 1.0f;
		local_verts[first_vert_index+i].rgba[1] = 1.0f;
		local_verts[first_vert_index+i].rgba[2] = 1.0f;
		local_verts[first_vert_index+i].rgba[3] = 1.0f;
	}
	RGL_EndUnit(4);
}

static void RGL_Wipe_Doors(float how_far)
{
	float dx = cos(how_far * M_PI / 2) * (SCREENWIDTH/2);
	float dy = sin(how_far * M_PI / 2) * (SCREENHEIGHT/3);

	for (int column = 0; column < 5; column++)
	{
		float c = column / 10.0f;
		float e = column / 5.0f;

		for (int side = 0; side < 2; side++)
		{
			float t_x1 = (side == 0) ? c : (0.9f - c);
			float t_x2 = t_x1 + 0.1f;

			float v_x1 = (side == 0) ? (dx * e) : (SCREENWIDTH - dx * (e + 0.2f));
			float v_x2 = v_x1 + dx * 0.2f;

			float v_y1 = (side == 0) ? (dy * e) : (dy * (e + 0.2f));
			float v_y2 = (side == 1) ? (dy * e) : (dy * (e + 0.2f));

			int first_vert_index = 0;

			local_gl_unit_t * glunit = RGL_BeginUnit(GL_TRIANGLES, 12,
					30, GL_MODULATE, cur_wipe_tex, GL_MODULATE,
					0, 0, BL_Alpha | BL_NoZBuf, &first_vert_index);

			int ind = 0;

			for (int v_idx = 0; v_idx + 2 < 12; v_idx += 2)
			{
				glunit->indices[ind++] = first_vert_index + v_idx;
				glunit->indices[ind++] = first_vert_index + v_idx + 1;
				glunit->indices[ind++] = first_vert_index + v_idx + 3;
				glunit->indices[ind++] = first_vert_index + v_idx;
				glunit->indices[ind++] = first_vert_index + v_idx + 3;
				glunit->indices[ind++] = first_vert_index + v_idx + 2;
			}

			ind = 0;

			for (int row = 0; row <= 5; row++)
			{
				float t_y = cur_wipe_top * row / 5.0f;

				float j1 = (SCREENHEIGHT - v_y1 * 2.0f) / 5.0f;
				float j2 = (SCREENHEIGHT - v_y2 * 2.0f) / 5.0f;

				local_verts[first_vert_index+ind].texc->Set(t_x2 * cur_wipe_right, t_y);
				local_verts[first_vert_index+ind].pos = {v_x2, v_y2 + j2 * row};
				local_verts[first_vert_index+ind].rgba[0] = 1.0f;
				local_verts[first_vert_index+ind].rgba[1] = 1.0f;
				local_verts[first_vert_index+ind].rgba[2] = 1.0f;
				local_verts[first_vert_index+ind].rgba[3] = 1.0f;
				ind++;
				local_verts[first_vert_index+ind].texc->Set(t_x1 * cur_wipe_right, t_y);
				local_verts[first_vert_index+ind].pos = {v_x1, v_y1 + j1 * row};
				local_verts[first_vert_index+ind].rgba[0] = 1.0f;
				local_verts[first_vert_index+ind].rgba[1] = 1.0f;
				local_verts[first_vert_index+ind].rgba[2] = 1.0f;
				local_verts[first_vert_index+ind].rgba[3] = 1.0f;
				ind++;
			}

			RGL_EndUnit(12);
		}
	}
}

bool RGL_DoWipe(void)
{
	//
	// NOTE: we assume 2D project matrix is already setup.
	//

	if (cur_wipe_effect == WIPE_None || cur_wipe_tex == 0)
		return true;

	// determine how many tics since we started.  If this is the first
	// call to DoWipe() since InitWipe(), then the clock starts now.
	int nowtime = I_GetTime() / (r_doubleframes.d ? 2 : 1);
	int tics = 0;

	if (cur_wipe_lasttime >= 0)
		tics = MAX(0, nowtime - cur_wipe_lasttime);
	
	cur_wipe_lasttime = nowtime;

	// hack for large delays (like when loading a level)
	tics = MIN(6, tics);

	cur_wipe_progress += tics;

	if (cur_wipe_progress > 40)  // FIXME: have option for wipe time
		return true;

	float how_far = (float) cur_wipe_progress / 40.0f;

	RGL_StartUnits(false);

	switch (cur_wipe_effect)
	{
		case WIPE_Melt:
			RGL_Wipe_Melt();
			RGL_Update_Melt(tics);
			break;

		case WIPE_Top:
			RGL_Wipe_Slide(how_far, 0, +SCREENHEIGHT);
			break;

		case WIPE_Bottom:
			RGL_Wipe_Slide(how_far, 0, -SCREENHEIGHT);
			break;

		case WIPE_Left:
			RGL_Wipe_Slide(how_far, -SCREENWIDTH, 0);
			break;

		case WIPE_Right:
			RGL_Wipe_Slide(how_far, +SCREENWIDTH, 0);
			break;


		case WIPE_Doors:
			RGL_Wipe_Doors(how_far);
			break;

		case WIPE_Spooky:  // difference is in alpha channel
		case WIPE_Pixelfade:
			RGL_Wipe_Pixelfade(how_far);
			break;

		case WIPE_Crossfade:
		default:
			RGL_Wipe_Fading(how_far);
			break;
	}

	RGL_FinishUnits();

	return false;
}

//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab
