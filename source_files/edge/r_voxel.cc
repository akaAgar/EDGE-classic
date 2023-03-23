//----------------------------------------------------------------------------
//  KVX/KV6 Voxels
//----------------------------------------------------------------------------
//
//  Copyright (c) 2023  The EDGE Team.
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
//  Voxel loading routines based on k8vavoom's voxelib library 
//  Copyright (C) 2022 Ketmar Dark
//
//
//----------------------------------------------------------------------------

#include "i_defs.h"
#include "i_defs_gl.h"

#include "types.h"
#include "endianess.h"
#include "image_data.h"

#include "ec_voxelib.h"

#include "r_voxel.h"
#include "r_gldefs.h"
#include "r_colormap.h"
#include "r_effects.h"
#include "r_image.h"
#include "r_misc.h"
#include "r_modes.h"
#include "r_state.h"
#include "r_shader.h"
#include "r_units.h"
#include "p_blockmap.h"
#include "r_texgl.h"

#include <vector>

extern float P_ApproxDistance(float dx, float dy, float dz);

extern cvar_c r_culling;
extern cvar_c r_fogofwar;

/*============== EDGE REPRESENTATION ====================*/

struct vxl_vertex_c
{
	float x, y, z; // coords
	float s, t; // texcoords
	float nx, ny, nz; // normals
};

std::vector<vxl_vertex_c> voxel_verts;

void ec_voxelib_callback(u32_t v0, u32_t v1, u32_t v2, void *udata)
{
	GLVoxelMesh *mesh = (GLVoxelMesh *)udata;
	voxel_verts.push_back({mesh->vertices[v0].y, -mesh->vertices[v0].x, mesh->vertices[v0].z,
	mesh->vertices[v0].s, mesh->vertices[v0].t,
	mesh->vertices[v0].nx, mesh->vertices[v0].ny, mesh->vertices[v0].nz});
	voxel_verts.push_back({mesh->vertices[v1].y, -mesh->vertices[v1].x, mesh->vertices[v1].z,
	mesh->vertices[v1].s, mesh->vertices[v1].t,
	mesh->vertices[v1].nx, mesh->vertices[v1].ny, mesh->vertices[v1].nz});
	voxel_verts.push_back({mesh->vertices[v2].y, -mesh->vertices[v2].x, mesh->vertices[v2].z,
	mesh->vertices[v2].s, mesh->vertices[v2].t,
	mesh->vertices[v2].nx, mesh->vertices[v2].ny, mesh->vertices[v2].nz});
};

struct vxl_frame_c
{
	vxl_vertex_c *vertices;
};

struct vxl_point_c
{
	float skin_s, skin_t;

	// index into frame's vertex array (mdl_frame_c::verts)
	int vert_idx;
};

struct vxl_triangle_c
{
	// index to the first point (within vxl_model_c::points).
	// All points for the strip are contiguous in that array.
	int first;
};

class vxl_model_c
{
public:
	int num_points;
	int num_tris;

	vxl_frame_c *frame;
	vxl_point_c *points;
	vxl_triangle_c *tris;

	int verts_per_frame;

	u32_t skin_id;
	int skin_width;
	int skin_height;
	float im_right;
	float im_top;

	multi_color_c *nm_colors;

	const char *name;

	GLuint vbo; // One VBO updated with lerping info

public:
	vxl_model_c(int _nframe, int _npoint, int _ntris) :
		num_points(_npoint), num_tris(_ntris), 
		verts_per_frame(0), vbo(0)
	{
		frame = new vxl_frame_c;
		points = new vxl_point_c[num_points];
		tris = new vxl_triangle_c[num_tris];
	}

	~vxl_model_c()
	{
		delete frame;
		delete[] points;
		delete[] tris;
	}
};


/*============== LOADING CODE ====================*/

