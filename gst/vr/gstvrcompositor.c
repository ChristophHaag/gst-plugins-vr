/*
 * GStreamer Plugins VR
 * Copyright (C) 2016 Lubosz Sarnecki <lubosz.sarnecki@collabora.co.uk>
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

/**
 * SECTION:element-vrcompositor
 *
 * Transform video for VR.
 *
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch-1.0 filesrc location=~/Videos/360.webm ! decodebin ! glupload ! glcolorconvert ! vrcompositor ! glimagesink
 * ]| Play spheric video.
 * </refsect2>
 */

#define GST_USE_UNSTABLE_API

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstvrcompositor.h"

#include <gst/gl/gstglapi.h>
#include <graphene-gobject.h>


#define GST_CAT_DEFAULT gst_vr_compositor_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define gst_vr_compositor_parent_class parent_class

enum
{
  PROP_0,
};

#define DEBUG_INIT \
    GST_DEBUG_CATEGORY_INIT (gst_vr_compositor_debug, "vrcompositor", 0, "vrcompositor element");

G_DEFINE_TYPE_WITH_CODE (GstVRCompositor, gst_vr_compositor,
    GST_TYPE_GL_FILTER, DEBUG_INIT);

static void gst_vr_compositor_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_vr_compositor_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_vr_compositor_set_caps (GstGLFilter * filter,
    GstCaps * incaps, GstCaps * outcaps);
static gboolean gst_vr_compositor_src_event (GstBaseTransform * trans,
    GstEvent * event);

static void gst_vr_compositor_reset_gl (GstGLFilter * filter);
static gboolean gst_vr_compositor_stop (GstBaseTransform * trans);
static gboolean gst_vr_compositor_init_scene (GstGLFilter * filter);
static void gst_vr_compositor_draw (gpointer stuff);

static gboolean gst_vr_compositor_filter_texture (GstGLFilter * filter,
    guint in_tex, guint out_tex);

static void
gst_vr_compositor_class_init (GstVRCompositorClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstBaseTransformClass *base_transform_class;

  gobject_class = (GObjectClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);
  base_transform_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = gst_vr_compositor_set_property;
  gobject_class->get_property = gst_vr_compositor_get_property;

  base_transform_class->src_event = gst_vr_compositor_src_event;

  GST_GL_FILTER_CLASS (klass)->init_fbo = gst_vr_compositor_init_scene;
  GST_GL_FILTER_CLASS (klass)->display_reset_cb = gst_vr_compositor_reset_gl;
  GST_GL_FILTER_CLASS (klass)->set_caps = gst_vr_compositor_set_caps;
  GST_GL_FILTER_CLASS (klass)->filter_texture =
      gst_vr_compositor_filter_texture;
  GST_BASE_TRANSFORM_CLASS (klass)->stop = gst_vr_compositor_stop;

  gst_element_class_set_metadata (element_class, "VR compositor",
      "Filter/Effect/Video", "Transform video for VR",
      "Lubosz Sarnecki <lubosz.sarnecki@collabora.co.uk>\n");

  GST_GL_BASE_FILTER_CLASS (klass)->supported_gl_api =
      GST_GL_API_OPENGL | GST_GL_API_OPENGL3 | GST_GL_API_GLES2;
}

static void
gst_vr_compositor_init (GstVRCompositor * self)
{
  self->shader = NULL;
  self->render_mode = GL_TRIANGLE_STRIP;
  self->in_tex = 0;
  self->mesh = NULL;
  self->camera = NULL;

  self->left_color_tex = 0;
  self->left_fbo = 0;
  self->right_color_tex = 0;
  self->right_fbo = 0;

  self->eye_width = 1;
  self->eye_height = 1;

  self->default_fbo = 0;
}

static void
gst_vr_compositor_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_vr_compositor_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_vr_compositor_set_caps (GstGLFilter * filter, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVRCompositor *self = GST_VR_COMPOSITOR (filter);

  if (!self->camera)
    self->camera = gst_3d_camera_new ();

  if (!self->camera->hmd->device)
    return FALSE;

  // TODO: get resolution from HMD
  // int w = GST_VIDEO_INFO_WIDTH (&filter->out_info);
  // int h = GST_VIDEO_INFO_HEIGHT (&filter->out_info);
  int w = 1920;
  int h = 1080;

  self->camera->aspect = (gdouble) w / (gdouble) h;
  self->eye_width = w / 2;
  self->eye_height = h;

  GST_DEBUG ("eye %dx%d", self->eye_width, self->eye_height);

  self->caps_change = TRUE;

  gst_3d_camera_update_view (self->camera);

  return TRUE;
}

void
_release_key (GstVRCompositor * self, const gchar * key)
{
  GST_DEBUG ("Event: Release %s", key);

  GList *l = self->pushed_buttons;
  while (l != NULL) {
    GList *next = l->next;
    if (g_strcmp0 (l->data, key) == 0) {
      g_free (l->data);
      self->pushed_buttons = g_list_delete_link (self->pushed_buttons, l);
    }
    l = next;
  }
}

