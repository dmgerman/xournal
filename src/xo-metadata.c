/* xo-metadata.c
 *  this file is part of evince, a gnome document viewer
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

#include <gio/gio.h>
#include <string.h>

#include "xo-metadata.h"

struct _XoMetadata {
	GObject base;

	GFile      *file;
	GHashTable *items;
};

struct _XoMetadataClass {
	GObjectClass base_class;
};

G_DEFINE_TYPE (XoMetadata, xo_metadata, G_TYPE_OBJECT)

#define XO_METADATA_NAMESPACE "metadata::xournal"

static void
xo_metadata_finalize (GObject *object)
{
	XoMetadata *metadata = XO_METADATA (object);

	if (metadata->items) {
		g_hash_table_destroy (metadata->items);
		metadata->items = NULL;
	}

	if (metadata->file) {
		g_object_unref (metadata->file);
		metadata->file = NULL;
	}

	G_OBJECT_CLASS (xo_metadata_parent_class)->finalize (object);
}

static void
xo_metadata_init (XoMetadata *metadata)
{
	metadata->items = g_hash_table_new_full (g_str_hash,
						 g_str_equal,
						 g_free,
						 g_free);
}

static void
xo_metadata_class_init (XoMetadataClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

	gobject_class->finalize = xo_metadata_finalize;
}

static void
xo_metadata_load (XoMetadata *metadata)
{
	GFileInfo *info;
	gchar    **attrs;
	gint       i;
	GError    *error = NULL;

	info = g_file_query_info (metadata->file, "metadata::*", 0, NULL, &error);
	if (!info) {
		g_warning ("%s", error->message);
		g_error_free (error);

		return;
	}

	if (!g_file_info_has_namespace (info, "metadata")) {
		g_object_unref (info);

		return;
	}

	attrs = g_file_info_list_attributes (info, "metadata");
	for (i = 0; attrs[i]; i++) {
		GFileAttributeType type;
		gpointer           value;
		const gchar       *key;

		if (!g_str_has_prefix (attrs[i], XO_METADATA_NAMESPACE))
			continue;

		if (!g_file_info_get_attribute_data (info, attrs[i],
						     &type, &value, NULL)) {
			continue;
		}

		key = attrs[i] + strlen (XO_METADATA_NAMESPACE"::");

		if (type == G_FILE_ATTRIBUTE_TYPE_STRING) {
			g_hash_table_insert (metadata->items,
					     g_strdup (key),
					     g_strdup (value));
		}
	}
	g_strfreev (attrs);
	g_object_unref (info);
}

XoMetadata *
xo_metadata_new (GFile *file)
{
	XoMetadata *metadata;

	g_return_val_if_fail (G_IS_FILE (file), NULL);

	metadata = XO_METADATA (g_object_new (XO_TYPE_METADATA, NULL));
	metadata->file = g_object_ref (file);

	xo_metadata_load (metadata);

	return metadata;
}

gboolean
xo_metadata_is_empty (XoMetadata *metadata)
{
	return g_hash_table_size (metadata->items) == 0;
}

gboolean
xo_metadata_get_string (XoMetadata  *metadata,
			const gchar *key,
			gchar     **value)
{
	gchar *v;

	v = g_hash_table_lookup (metadata->items, key);
	if (!v)
		return FALSE;

	*value = v;
	return TRUE;
}

static void
metadata_set_callback (GObject      *file,
		       GAsyncResult *result,
		       XoMetadata   *metadata)
{
	GError *error = NULL;

	if (!g_file_set_attributes_finish (G_FILE (file), result, NULL, &error)) {
		g_warning ("%s", error->message);
		g_error_free (error);
	}
}

gboolean
xo_metadata_set_string (XoMetadata  *metadata,
			const gchar *key,
			const gchar *value)
{
	GFileInfo *info;
	gchar     *gio_key;

	info = g_file_info_new ();

	gio_key = g_strconcat (XO_METADATA_NAMESPACE"::", key, NULL);
	if (value) {
		g_file_info_set_attribute_string (info, gio_key, value);
	} else {
		g_file_info_set_attribute (info, gio_key,
					   G_FILE_ATTRIBUTE_TYPE_INVALID,
					   NULL);
	}
	g_free (gio_key);

	g_hash_table_insert (metadata->items, g_strdup (key), g_strdup (value));
	g_file_set_attributes_async (metadata->file,
				     info,
				     0,
				     G_PRIORITY_DEFAULT,
				     NULL,
				     (GAsyncReadyCallback)metadata_set_callback,
				     metadata);
	g_object_unref (info);

	return TRUE;
}

gboolean
xo_metadata_get_int (XoMetadata  *metadata,
		     const gchar *key,
		     gint        *value)
{
	gchar *string_value;
	gchar *endptr;
	gint   int_value;

	if (!xo_metadata_get_string (metadata, key, &string_value))
		return FALSE;

	int_value = g_ascii_strtoull (string_value, &endptr, 0);
	if (int_value == 0 && string_value == endptr)
		return FALSE;

	*value = int_value;
	return TRUE;
}

gboolean
xo_metadata_set_int (XoMetadata  *metadata,
		     const gchar *key,
		     gint         value)
{
	gchar string_value[32];

	g_snprintf (string_value, sizeof (string_value), "%d", value);

	return xo_metadata_set_string (metadata, key, string_value);
}

gboolean
xo_metadata_get_double (XoMetadata  *metadata,
			const gchar *key,
			gdouble     *value)
{
	gchar  *string_value;
	gchar  *endptr;
	gdouble double_value;

	if (!xo_metadata_get_string (metadata, key, &string_value))
		return FALSE;

	double_value = g_ascii_strtod (string_value, &endptr);
	if (double_value == 0. && string_value == endptr)
		return FALSE;

	*value = double_value;
	return TRUE;
}

gboolean
xo_metadata_set_double (XoMetadata  *metadata,
			const gchar *key,
			gdouble      value)
{
	gchar string_value[G_ASCII_DTOSTR_BUF_SIZE];

	g_ascii_dtostr (string_value, G_ASCII_DTOSTR_BUF_SIZE, value);

	return xo_metadata_set_string (metadata, key, string_value);
}

gboolean
xo_metadata_get_boolean (XoMetadata  *metadata,
			 const gchar *key,
			 gboolean    *value)
{
	gint int_value;

	if (!xo_metadata_get_int (metadata, key, &int_value))
		return FALSE;

	*value = int_value;
	return TRUE;
}

gboolean
xo_metadata_set_boolean (XoMetadata  *metadata,
			 const gchar *key,
			 gboolean     value)
{
	return xo_metadata_set_string (metadata, key, value ? "1" : "0");
}

gboolean
xo_is_metadata_supported_for_file (GFile *file)
{
	GFileAttributeInfoList *namespaces;
	gint i;
	gboolean retval = FALSE;

	namespaces = g_file_query_writable_namespaces (file, NULL, NULL);
	if (!namespaces)
		return retval;

	for (i = 0; i < namespaces->n_infos; i++) {
		if (strcmp (namespaces->infos[i].name, "metadata") == 0) {
			retval = TRUE;
			break;
		}
	}

	g_file_attribute_info_list_unref (namespaces);

	return retval;
}
