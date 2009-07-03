/*
 * Copyright (C) 2009 Michal Hruby <michal.mhr@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Library General Public License version 
 * 2 or later as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by Michal Hruby <michal.mhr@gmail.com>
 *
 */

#ifndef _AWN_IMAGE_H_
#define _AWN_IMAGE_H_

#include <glib-object.h>
#include <gtk/gtk.h>

#include "awn-overlay-text.h"

G_BEGIN_DECLS

#define AWN_TYPE_IMAGE awn_image_get_type()

#define AWN_IMAGE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), AWN_TYPE_IMAGE, AwnImage))

#define AWN_IMAGE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), AWN_TYPE_IMAGE, AwnImageClass))

#define AWN_IS_IMAGE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), AWN_TYPE_IMAGE))

#define AWN_IS_IMAGE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), AWN_TYPE_IMAGE))

#define AWN_IMAGE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), AWN_TYPE_IMAGE, AwnImageClass))

typedef struct _AwnImagePrivate AwnImagePrivate;

typedef struct {
  GtkImage parent;

  AwnImagePrivate *priv;
} AwnImage;

typedef struct {
  GtkImageClass parent_class;
} AwnImageClass;

GType           awn_image_get_type              (void);

AwnImage*       awn_image_new                   (void);

AwnImage*       awn_image_new_with_label        (const gchar *label);

void            awn_image_set_label             (AwnImage *image,
                                                 const gchar *label);

AwnOverlayText* awn_image_get_overlay_text      (AwnImage *image);

G_END_DECLS

#endif /* _AWN_IMAGE_H_ */
