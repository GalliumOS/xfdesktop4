/* GTK - The GIMP Toolkit
 *
 * Copyright (C) 2014 Red Hat
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
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 *     Owen Taylor <otaylor@redhat.com>
 */

/*
 * All code in this file is based on Gtk+-3 code.
 * Files: gtk/gtkcairoblur.c and gtk/gtkcssshadowvalue.c
 */

#include "gtkcairoblurprivate.h"

#include <math.h>
#include <string.h>

/* This applies a single box blur pass to a horizontal range of pixels;
 * since the box blur has the same weight for all pixels, we can
 * implement an efficient sliding window algorithm where we add
 * in pixels coming into the window from the right and remove
 * them when they leave the windw to the left.
 *
 * d is the filter width; for even d shift indicates how the blurred
 * result is aligned with the original - does ' x ' go to ' yy' (shift=1)
 * or 'yy ' (shift=-1)
 */
static void
blur_xspan (guchar *row,
            guchar *tmp_buffer,
            int     row_width,
            int     d,
            int     shift)
{
  int offset;
  int sum = 0;
  int i;

  if (d % 2 == 1)
    offset = d / 2;
  else
    offset = (d - shift) / 2;

  /* All the conditionals in here look slow, but the branches will
   * be well predicted and there are enough different possibilities
   * that trying to write this as a series of unconditional loops
   * is hard and not an obvious win. The main slow down here seems
   * to be the integer division per pixel; one possible optimization
   * would be to accumulate into two 16-bit integer buffers and
   * only divide down after all three passes. (SSE parallel implementation
   * of the divide step is possible.)
   */
  for (i = -d + offset; i < row_width + offset; i++)
    {
      if (i >= 0 && i < row_width)
        sum += row[i];

      if (i >= offset)
        {
          if (i >= d)
            sum -= row[i - d];

          tmp_buffer[i - offset] = (sum + d / 2) / d;
        }
    }

  memcpy (row, tmp_buffer, row_width);
}

static void
blur_rows (guchar *dst_buffer,
           guchar *tmp_buffer,
           int     buffer_width,
           int     buffer_height,
           int     d)
{
  int i;

  for (i = 0; i < buffer_height; i++)
    {
      guchar *row = dst_buffer + i * buffer_width;

      /* We want to produce a symmetric blur that spreads a pixel
       * equally far to the left and right. If d is odd that happens
       * naturally, but for d even, we approximate by using a blur
       * on either side and then a centered blur of size d + 1.
       * (technique also from the SVG specification)
       */
      if (d % 2 == 1)
        {
          blur_xspan (row, tmp_buffer, buffer_width, d, 0);
          blur_xspan (row, tmp_buffer, buffer_width, d, 0);
          blur_xspan (row, tmp_buffer, buffer_width, d, 0);
        }
      else
        {
          blur_xspan (row, tmp_buffer, buffer_width, d, 1);
          blur_xspan (row, tmp_buffer, buffer_width, d, -1);
          blur_xspan (row, tmp_buffer, buffer_width, d + 1, 0);
        }
    }
}

/* Swaps width and height.
 */
static void
flip_buffer (guchar *dst_buffer,
             guchar *src_buffer,
             int     width,
             int     height)
{
  /* Working in blocks increases cache efficiency, compared to reading
   * or writing an entire column at once
   */
#define BLOCK_SIZE 16

  int i0, j0;

  for (i0 = 0; i0 < width; i0 += BLOCK_SIZE)
    for (j0 = 0; j0 < height; j0 += BLOCK_SIZE)
      {
        int max_j = MIN(j0 + BLOCK_SIZE, height);
        int max_i = MIN(i0 + BLOCK_SIZE, width);
        int i, j;

        for (i = i0; i < max_i; i++)
          for (j = j0; j < max_j; j++)
            dst_buffer[i * height + j] = src_buffer[j * width + i];
      }
#undef BLOCK_SIZE
}

static void
_boxblur (guchar  *buffer,
          int      width,
          int      height,
          int      radius)
{
  guchar *flipped_buffer;

  flipped_buffer = g_malloc (width * height);

  /* Step 1: swap rows and columns */
  flip_buffer (flipped_buffer, buffer, width, height);

  /* Step 2: blur rows (really columns) */
  blur_rows (flipped_buffer, buffer, height, width, radius);

  /* Step 3: swap rows and columns */
  flip_buffer (buffer, flipped_buffer, height, width);

  /* Step 4: blur rows */
  blur_rows (buffer, flipped_buffer, width, height, radius);

  g_free (flipped_buffer);
}

static const cairo_user_data_key_t original_cr_key;