vxl_model_c *VXL_LoadModel(epi::file_c *f, const char *name)
{
	int i;

	voxel_verts.clear();

	if (f->GetLength() < 4)
	{
		I_Error("VXL_LoadModel: Unable to load model!\n");
		return nullptr; // Not reached
	}

	u8_t *vox_data = f->LoadIntoMemory();

	VoxMemByteStream mst;
    VoxByteStream *xst = vox_InitMemoryStream(&mst, vox_data, f->GetLength());

	uint8_t defpal[768];
    for (int cidx = 0; cidx < 256; ++cidx) 
	{
      defpal[cidx*3+0] = playpal_data[0][cidx][0];
      defpal[cidx*3+1] = playpal_data[0][cidx][1];
      defpal[cidx*3+2] = playpal_data[0][cidx][2];
    }

	VoxelData vox;
    const VoxFmt vfmt = vox_detectFormat((const uint8_t *)vox_data);
    bool ok = false;
    switch (vfmt) {
      case VoxFmt_Unknown: // assume KVX
        I_Printf("VXL_LoadModel: loading KVX...\n");
        ok = vox_loadKVX(*xst, vox, defpal);
        break;
      case VoxFmt_KV6:
        I_Printf("VXL_LoadModel: Loading KV6...\n");
        ok = vox_loadKV6(*xst, vox);
        break;
      case VoxFmt_Vxl:
        I_Error("VXL_LoadModel: Cannot load voxel model in VXL format!");
		return nullptr; // not reached
        break;
      default:
        break;
    }
    if (!ok) 
	{
		I_Error("VXL_LoadModel: Failed to load voxel model!\n");
		return nullptr; // not reached
	}

	bool doHollowFill = true;
    bool fixTJunctions = false;
    const uint32_t BreakIndex = 65535;
    int optLevel = 3;

	vox.optimise(doHollowFill);

	vox.cz = 0.0f; // otherwise loaded voxel will have (0,0,0) at its center
    VoxelMesh vmesh;
    vmesh.createFrom(vox, optLevel);
    vox.clear();

	GLVoxelMesh glvmesh;
    glvmesh.create(vmesh, fixTJunctions, BreakIndex);
    vmesh.clear();

	// Populate voxel_verts vector
	glvmesh.createTriangles(&ec_voxelib_callback, (void *)&glvmesh);

	int num_frames = 1;
	int num_verts = voxel_verts.size();
	int num_tris = num_verts / 3;
	int num_points = num_verts;

	vxl_model_c *md = new vxl_model_c(num_frames, num_points, num_tris);

	md->name = name;

	md->nm_colors = new multi_color_c[num_verts];

	for (i=0; i < num_verts; i++)
	{
		md->nm_colors[i].Clear();
	}

	md->skin_width = glvmesh.imgWidth;
	md->skin_height = glvmesh.imgHeight;
	md->im_right = (float)md->skin_width / (float)W_MakeValidSize(md->skin_width);
	md->im_top = (float)md->skin_height / (float)W_MakeValidSize(md->skin_height);

	/* PARSE SKIN */

	epi::image_data_c *tmp_img = new epi::image_data_c(md->skin_width, md->skin_height, 4);
	tmp_img->pixels = (u8_t *)glvmesh.img.ptr();
	md->skin_id = R_UploadTexture(tmp_img, UPL_MipMap | UPL_Smooth);
	tmp_img->pixels = nullptr;
	delete tmp_img; // pixels are cleaned up later with glvmesh closure

	I_Debugf("  frames:%d  points:%d  tris: %d\n",
			num_frames, num_tris * 3, num_tris);

	md->verts_per_frame = num_verts;

	I_Debugf("  verts_per_frame:%d\n", md->verts_per_frame);

	// convert glcmds into tris and points
	vxl_triangle_c *tri = md->tris;
	vxl_point_c *point = md->points;

	for (i = 0; i < num_tris; i++)
	{
		SYS_ASSERT(tri < md->tris + md->num_tris);
		SYS_ASSERT(point < md->points + md->num_points);

		tri->first = point - md->points;

		tri++;

		for (int j=0; j < 3; j++, point++)
		{
			vxl_vertex_c vert = voxel_verts[(i*3) + j];
			point->vert_idx = i * 3 + j;
			point->skin_s   = vert.s;
			point->skin_t   = vert.t;
			SYS_ASSERT(point->vert_idx >= 0);
			SYS_ASSERT(point->vert_idx < md->verts_per_frame);
		}
	}

	SYS_ASSERT(tri == md->tris + md->num_tris);
	SYS_ASSERT(point == md->points + md->num_points);

	md->frame->vertices = new vxl_vertex_c[md->verts_per_frame];
	std::copy(voxel_verts.begin(), voxel_verts.end(), md->frame->vertices);

	glvmesh.clear();
	voxel_verts.clear();
#ifdef EDGE_GL_ES2
	glGenBuffers(1, &md->vbo);
	if (md->vbo == 0)
		I_Error("VXL_LoadModel: Failed to bind VBO!\n");
	glBindBuffer(GL_ARRAY_BUFFER, md->vbo);
	glBufferData(GL_ARRAY_BUFFER, md->num_tris * 3 * sizeof(local_gl_vert_t), NULL, GL_STREAM_DRAW);
#endif
	return md;
}

