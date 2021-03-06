/*
 * Copyright (C) 2010 Rodney Cryderman <rcryderman@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by Rodney Cryderman <rcryderman@gmail.com>
 *
 */
/* awn-desktop-lookup-client.h */

#ifndef _AWN_DESKTOP_LOOKUP_CLIENT
#define _AWN_DESKTOP_LOOKUP_CLIENT

#include <glib-object.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AWN_TYPE_DESKTOP_LOOKUP_CLIENT awn_desktop_lookup_client_get_type()

#define AWN_DESKTOP_LOOKUP_CLIENT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), AWN_TYPE_DESKTOP_LOOKUP_CLIENT, AwnDesktopLookupClient))

#define AWN_DESKTOP_LOOKUP_CLIENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), AWN_TYPE_DESKTOP_LOOKUP_CLIENT, AwnDesktopLookupClientClass))

#define AWN_IS_DESKTOP_LOOKUP_CLIENT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), AWN_TYPE_DESKTOP_LOOKUP_CLIENT))

#define AWN_IS_DESKTOP_LOOKUP_CLIENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), AWN_TYPE_DESKTOP_LOOKUP_CLIENT))

#define AWN_DESKTOP_LOOKUP_CLIENT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), AWN_TYPE_DESKTOP_LOOKUP_CLIENT, AwnDesktopLookupClientClass))

typedef struct {
    GObject parent;
} AwnDesktopLookupClient;

typedef struct {
    GObjectClass parent_class;
} AwnDesktopLookupClientClass;

GType awn_desktop_lookup_client_get_type(void);

AwnDesktopLookupClient* awn_desktop_lookup_client_new(void);

#ifdef __cplusplus
} // extern "C"
#endif


#endif /* _AWN_DESKTOP_LOOKUP_CLIENT */

/* awn-desktop-lookup-client.c */

