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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>

#define GST_USE_UNSTABLE_API
#include <gst/gl/gl.h>

#include "gst3dcamera_arcball.h"
#include "gst3dglm.h"
#include "gst3drenderer.h"
#include "gst3dmath.h"

#define GST_CAT_DEFAULT gst_3d_camera_arcball_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

G_DEFINE_TYPE_WITH_CODE (Gst3DCameraArcball, gst_3d_camera_arcball,
    GST_3D_TYPE_CAMERA, GST_DEBUG_CATEGORY_INIT (gst_3d_camera_arcball_debug,
        "3dcamera_arcball", 0, "camera_arcball"));


void
gst_3d_camera_arcball_init (Gst3DCameraArcball * self)
{
  self->center_distance = 2.5;
  self->scroll_speed = 0.03;
  self->rotation_speed = 0.002;
  self->cursor_last_x = 0;
  self->cursor_last_y = 0;

  self->theta = 5.0;
  self->phi = 5.0;

  self->pressed_mouse_button = 0;
}

Gst3DCameraArcball *
gst_3d_camera_arcball_new (void)
{
  Gst3DCameraArcball *camera_arcball =
      g_object_new (GST_3D_TYPE_CAMERA_ARCBALL, NULL);
  return camera_arcball;
}

static void
gst_3d_camera_arcball_finalize (GObject * object)
{
  Gst3DCameraArcball *self = GST_3D_CAMERA_ARCBALL (object);
  g_return_if_fail (self != NULL);

  G_OBJECT_CLASS (gst_3d_camera_arcball_parent_class)->finalize (object);
}

static void
gst_3d_camera_arcball_class_init (Gst3DCameraArcballClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = gst_3d_camera_arcball_finalize;
/*
  gobject_class->set_property = gst_hmd_warp_set_property;
  gobject_class->get_property = gst_hmd_warp_get_property;
*/
  Gst3DCameraClass *camera_class = GST_3D_CAMERA_CLASS (klass);
  camera_class->update_view = gst_3d_camera_arcball_update_view;
}


void
gst_3d_camera_arcball_translate (Gst3DCameraArcball * self, float z)
{
  self->center_distance += z * self->scroll_speed;
  GST_DEBUG ("center distance: %f", self->center_distance);
  gst_3d_camera_arcball_update_view (self);
}

void
gst_3d_camera_arcball_rotate (Gst3DCameraArcball * self, gdouble x, gdouble y)
{
  float delta_theta = y * self->rotation_speed;
  float delta_phi = x * self->rotation_speed;

  self->phi += delta_phi;

  // 2π < θ < π to avoid gimbal lock
  float next_theta_pi = (self->theta + delta_theta) / M_PI;
  if (next_theta_pi < 2.0 && next_theta_pi > 1.0)
    self->theta += delta_theta;

  GST_DEBUG ("θ = %fπ ϕ = %fπ", self->theta / M_PI, self->phi / M_PI);
  gst_3d_camera_arcball_update_view (self);
}

void
gst_3d_camera_arcball_update_view (Gst3DCameraArcball * self)
{
  float radius = exp (self->center_distance);

  Gst3DCamera *cam = GST_3D_CAMERA (self);

  graphene_vec3_init (&cam->eye,
      radius * sin (self->theta) * cos (self->phi),
      radius * -cos (self->theta),
      radius * sin (self->theta) * sin (self->phi));

  graphene_matrix_t projection_matrix;
  graphene_matrix_init_perspective (&projection_matrix,
      cam->fov, cam->aspect, cam->znear, cam->zfar);

  graphene_matrix_t view_matrix;
  graphene_matrix_init_look_at (&view_matrix, &cam->eye, &cam->center,
      &cam->up);

  /* fix graphene look at */
  graphene_matrix_t v_inverted;
  graphene_matrix_t v_inverted_fix;
  graphene_matrix_inverse (&view_matrix, &v_inverted);
  gst_3d_math_matrix_negate_component (&v_inverted, 3, 2, &v_inverted_fix);

  graphene_matrix_multiply (&v_inverted_fix, &projection_matrix, &cam->mvp);
}

void
gst_3d_camera_arcball_navigation_event (Gst3DCameraArcball * self,
    GstEvent * event)
{
  GstNavigationEventType event_type = gst_navigation_event_get_type (event);
  GstStructure *structure = (GstStructure *) gst_event_get_structure (event);

  switch (event_type) {
    case GST_NAVIGATION_EVENT_MOUSE_MOVE:{
      gdouble x, y;
      gst_structure_get_double (structure, "pointer_x", &x);
      gst_structure_get_double (structure, "pointer_y", &y);

      // hanlde the mouse motion for zooming and rotating the view
      gdouble dx, dy;
      dx = x - self->cursor_last_x;
      dy = y - self->cursor_last_y;

      if (self->pressed_mouse_button == 1) {
        GST_DEBUG ("Rotating [%fx%f]", dx, dy);
        gst_3d_camera_arcball_rotate (self, dx, dy);
      }
      self->cursor_last_x = x;
      self->cursor_last_y = y;
      break;
    }
    case GST_NAVIGATION_EVENT_MOUSE_BUTTON_RELEASE:{
      gint button;
      gst_structure_get_int (structure, "button", &button);
      self->pressed_mouse_button = 0;

      if (button == 1) {
        // GST_DEBUG("first button release");
        gst_structure_get_double (structure, "pointer_x", &self->cursor_last_x);
        gst_structure_get_double (structure, "pointer_y", &self->cursor_last_y);
      } else if (button == 4) {
        // GST_DEBUG("wheel up");
        gst_3d_camera_arcball_translate (self, -1.0);
      } else if (button == 5) {
        // GST_DEBUG("wheel down");
        gst_3d_camera_arcball_translate (self, 1.0);
      }
      break;
    }
    case GST_NAVIGATION_EVENT_MOUSE_BUTTON_PRESS:{
      gint button;
      gst_structure_get_int (structure, "button", &button);
      self->pressed_mouse_button = button;
      break;
    }
    default:
      break;
  }
}