/*============== MODEL RENDERING ====================*/


typedef struct
{
	mobj_t *mo;

	vxl_model_c *model;

	const vxl_frame_c *frame;
	const vxl_triangle_c *tri;

	float lerp;
	float x, y, z;

	bool is_weapon;
	bool is_fuzzy;

	// scaling
	float xy_scale;
	float  z_scale;
	float bias;

	// image size
	float im_right;
	float im_top;

	// fuzzy info
	float  fuzz_mul;
	vec2_t fuzz_add;

	// mlook vectors
	vec2_t kx_mat;
	vec2_t kz_mat;

	// rotation vectors
	vec2_t rx_mat;
	vec2_t ry_mat;

	bool is_additive;

public:
	void CalcPos(vec3_t *pos, float x1, float y1, float z1) const
	{
		x1 *= xy_scale;
		y1 *= xy_scale;
		z1 *=  z_scale;

		float x2 = x1 * kx_mat.x + z1 * kx_mat.y;
		float z2 = x1 * kz_mat.x + z1 * kz_mat.y;
		float y2 = y1;

		pos->x = x + x2 * rx_mat.x + y2 * rx_mat.y;
		pos->y = y + x2 * ry_mat.x + y2 * ry_mat.y;
		pos->z = z + z2;
	}

	void CalcNormal(vec3_t *normal, const vxl_vertex_c *vert) const
	{
		float nx1 = vert->nx;
		float ny1 = vert->ny;
		float nz1 = vert->nz;

		float nx2 = nx1 * kx_mat.x + nz1 * kx_mat.y;
		float nz2 = nx1 * kz_mat.x + nz1 * kz_mat.y;
		float ny2 = ny1;

		normal->x = nx2 * rx_mat.x + ny2 * rx_mat.y;
		normal->y = nx2 * ry_mat.x + ny2 * ry_mat.y;
		normal->z = nz2;
	}
}
model_coord_data_t;


static void ClearNormalColors(model_coord_data_t *data)
{
	for (int i=0; i < data->model->verts_per_frame; i++)
	{
		data->model->nm_colors[i].Clear();
	}
}

