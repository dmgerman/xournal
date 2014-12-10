/* xo-metadata.h
 *  this file is part of xournal, a gnome document viewer
 *
 * Copyright (C) 2009 Carlos Garcia Campos  <carlosgc@gnome.org>
 *
 * originally from evince adapted for xournal by Daniel M German
 *
 * Xournal is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xournal is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef XO_METADATA_H
#define XO_METADATA_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define XO_TYPE_METADATA         (xo_metadata_get_type())
#define XO_METADATA(object)      (G_TYPE_CHECK_INSTANCE_CAST((object), XO_TYPE_METADATA, XoMetadata))
#define XO_METADATA_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), XO_TYPE_METADATA, XoMetadataClass))
#define XO_IS_METADATA(object)   (G_TYPE_CHECK_INSTANCE_TYPE((object), XO_TYPE_METADATA))

typedef struct _XoMetadata      XoMetadata;
typedef struct _XoMetadataClass XoMetadataClass;

GType       xo_metadata_get_type              (void) G_GNUC_CONST;
XoMetadata *xo_metadata_new                   (GFile       *file);
gboolean    xo_metadata_is_empty              (XoMetadata  *metadata);

gboolean    xo_metadata_get_string            (XoMetadata  *metadata,
					       const gchar *key,
					       gchar     **value);
gboolean    xo_metadata_set_string            (XoMetadata  *metadata,
					       const gchar *key,
					       const gchar *value);
gboolean    xo_metadata_get_int               (XoMetadata  *metadata,
					       const gchar *key,
					       gint        *value);
gboolean    xo_metadata_set_int               (XoMetadata  *metadata,
					       const gchar *key,
					       gint         value);
gboolean    xo_metadata_get_double            (XoMetadata  *metadata,
					       const gchar *key,
					       gdouble     *value);
gboolean    xo_metadata_set_double            (XoMetadata  *metadata,
					       const gchar *key,
					       gdouble      value);
gboolean    xo_metadata_get_boolean           (XoMetadata  *metadata,
					       const gchar *key,
					       gboolean    *value);
gboolean    xo_metadata_set_boolean           (XoMetadata  *metadata,
					       const gchar *key,
					       gboolean     value);

gboolean    xo_is_metadata_supported_for_file (GFile       *file);

G_END_DECLS

#endif /* XO_METADATA_H */