static gboolean
gdk_cairo_get_clip_rectangle (cairo_t      *cr,
                              cairo_rectangle_int_t *rect)
{
  double x1, y1, x2, y2;
  gboolean clip_exists;

  cairo_clip_extents (cr, &x1, &y1, &x2, &y2);

  clip_exists = x1 < x2 && y1 < y2;

  if (rect)
  {
    x1 = floor (x1);
    y1 = floor (y1);
    x2 = ceil (x2);
    y2 = ceil (y2);

    rect->x = CLAMP (x1, G_MININT, G_MAXINT);
    rect->y = CLAMP (y1, G_MININT, G_MAXINT);
    rect->width = CLAMP (x2 - x1, G_MININT, G_MAXINT);
    rect->height = CLAMP (y2 - y1, G_MININT, G_MAXINT);
  }

  return clip_exists;
}

cairo_t *
gtk_css_shadow_value_start_drawing (cairo_t *cr, gdouble radius)
{
  cairo_rectangle_int_t clip_rect;
  cairo_surface_t *surface;
  cairo_t *blur_cr;
  gdouble clip_radius;

  gdk_cairo_get_clip_rectangle (cr, &clip_rect);

  clip_radius = _gtk_cairo_blur_compute_pixels (radius);

  /* Create a larger surface to center the blur. */
  surface = cairo_image_surface_create (CAIRO_FORMAT_A8,
                                        clip_rect.width + 2 * clip_radius,
                                        clip_rect.height + 2 * clip_radius);
  cairo_surface_set_device_offset (surface, clip_radius - clip_rect.x, clip_radius - clip_rect.y);
  blur_cr = cairo_create (surface);
  cairo_set_user_data (blur_cr, &original_cr_key, cairo_reference (cr), (cairo_destroy_func_t) cairo_destroy);

  if (cairo_has_current_point (cr))
    {
      double x, y;

      cairo_get_current_point (cr, &x, &y);
      cairo_move_to (blur_cr, x, y);
    }

  return blur_cr;
}

cairo_t *
gtk_css_shadow_value_finish_drawing (cairo_t *cr, gdouble radius, GdkColor *color)
{
  cairo_t *original_cr;
  cairo_surface_t *surface;

  original_cr = cairo_get_user_data (cr, &original_cr_key);

  /* Blur the surface. */
  surface = cairo_get_target (cr);

   _gtk_cairo_blur_surface (surface, radius);

  gdk_cairo_set_source_color(original_cr, color);
  cairo_mask_surface (original_cr, surface, 0, 0);

  cairo_destroy (cr);
  cairo_surface_destroy (surface);

  return original_cr;
}

/*
 * _gtk_cairo_blur_surface:
 * @surface: a cairo image surface.
 * @radius: the blur radius.
 *
 * Blurs the cairo image surface at the given radius.
 */
void
_gtk_cairo_blur_surface (cairo_surface_t* surface,
                         double           radius_d)
{
  cairo_format_t format;
  int radius = radius_d;

  g_return_if_fail (surface != NULL);
  g_return_if_fail (cairo_surface_get_type (surface) == CAIRO_SURFACE_TYPE_IMAGE);

  format = cairo_image_surface_get_format (surface);
  g_return_if_fail (format == CAIRO_FORMAT_A8);

  if (radius == 0)
    return;

  /* Before we mess with the surface, execute any pending drawing. */
  cairo_surface_flush (surface);

  _boxblur (cairo_image_surface_get_data (surface),
            cairo_image_surface_get_stride (surface),
            cairo_image_surface_get_height (surface),
            radius);

  /* Inform cairo we altered the surface contents. */
  cairo_surface_mark_dirty (surface);
}

/*
 * _gtk_cairo_blur_compute_pixels:
 * @radius: the radius to compute the pixels for
 *
 * Computes the number of pixels necessary to extend an image in one
 * direction to hold the image with shadow.
 *
 * This is just the number of pixels added by the blur radius, shadow
 * offset and spread are not included.
 *
 * Much of this, the 3 * sqrt(2 * pi) / 4, is the known value for
 * approximating a Gaussian using box blurs.  This yields quite a good
 * approximation for a Gaussian.  Then we multiply this by 1.5 since our
 * code wants the radius of the entire triple-box-blur kernel instead of
 * the diameter of an individual box blur.  For more details, see:
 * http://www.w3.org/TR/SVG11/filters.html#feGaussianBlurElement
 * https://bugzilla.mozilla.org/show_bug.cgi?id=590039#c19
 */
#define GAUSSIAN_SCALE_FACTOR ((3.0 * sqrt(2 * G_PI) / 4) * 1.5)

int
_gtk_cairo_blur_compute_pixels (double radius)
{
  return floor (radius * GAUSSIAN_SCALE_FACTOR + 0.5);
}