static void ShadeNormals(abstract_shader_c *shader,
		 model_coord_data_t *data, bool skip_calc)
{
	for (int i=0; i < data->model->verts_per_frame; i++)
	{
		float nx = 0;
		float ny = 0;
		float nz = 0;
		if (!skip_calc)
		{
			float nx1 = data->model->frame->vertices[i].nx;
			float ny1 = data->model->frame->vertices[i].ny;
			float nz1 = data->model->frame->vertices[i].nz;

			float nx2 = nx1 * data->kx_mat.x + nz1 * data->kx_mat.y;
			float nz2 = nx1 * data->kz_mat.x + nz1 * data->kz_mat.y;
			float ny2 = ny1;

			nx = nx2 * data->rx_mat.x + ny2 * data->rx_mat.y;
			ny = nx2 * data->ry_mat.x + ny2 * data->ry_mat.y;
			nz = nz2;
		}

		shader->Corner(&data->model->nm_colors[i], nx, ny, nz, data->mo, data->is_weapon);
	}
}

static void DLIT_Model(mobj_t *mo, void *dataptr)
{
	model_coord_data_t *data = (model_coord_data_t *)dataptr;

	// dynamic lights do not light themselves up!
	if (mo == data->mo)
		return;

	SYS_ASSERT(mo->dlight.shader);

	ShadeNormals(mo->dlight.shader, data, false);
}

static int MDL_MulticolMaxRGB(model_coord_data_t *data, bool additive)
{
	int result = 0;

	for (int i=0; i < data->model->verts_per_frame; i++)
	{
		multi_color_c *col = &data->model->nm_colors[i];

		int mx = additive ? col->add_MAX() : col->mod_MAX();

		result = MAX(result, mx);
	}

	return result;
}

static void UpdateMulticols(model_coord_data_t *data)
{
	for (int i=0; i < data->model->verts_per_frame; i++)
	{
		multi_color_c *col = &data->model->nm_colors[i];

		col->mod_R -= 256;
		col->mod_G -= 256;
		col->mod_B -= 256;
	}
}

static inline void ModelCoordFunc(model_coord_data_t *data,
					 int v_idx, vec3_t *pos,
					 float *rgb, vec2_t *texc, vec3_t *normal)
{
	const vxl_model_c *md = data->model;

	const vxl_frame_c *frame = data->frame;
	const vxl_triangle_c *tri  = data->tri;

	SYS_ASSERT(tri->first + v_idx >= 0);
	SYS_ASSERT(tri->first + v_idx < md->num_points);

	const vxl_point_c *point = &md->points[tri->first + v_idx];

	const vxl_vertex_c *vert = &frame->vertices[point->vert_idx];

	float x1 = vert->x;
	float y1 = vert->y;
	float z1 = vert->z + data->bias;

	if (MIR_Reflective())
		y1 = -y1;

	data->CalcPos(pos, x1, y1, z1);

	data->CalcNormal(normal, vert);

	if (data->is_fuzzy)
	{
		texc->x = point->skin_s * data->fuzz_mul + data->fuzz_add.x;
		texc->y = point->skin_t * data->fuzz_mul + data->fuzz_add.y;

		rgb[0] = rgb[1] = rgb[2] = 0;
		return;
	}

	texc->Set(point->skin_s * data->im_right, point->skin_t * data->im_top);

	multi_color_c *col = &data->model->nm_colors[point->vert_idx];

	if (! data->is_additive)
	{
		rgb[0] = col->mod_R / 255.0;
		rgb[1] = col->mod_G / 255.0;
		rgb[2] = col->mod_B / 255.0;
	}
	else
	{
		rgb[0] = col->add_R / 255.0;
		rgb[1] = col->add_G / 255.0;
		rgb[2] = col->add_B / 255.0;
	}
}


