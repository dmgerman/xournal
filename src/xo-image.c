/*
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This software is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of  
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <math.h>
#include <string.h>
#include <gtk/gtk.h>

#include "xournal.h"
#include "xo-support.h"
#include "xo-image.h"
#include "xo-misc.h"

// create pixbuf from buffer, or return NULL on failure
GdkPixbuf *pixbuf_from_buffer(const gchar *buf, gsize buflen)
{
  GdkPixbufLoader *loader;
  GdkPixbuf *pixbuf;

  loader = gdk_pixbuf_loader_new();
  gdk_pixbuf_loader_write(loader, buf, buflen, NULL);
  gdk_pixbuf_loader_close(loader, NULL);
  pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
  g_object_ref(pixbuf);
  g_object_unref(loader);
  return pixbuf;
}

void create_image_from_pixbuf(GdkPixbuf *pixbuf, double *pt)
{
  double scale;
  struct Item *item;

  item = g_new(struct Item, 1);
  item->type = ITEM_IMAGE;
  item->canvas_item = NULL;
  item->bbox.left = pt[0];
  item->bbox.top = pt[1];
  item->image = pixbuf;
  item->image_png = NULL;
  item->image_png_len = 0;

  // Scale at native size, unless that won't fit, in which case we shrink it down.
  scale = 1 / ui.zoom;
  if ((scale * gdk_pixbuf_get_width(item->image)) > ui.cur_page->width - item->bbox.left)
    scale = (ui.cur_page->width - item->bbox.left) / gdk_pixbuf_get_width(item->image);
  if ((scale * gdk_pixbuf_get_height(item->image)) > ui.cur_page->height - item->bbox.top)
    scale = (ui.cur_page->height - item->bbox.top) / gdk_pixbuf_get_height(item->image);

  item->bbox.right = item->bbox.left + scale * gdk_pixbuf_get_width(item->image);
  item->bbox.bottom = item->bbox.top + scale * gdk_pixbuf_get_height(item->image);
  ui.cur_layer->items = g_list_append(ui.cur_layer->items, item);
  ui.cur_layer->nitems++;
  
  make_canvas_item_one(ui.cur_layer->group, item);

  // add undo information
  prepare_new_undo();
  undo->type = ITEM_IMAGE;
  undo->item = item;
  undo->layer = ui.cur_layer;
  ui.cur_item = NULL;
  ui.cur_item_type = ITEM_NONE;

  // select image
  reset_selection();
  ui.selection = g_new0(struct Selection, 1);
  ui.selection->type = ITEM_SELECTRECT;
  ui.selection->layer = ui.cur_layer;
  ui.selection->bbox = item->bbox;
  ui.selection->items = g_list_append(ui.selection->items, item);
  ui.selection->canvas_item = gnome_canvas_item_new(ui.cur_layer->group,
      gnome_canvas_rect_get_type(), "width-pixels", 1,
      "outline-color-rgba", 0x000000ff,
      "fill-color-rgba", 0x80808040,
      "x1", ui.selection->bbox.left, "x2", ui.selection->bbox.right, 
      "y1", ui.selection->bbox.top, "y2", ui.selection->bbox.bottom, NULL);
  make_dashed(ui.selection->canvas_item);
  update_copy_paste_enabled();
}

void insert_image(GdkEvent *event)
{
  GtkTextBuffer *buffer;
  GnomeCanvasItem *canvas_item;
  GdkColor color;
  GtkWidget *dialog;
  GtkFileFilter *filt_all;
  GtkFileFilter *filt_gdkimage;
  char *filename;
  GdkPixbuf *pixbuf;
  double scale=1;
  double pt[2];
  
  dialog = gtk_file_chooser_dialog_new(_("Insert Image"), GTK_WINDOW (winMain),
     GTK_FILE_CHOOSER_ACTION_OPEN, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
     GTK_STOCK_OPEN, GTK_RESPONSE_OK, NULL);
#ifdef FILE_DIALOG_SIZE_BUGFIX
  gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 400);
#endif
     
  filt_all = gtk_file_filter_new();
  gtk_file_filter_set_name(filt_all, _("All files"));
  gtk_file_filter_add_pattern(filt_all, "*");
  filt_gdkimage = gtk_file_filter_new();
  gtk_file_filter_set_name(filt_gdkimage, _("Image files"));
  gtk_file_filter_add_pixbuf_formats(filt_gdkimage);
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER (dialog), filt_gdkimage);
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER (dialog), filt_all);

  if (ui.default_image != NULL) gtk_file_chooser_set_filename(GTK_FILE_CHOOSER (dialog), ui.default_image);

  if (wrapper_gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_OK) {
    gtk_widget_destroy(dialog);
    return;
  }
  filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER (dialog));
  gtk_widget_destroy(dialog);

  if (filename == NULL) return; /* nothing selected */

  if (ui.default_image != NULL) g_free(ui.default_image);
  ui.default_image = g_strdup(filename);
  
  set_cursor_busy(TRUE);
  pixbuf=gdk_pixbuf_new_from_file(filename, NULL);
  set_cursor_busy(FALSE);
  
  if(pixbuf==NULL) { /* open failed */
    dialog = gtk_message_dialog_new(GTK_WINDOW (winMain), GTK_DIALOG_MODAL,
      GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, _("Error opening image '%s'"), filename);
    wrapper_gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_free(filename);
    ui.cur_item = NULL;
    ui.cur_item_type = ITEM_NONE;
    return;
  }

  ui.cur_item_type = ITEM_IMAGE;

  get_pointer_coords(event, pt);
  set_current_page(pt);  

  create_image_from_pixbuf(pixbuf, pt);
}

void rescale_images(void)
{
  // nothing needed in this implementation
}