void
_press_key (GstVRCompositor * self, const gchar * key)
{
  GList *l;
  gboolean already_pushed = FALSE;

  GST_DEBUG ("Event: Press %s", key);

  for (l = self->pushed_buttons; l != NULL; l = l->next)
    if (g_strcmp0 (l->data, key) == 0)
      already_pushed = TRUE;

  if (!already_pushed)
    self->pushed_buttons = g_list_append (self->pushed_buttons, g_strdup (key));
}

void
_print_pressed_keys (GstVRCompositor * self)
{
  GList *l;
  GST_DEBUG ("Pressed keys:");

  for (l = self->pushed_buttons; l != NULL; l = l->next)
    GST_DEBUG ("%s", (const gchar *) l->data);
}

static gboolean
gst_vr_compositor_src_event (GstBaseTransform * trans, GstEvent * event)
{
  GstVRCompositor *self = GST_VR_COMPOSITOR (trans);
  GstStructure *structure;

  GST_DEBUG_OBJECT (trans, "handling %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:
      event =
          GST_EVENT (gst_mini_object_make_writable (GST_MINI_OBJECT (event)));

      structure = (GstStructure *) gst_event_get_structure (event);

      const gchar *key = gst_structure_get_string (structure, "key");
      if (key != NULL) {
        const gchar *event_name = gst_structure_get_string (structure, "event");
        if (g_strcmp0 (event_name, "key-press") == 0)
          if (g_strcmp0 (key, "Escape") == 0) {
            gst_3d_renderer_send_eos (GST_ELEMENT (self));
            // } else if (g_strcmp0 (key, "Tab") == 0) {
            //  _toggle_render_mode (self);
          } else if (g_strcmp0 (key, "KP_Add") == 0) {
            gst_3d_hmd_eye_sep_inc (self->camera->hmd);
          } else if (g_strcmp0 (key, "KP_Subtract") == 0) {
            gst_3d_hmd_eye_sep_dec (self->camera->hmd);
          } else {
            GST_DEBUG ("%s", key);
            _press_key (self, key);
          }

        /*
           // reset rotation and position
           float zero[] = {0, 0, 0, 1};
           ohmd_device_setf(hmd, OHMD_ROTATION_QUAT, zero);
           ohmd_device_setf(hmd, OHMD_POSITION_VECTOR, zero);
         */

        else if (g_strcmp0 (event_name, "key-release") == 0)
          _release_key (self, key);
      }
      break;
    default:
      break;
  }
  return GST_BASE_TRANSFORM_CLASS (parent_class)->src_event (trans, event);
}

static void
gst_vr_compositor_reset_gl (GstGLFilter * filter)
{
  GstVRCompositor *self = GST_VR_COMPOSITOR (filter);

  if (self->shader) {
    gst_object_unref (self->shader);
    self->shader = NULL;
  }
  gst_object_unref (self->mesh);
}

static gboolean
gst_vr_compositor_stop (GstBaseTransform * trans)
{
  GstVRCompositor *self = GST_VR_COMPOSITOR (trans);
  // GstGLContext *context = GST_GL_BASE_FILTER (trans)->context;

  /* blocking call, wait until the opengl thread has destroyed the shader */
  gst_3d_shader_delete (self->shader);

  return GST_BASE_TRANSFORM_CLASS (parent_class)->stop (trans);
}

static gboolean
gst_vr_compositor_init_scene (GstGLFilter * filter)
{
  GstVRCompositor *self = GST_VR_COMPOSITOR (filter);

  GstGLContext *context = GST_GL_BASE_FILTER (self)->context;
  GstGLFuncs *gl = context->gl_vtable;
  gboolean ret = TRUE;
  if (!self->mesh) {
    self->shader = gst_3d_shader_new (context);
    ret = gst_3d_shader_from_vert_frag (self->shader, "mvp_uv.vert",
        "texture_uv.frag");
    gst_3d_shader_bind (self->shader);

    self->mesh = gst_3d_mesh_new_sphere (context, 10.0, 20, 20);
    gst_3d_mesh_bind_to_shader (self->mesh, self->shader);


    self->render_plane = gst_3d_mesh_new_plane (context, self->camera->aspect);
    gst_3d_mesh_bind_to_shader (self->render_plane, self->shader);

    gst_3d_renderer_create_fbo (gl, &self->left_fbo, &self->left_color_tex,
        self->eye_width, self->eye_height);
    gst_3d_renderer_create_fbo (gl, &self->right_fbo, &self->right_color_tex,
        self->eye_width, self->eye_height);
    gl->ClearColor (0.f, 0.f, 0.f, 0.f);
    gl->ActiveTexture (GL_TEXTURE0);
    gst_gl_shader_set_uniform_1i (self->shader->shader, "texture", 0);
  }
  return ret;
}