void VXL_RenderModel(vxl_model_c *md, bool is_weapon,
		             float x, float y, float z, mobj_t *mo,
					 region_properties_t *props,
					 float scale, float aspect, float bias, int rotation)
{
	if (!md->frame)
	{
		I_Debugf("Render model: bad frame for voxel %s\n", md->name);
				return;
	}

	model_coord_data_t data;

	data.is_fuzzy = (mo->flags & MF_FUZZY) ? true : false;

	float trans = mo->visibility;

	if (trans <= 0)
		return;

	int blending = BL_NONE;

	if (mo->hyperflags & HF_NOZBUFFER)
		blending |= BL_NoZBuf;

	if (MIR_Reflective())
		blending |= BL_CullFront;
	else
		blending |= BL_CullBack;

	data.mo = mo;
	data.model = md;

	data.frame = md->frame;

	data.x = x;
	data.y = y;
	data.z = z;

	data.is_weapon = is_weapon;

	data.xy_scale = scale * aspect * MIR_XYScale();
	data. z_scale = scale * MIR_ZScale();
	data.bias = bias;

	bool tilt = is_weapon || (mo->flags & MF_MISSILE) || (mo->hyperflags & HF_TILT);

	M_Angle2Matrix(tilt ? ~mo->vertangle : 0, &data.kx_mat, &data.kz_mat);

	angle_t ang = mo->angle + rotation;

	MIR_Angle(ang);

	M_Angle2Matrix(~ ang, &data.rx_mat, &data.ry_mat);

	ClearNormalColors(&data);

	GLuint skin_tex = 0;

	if (data.is_fuzzy)
	{
		skin_tex = W_ImageCache(fuzz_image, false);

		data.fuzz_mul = 0.8;
		data.fuzz_add.Set(0, 0);

		data.im_right = 1.0;
		data.im_top   = 1.0;

		if (! data.is_weapon && ! viewiszoomed)
		{
			float dist = P_ApproxDistance(mo->x - viewx, mo->y - viewy, mo->z - viewz);

			data.fuzz_mul = 70.0 / CLAMP(35, dist, 700);
		}

		FUZZ_Adjust(&data.fuzz_add, mo);

		trans = 1.0f;

		blending |=  BL_Alpha | BL_Masked;
		blending &= ~BL_Less;
	}
	else /* (! data.is_fuzzy) */
	{
		skin_tex = md->skin_id;

		if (skin_tex == 0)
			I_Error("Voxel %s missing skin?\n", md->name);

		data.im_right = md->im_right;
		data.im_top   = md->im_top;

		abstract_shader_c *shader = R_GetColormapShader(props, mo->state->bright);

		ShadeNormals(shader, &data, true);

		if (use_dlights && ren_extralight < 250)
		{
			float r = mo->radius;
			
			P_DynamicLightIterator(mo->x - r, mo->y - r, mo->z,
					               mo->x + r, mo->y + r, mo->z + mo->height,
								   DLIT_Model, &data);

			P_SectorGlowIterator(mo->subsector->sector,
					             mo->x - r, mo->y - r, mo->z,
					             mo->x + r, mo->y + r, mo->z + mo->height,
								 DLIT_Model, &data);
		}
	}


	/* draw the model */

	int num_pass = data.is_fuzzy  ? 1 :
		           data.is_weapon ? (3 + detail_level) :
					                (2 + detail_level*2);
#ifdef EDGE_GL_ES2
	glBindBuffer(GL_ARRAY_BUFFER, md->vbo);
#endif
	for (int pass = 0; pass < num_pass; pass++)
	{
		if (pass == 1)
		{
			blending &= ~BL_Alpha;
			blending |=  BL_Add;
		}

		data.is_additive = (pass > 0 && pass == num_pass-1);

		if (pass > 0 && pass < num_pass-1)
		{
			UpdateMulticols(&data);
			if (MDL_MulticolMaxRGB(&data, false) <= 0)
				continue;
		}
		else if (data.is_additive)
		{
			if (MDL_MulticolMaxRGB(&data, true) <= 0)
				continue;
		}
#ifdef EDGE_GL_ES2
		GLuint model_env = data.is_additive ? ENV_SKIP_RGB : GL_MODULATE;

		glPolygonOffset(0, -pass);

		if (blending & (BL_Masked | BL_Less))
		{
			if (blending & BL_Less)
			{
				glEnable(GL_ALPHA_TEST);
			}
			else if (blending & BL_Masked)
			{
				glEnable(GL_ALPHA_TEST);
				glAlphaFunc(GL_GREATER, 0);
			}
			else
				glDisable(GL_ALPHA_TEST);
		}

		if (blending & (BL_Alpha | BL_Add))
		{
			if (blending & BL_Add)
			{
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE);
			}
			else if (blending & BL_Alpha)
			{
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			}
			else
				glDisable(GL_BLEND);
		}

		if (blending & BL_CULL_BOTH)
		{
			if (blending & BL_CULL_BOTH)
			{
				glEnable(GL_CULL_FACE);
				glCullFace((blending & BL_CullFront) ? GL_FRONT : GL_BACK);
			}
			else
				glDisable(GL_CULL_FACE);
		}

		if (blending & BL_NoZBuf)
		{
			glDepthMask((blending & BL_NoZBuf) ? GL_FALSE : GL_TRUE);
		}

		if (blending & BL_Less)
		{
			// NOTE: assumes alpha is constant over whole model
			glAlphaFunc(GL_GREATER, trans * 0.66f);
		}

		if (pass > 0)
		{ 
			if (r_fogofwar.d || r_culling.d)
			{
				if ((blending & BL_Foggable) != BL_Foggable)
				glDisable(GL_FOG);
			}
		}

		glActiveTexture(GL_TEXTURE1);
		glDisable(GL_TEXTURE_2D);
		glActiveTexture(GL_TEXTURE0);
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, skin_tex);

		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, model_env);

		GLint old_clamp = 789;

		if (blending & BL_ClampY)
		{
			glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &old_clamp);

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
				r_dumbclamp.d ? GL_CLAMP : GL_CLAMP_TO_EDGE);
		}

		local_gl_vert_t *start = (local_gl_vert_t *)glMapBuffer(GL_ARRAY_BUFFER, GL_READ_WRITE);

		for (int i = 0; i < md->num_tris; i++)
		{
			data.tri = & md->tris[i];

			for (int v_idx=0; v_idx < 3; v_idx++)
			{
				local_gl_vert_t *dest = start + (i*3) + v_idx;

				ModelCoordFunc(&data, v_idx, &dest->pos, dest->rgba,
						&dest->texc[0], &dest->normal);

				dest->rgba[3] = trans;
			}
		}

		glUnmapBuffer(GL_ARRAY_BUFFER);

		// setup pointers to client state
		glVertexPointer(3, GL_FLOAT, sizeof(local_gl_vert_t), BUFFER_OFFSET(offsetof(local_gl_vert_t, pos.x)));
		glColorPointer (4, GL_FLOAT, sizeof(local_gl_vert_t), BUFFER_OFFSET(offsetof(local_gl_vert_t, rgba)));
		glNormalPointer(GL_FLOAT, sizeof(local_gl_vert_t), BUFFER_OFFSET(offsetof(local_gl_vert_t, normal.x)));
		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_COLOR_ARRAY);
		glEnableClientState(GL_NORMAL_ARRAY);
		glClientActiveTexture(GL_TEXTURE0);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glTexCoordPointer(2, GL_FLOAT, sizeof(local_gl_vert_t), BUFFER_OFFSET(offsetof(local_gl_vert_t, texc[0])));

		glDrawArrays(GL_TRIANGLES, 0, md->num_tris * 3);

		// restore the clamping mode
		if (old_clamp != 789)
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, old_clamp);
#else
	int first_vert_index = 0;

	local_gl_unit_t * glunit = RGL_BeginUnit(
			 GL_TRIANGLES, md->num_points, md->num_points,
			 data.is_additive ? ENV_SKIP_RGB : GL_MODULATE, skin_tex,
			 ENV_NONE, 0, pass, blending, &first_vert_index);

		for (int i = 0; i < md->num_tris; i++)
		{
			data.tri = & md->tris[i];

			for (int v_idx=0; v_idx < 3; v_idx++)
			{
				local_gl_vert_t *dest = &local_verts[first_vert_index + (i*3) + v_idx];

				ModelCoordFunc(&data, v_idx, &dest->pos, dest->rgba,
						&dest->texc[0], &dest->normal);

				dest->rgba[3] = trans;

				glunit->indices[(i*3) + v_idx] = first_vert_index + (i*3) + v_idx;
			}
		}

		RGL_EndUnit(md->num_points);
