/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2016> Matthew Waters <matthew@centricular.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "vrtestsrc.h"

#include "../../gst-libs/gst/3d/gst3dshader.h"
#include "../../gst-libs/gst/3d/gst3dmesh.h"
#include "../../gst-libs/gst/3d/gst3dcamera_arcball.h"
#include "../../gst-libs/gst/3d/gst3dnode.h"

struct SrcShader
{
  struct BaseSrcImpl base;
  Gst3DCameraArcball *camera;
  GList *nodes;
};

/*
static gboolean
_src_mandelbrot_src_event (gpointer impl, GstEvent * event)
{
  // GstPointCloudBuilder *self = GST_POINT_CLOUD_BUILDER (trans);

  struct SrcShader *src = impl;

  GST_DEBUG ("handling %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:
      //event =
      //    GST_EVENT (gst_mini_object_make_writable (GST_MINI_OBJECT (event)));
      //gst_3d_renderer_navigation_event (GST_ELEMENT (src), event);
      //gst_3d_camera_arcball_navigation_event (src->camera, event);
      break;
    default:
      break;
  }

  // gst_event_unref (event);
  // return GST_BASE_TRANSFORM_CLASS (parent_class)->src_event (trans, event);
  return TRUE;
}
*/


static gboolean
_src_mandelbrot_init (gpointer impl, GstGLContext * context,
    GstVideoInfo * v_info)
{
  struct SrcShader *src = impl;
  // GError *error = NULL;

  src->base.context = context;

  GstGLFuncs *gl = context->gl_vtable;

  src->camera = gst_3d_camera_arcball_new ();

/*
  if (src->shader)
    gst_object_unref (src->shader);
*/




  // Gst3DMesh * plane_mesh = gst_3d_mesh_new_plane (context, 1.0);
  Gst3DMesh *plane_mesh = gst_3d_mesh_new_cube (context);
  Gst3DNode *plane_node = gst_3d_node_new (context);
  plane_node->meshes = g_list_append (plane_node->meshes, plane_mesh);
  plane_node->shader =
      gst_3d_shader_new_vert_frag (context, "mvp_uv.vert", "debug_uv.frag");
  gst_gl_shader_use (plane_node->shader->shader);
  gst_3d_mesh_bind_shader (plane_mesh, plane_node->shader);

  src->nodes = g_list_append (src->nodes, plane_node);


  Gst3DNode *axes_node = gst_3d_node_new_debug_axes (context);
  src->nodes = g_list_append (src->nodes, axes_node);

  //src->plane_mesh = gst_3d_mesh_new_sphere (context, 2.0, 20, 20);
  // src->plane_mesh->draw_mode = GL_LINES;

  // _create_debug_axes (src);

  gl->Disable (GL_CULL_FACE);

/*
  gst_gl_shader_use (axes_node->shader->shader);
  gst_gl_shader_use (src->shader);
  gst_gl_shader_set_uniform_1f (src->shader, "aspect_ratio",
      (gfloat) GST_VIDEO_INFO_WIDTH (v_info) /
      (gfloat) GST_VIDEO_INFO_HEIGHT (v_info));
  gst_gl_context_clear_shader (src->base.context);
*/


  return TRUE;
}

static gboolean
_src_mandelbrot_draw (gpointer impl)
{
  struct SrcShader *src = impl;

  g_return_val_if_fail (src->base.context, FALSE);

  //TODO: exit with an error message (shader compiler mostly)
  // if (!src->shader)
  //  exit (0);
  // g_return_val_if_fail (src->shader, FALSE);

  GstGLFuncs *gl = src->base.context->gl_vtable;

  // gl->Viewport (0, 0, self->eye_width, self->eye_height);
  gl->Clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  /*
     gst_gl_shader_use (src->shader->shader);
     gst_gl_shader_set_uniform_1f (src->shader->shader, "time",
     (gfloat) src->base.src->running_time / GST_SECOND);
   */
  Gst3DCameraArcball *camera = src->base.src->camera;

  gst_3d_camera_arcball_update_view (camera);
  gl->Enable (GL_DEPTH_TEST);

  GList *l;
  for (l = src->nodes; l != NULL; l = l->next) {
    Gst3DNode *node = (Gst3DNode *) l->data;
    gst_gl_shader_use (node->shader->shader);
    gst_3d_shader_upload_matrix (node->shader, &camera->mvp, "mvp");
    gst_3d_node_draw (node);
  }
  gl->Disable (GL_DEPTH_TEST);
  //gst_3d_mesh_draw_arrays(src->plane_mesh);

  // gl->BindVertexArray (0);
  // gst_gl_context_clear_shader (src->base.context);

  return TRUE;
}

static void
_src_mandelbrot_free (gpointer impl)
{
  struct SrcShader *src = impl;

  if (!src)
    return;

  g_free (impl);
}

static gpointer
_src_mandelbrot_new (GstVRTestSrc * test)
{
  struct SrcShader *src = g_new0 (struct SrcShader, 1);

  src->base.src = test;

  return src;
}

static const struct SrcFuncs src_mandelbrot = {
  GST_VR_TEST_SRC_MANDELBROT,
  _src_mandelbrot_new,
  _src_mandelbrot_init,
  _src_mandelbrot_draw,
  _src_mandelbrot_free,
};


static const struct SrcFuncs *src_impls[] = {
  &src_mandelbrot,
};

const struct SrcFuncs *
gst_vr_test_src_get_src_funcs_for_pattern (GstVRTestSrcPattern pattern)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (src_impls); i++) {
    if (src_impls[i]->pattern == pattern)
      return src_impls[i];
  }

  return NULL;
}