static gboolean
gst_vr_compositor_filter_texture (GstGLFilter * filter, guint in_tex,
    guint out_tex)
{
  GstVRCompositor *self = GST_VR_COMPOSITOR (filter);

  self->in_tex = in_tex;

  /* blocking call, use a FBO */
  gst_gl_context_use_fbo_v2 (GST_GL_BASE_FILTER (filter)->context,
      GST_VIDEO_INFO_WIDTH (&filter->out_info),
      GST_VIDEO_INFO_HEIGHT (&filter->out_info),
      filter->fbo, filter->depthbuffer,
      out_tex, gst_vr_compositor_draw, (gpointer) self);

  return TRUE;
}

/*
void
_toggle_render_mode (GstVRCompositor * self)
{
  if (self->render_mode == GL_TRIANGLES)
    self->render_mode = GL_LINES;
  else
    self->render_mode = GL_TRIANGLES;
}
*/
/*
void
_process_input (GstVRCompositor * self)
{
  //_print_pressed_keys (self);

  gfloat fast_modifier = 1.0;
  GList *l;
  for (l = self->pushed_buttons; l != NULL; l = l->next)
    if (g_strcmp0 (l->data, "Shift_L") == 0)
      fast_modifier = 3.0;


  gfloat distance = 0.01 * fast_modifier;

  for (l = self->pushed_buttons; l != NULL; l = l->next) {
    if (g_strcmp0 (l->data, "w") == 0) {
      self->camera->ztranslation += distance;
      continue;
    } else if (g_strcmp0 (l->data, "s") == 0) {
      self->camera->ztranslation -= distance;
      continue;
    }

    if (g_strcmp0 (l->data, "a") == 0) {
      self->camera->xtranslation += distance;
      continue;
    } else if (g_strcmp0 (l->data, "d") == 0) {
      self->camera->xtranslation -= distance;
      continue;
    }

    if (g_strcmp0 (l->data, "space") == 0) {
      self->camera->ytranslation += distance;
      continue;
    } else if (g_strcmp0 (l->data, "Control_L") == 0) {
      self->camera->ytranslation -= distance;
      continue;
    }
  }
}
*/
static void
_draw_eye (GstVRCompositor * self, GstGLFuncs * gl)
{
  gl->Viewport (0, 0, self->eye_width, self->eye_height);
  gl->Clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  gst_3d_mesh_bind (self->mesh);
  gst_3d_mesh_draw (self->mesh);
}

static void
_clear_state (GstGLContext * context, GstGLFuncs * gl)
{
  gl->BindVertexArray (0);
  gl->BindTexture (GL_TEXTURE_2D, 0);
  gst_gl_context_clear_shader (context);
}

static void
_draw_framebuffers_on_planes (GstVRCompositor * self, GstGLFuncs * gl)
{
  gl->BindFramebuffer (GL_FRAMEBUFFER, self->default_fbo);
  gl->Clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  graphene_matrix_t projection_ortho;
  graphene_matrix_init_ortho (&projection_ortho, -self->camera->aspect,
      self->camera->aspect, -1.0, 1.0, -1.0, 1.0);
  gst_3d_shader_upload_matrix (self->shader, &projection_ortho, "mvp");
  gst_3d_mesh_bind (self->render_plane);

  // left framebuffer
  gl->Viewport (0, 0, self->eye_width, self->eye_height);
  glBindTexture (GL_TEXTURE_2D, self->left_color_tex);
  gst_3d_mesh_draw (self->render_plane);

  // right framebuffer
  gl->Viewport (self->eye_width, 0, self->eye_width, self->eye_height);
  glBindTexture (GL_TEXTURE_2D, self->right_color_tex);
  gst_3d_mesh_draw (self->render_plane);
}

static void
gst_vr_compositor_draw (gpointer this)
{
  GstVRCompositor *self = GST_VR_COMPOSITOR (this);
  GstGLContext *context = GST_GL_BASE_FILTER (this)->context;
  GstGLFuncs *gl = context->gl_vtable;

  // _process_input (self);

  gst_3d_camera_update_view (self->camera);
  gst_gl_shader_use (self->shader->shader);
  gl->BindTexture (GL_TEXTURE_2D, self->in_tex);

  /* store current fbo id */
  if (self->default_fbo == 0)
    gl->GetIntegerv (GL_DRAW_FRAMEBUFFER_BINDING, &self->default_fbo);

  // LEFT EYE
  gl->BindFramebuffer (GL_FRAMEBUFFER, self->left_fbo);
  gst_3d_shader_upload_matrix (self->shader, &self->camera->left_vp_matrix,
      "mvp");
  _draw_eye (self, gl);

  // RIGHT EYE
  gl->BindFramebuffer (GL_FRAMEBUFFER, self->right_fbo);
  gst_3d_shader_upload_matrix (self->shader, &self->camera->right_vp_matrix,
      "mvp");
  _draw_eye (self, gl);

  _draw_framebuffers_on_planes (self, gl);
  _clear_state (context, gl);

}