#endif
	}
#ifdef EDGE_GL_ES2
	glPolygonOffset(0, 0);

	glDisable(GL_TEXTURE_2D);

	glDepthMask(GL_TRUE);
	glCullFace(GL_BACK);

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glAlphaFunc(GL_GREATER, 0);

	glDisable(GL_ALPHA_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
#endif
}


void VXL_RenderModel_2D(vxl_model_c *md, float x, float y, 
						float xscale, float yscale, const mobjtype_c *info)
{
	// check if frame is valid
	if (!md->frame)
		return;

	GLuint skin_tex = md->skin_id;

	if (skin_tex == 0)
		I_Error("Voxel %s missing skin?\n", md->name);

	float im_right = md->im_right;
	float im_top   = md->im_top;

	xscale = yscale * info->model_scale * info->model_aspect;
	yscale = yscale * info->model_scale;

	float r,g,b,a;

	if (info->flags & MF_FUZZY)
	{
		r = 0.0f;
		g = 0.0f;
		b = 0.0f;
		a = 0.5f;
	}
	else
	{
		r = 1.0f;
		g = 1.0f;
		b = 1.0f;
		a = 1.0f;
	}

	RGL_StartUnits(false);

	int first_vert_index = 0;

	local_gl_unit_t * glunit = RGL_BeginUnit(
			 GL_TRIANGLES, md->num_points, md->num_points,
			 GL_MODULATE, skin_tex,
			 ENV_NONE, 0, 0, BL_Alpha | BL_CullBack, &first_vert_index);

	for (int i = 0; i < md->num_tris; i++)
	{
		const vxl_triangle_c *tri = & md->tris[i];

		for (int v_idx=0; v_idx < 3; v_idx++)
		{
			const vxl_frame_c *frame_ptr = md->frame;

			SYS_ASSERT(tri->first + v_idx >= 0);
			SYS_ASSERT(tri->first + v_idx < md->num_points);

			const vxl_point_c *point = &md->points[tri->first + v_idx];
			const vxl_vertex_c *vert = &frame_ptr->vertices[point->vert_idx];

			local_verts[first_vert_index + (i*3) + v_idx].texc->Set(point->skin_s * im_right, point->skin_t * im_top);

			float norm_x = md->frame->vertices[v_idx].nx;
			float norm_y = md->frame->vertices[v_idx].ny;
			float norm_z = md->frame->vertices[v_idx].nz;

			local_verts[first_vert_index + (i*3) + v_idx].normal = {norm_y, norm_z, norm_x};

			float dx = vert->x * xscale;
			float dy = vert->y * xscale;
			float dz = (vert->z + info->model_bias) * yscale;

			local_verts[first_vert_index + (i*3) + v_idx].pos = {x + dy, y + dz, dx / 256.0f};

			local_verts[first_vert_index + (i*3) + v_idx].rgba[0] = r;
			local_verts[first_vert_index + (i*3) + v_idx].rgba[1] = g;
			local_verts[first_vert_index + (i*3) + v_idx].rgba[2] = b;
			local_verts[first_vert_index + (i*3) + v_idx].rgba[3] = a;

			glunit->indices[(i*3) + v_idx] = first_vert_index + (i*3) + v_idx;
		}
	}

	RGL_EndUnit(md->num_points);

	RGL_FinishUnits();
}

//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab
