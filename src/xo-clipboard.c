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

#include <string.h>
#include <gtk/gtk.h>

#include "xournal.h"
#include "xo-callbacks.h"
#include "xo-interface.h"
#include "xo-support.h"
#include "xo-misc.h"
#include "xo-paint.h"
#include "xo-image.h"
#include "xo-selection.h"

// the various formats in which we might present clipboard data
#define TARGET_XOURNAL 1
#define TARGET_TEXT    2
#define TARGET_PIXBUF  3
#define XOURNAL_TARGET_ATOM "_XOURNAL" 
  /* change when serialized data format changes incompatibly */

typedef struct XojSelectionData {
  int xo_data_len;
  char *xo_data;
  gchar *text_data;
  GdkPixbuf *image_data;
} XojSelectionData;


void callback_clipboard_get(GtkClipboard *clipboard,
                            GtkSelectionData *selection_data,
                            guint info, gpointer user_data)
{
  struct XojSelectionData *sel = (struct XojSelectionData *)user_data;

  switch (info) {
    case TARGET_XOURNAL:
      gtk_selection_data_set(selection_data,
        gdk_atom_intern(XOURNAL_TARGET_ATOM, FALSE), 8, sel->xo_data, sel->xo_data_len);
      break;
    case TARGET_TEXT:
      if (sel->text_data!=NULL) 
        gtk_selection_data_set_text(selection_data, sel->text_data, -1);
      break;
    case TARGET_PIXBUF:
      if (sel->image_data!=NULL)
        gtk_selection_data_set_pixbuf(selection_data, sel->image_data);
      break;
  }
}

void callback_clipboard_clear(GtkClipboard *clipboard, gpointer user_data)
{
  struct XojSelectionData *sel = (struct XojSelectionData *)user_data;
  
  if (sel->xo_data!=NULL) g_free(sel->xo_data);
  if (sel->text_data!=NULL) g_free(sel->text_data);
  if (sel->image_data!=NULL) g_object_unref(sel->image_data);
  g_free(sel);
}

void selection_to_clip(void)
{
  struct XojSelectionData *sel;
  int bufsz, nitems, val;
  char *p;
  GList *list;
  struct Item *item;
  GtkTargetList *targetlist;
  GtkTargetEntry *targets;
  int n_targets;
  
  if (ui.selection == NULL) return;
  bufsz = 2*sizeof(int) // bufsz, nitems
        + sizeof(struct BBox); // bbox
  nitems = 0;
  for (list = ui.selection->items; list != NULL; list = list->next) {
    item = (struct Item *)list->data;
    nitems++;
    if (item->type == ITEM_STROKE) {
      bufsz+= sizeof(int) // type
            + sizeof(struct Brush) // brush
            + sizeof(int) // num_points
            + 2*item->path->num_points*sizeof(double); // the points
      if (item->brush.variable_width)
        bufsz += (item->path->num_points-1)*sizeof(double); // the widths
    }
    else if (item->type == ITEM_TEXT) {
      bufsz+= sizeof(int) // type
            + sizeof(struct Brush) // brush
            + 2*sizeof(double) // bbox upper-left
            + sizeof(int) // text len
            + strlen(item->text)+1 // text
            + sizeof(int) // font_name len
            + strlen(item->font_name)+1 // font_name
            + sizeof(double); // font_size
    }
    else if (item->type == ITEM_IMAGE) {
      if (item->image_png == NULL) {
        set_cursor_busy(TRUE);
        if (!gdk_pixbuf_save_to_buffer(item->image, &item->image_png, &item->image_png_len, "png", NULL, NULL))
          item->image_png_len = 0;       // failed for some reason, so forget it
        set_cursor_busy(FALSE);
      }
      bufsz+= sizeof(int) // type
        + sizeof(struct BBox)
        + sizeof(gsize) // png_buflen
        + item->image_png_len;
    }
    else bufsz+= sizeof(int); // type
  }

  // allocate selection data structure and buffer
  sel = g_malloc(sizeof(struct XojSelectionData));
  sel->xo_data_len = bufsz;
  sel->xo_data = g_malloc(bufsz);
  sel->image_data = NULL;
  sel->text_data = NULL;

  // fill in the data
  p = sel->xo_data;
  g_memmove(p, &bufsz, sizeof(int)); p+= sizeof(int);
  g_memmove(p, &nitems, sizeof(int)); p+= sizeof(int);
  g_memmove(p, &ui.selection->bbox, sizeof(struct BBox)); p+= sizeof(struct BBox);
  for (list = ui.selection->items; list != NULL; list = list->next) {
    item = (struct Item *)list->data;
    g_memmove(p, &item->type, sizeof(int)); p+= sizeof(int);
    if (item->type == ITEM_STROKE) {
      g_memmove(p, &item->brush, sizeof(struct Brush)); p+= sizeof(struct Brush);
      g_memmove(p, &item->path->num_points, sizeof(int)); p+= sizeof(int);
      g_memmove(p, item->path->coords, 2*item->path->num_points*sizeof(double));
      p+= 2*item->path->num_points*sizeof(double);
      if (item->brush.variable_width) {
        g_memmove(p, item->widths, (item->path->num_points-1)*sizeof(double));
        p+= (item->path->num_points-1)*sizeof(double);
      }
    }
    if (item->type == ITEM_TEXT) {
      g_memmove(p, &item->brush, sizeof(struct Brush)); p+= sizeof(struct Brush);
      g_memmove(p, &item->bbox.left, sizeof(double)); p+= sizeof(double);
      g_memmove(p, &item->bbox.top, sizeof(double)); p+= sizeof(double);
      val = strlen(item->text);
      g_memmove(p, &val, sizeof(int)); p+= sizeof(int);
      g_memmove(p, item->text, val+1); p+= val+1;
      val = strlen(item->font_name);
      g_memmove(p, &val, sizeof(int)); p+= sizeof(int);
      g_memmove(p, item->font_name, val+1); p+= val+1;
      g_memmove(p, &item->font_size, sizeof(double)); p+= sizeof(double);
      if (nitems==1) sel->text_data = g_strdup(item->text); // single text item
    }
    if (item->type == ITEM_IMAGE) {
      g_memmove(p, &item->bbox, sizeof(struct BBox)); p+= sizeof(struct BBox);
      g_memmove(p, &item->image_png_len, sizeof(gsize)); p+= sizeof(gsize);
      if (item->image_png_len > 0) {
        g_memmove(p, item->image_png, item->image_png_len); p+= item->image_png_len;
      }
      if (nitems==1) sel->image_data = gdk_pixbuf_copy(item->image); // single image
    }
  }
  
  /* build list of valid targets */
  targetlist = gtk_target_list_new(NULL, 0);
  gtk_target_list_add(targetlist, 
    gdk_atom_intern(XOURNAL_TARGET_ATOM, FALSE), 0, TARGET_XOURNAL);
  if (sel->image_data!=NULL)
    gtk_target_list_add_image_targets(targetlist, TARGET_PIXBUF, TRUE);
  if (sel->text_data!=NULL) 
    gtk_target_list_add_text_targets(targetlist, TARGET_TEXT);
  targets = gtk_target_table_new_from_list(targetlist, &n_targets);
  gtk_target_list_unref(targetlist);
  
  gtk_clipboard_set_with_data(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), 
       targets, n_targets,
       callback_clipboard_get, callback_clipboard_clear, sel);
  gtk_target_table_free(targets, n_targets);
}

// paste xournal native data
void clipboard_paste_from_xournal(GtkSelectionData *sel_data)
{
  unsigned char *p;
  int nitems, npts, i, len;
  struct Item *item;
  double hoffset, voffset, cx, cy;
  double *pf;
  int sx, sy, wx, wy;
  
  reset_selection();
  
  ui.selection = g_new(struct Selection, 1);
  p = sel_data->data + sizeof(int);
  g_memmove(&nitems, p, sizeof(int)); p+= sizeof(int);
  ui.selection->type = ITEM_SELECTRECT;
  ui.selection->layer = ui.cur_layer;
  g_memmove(&ui.selection->bbox, p, sizeof(struct BBox)); p+= sizeof(struct BBox);
  ui.selection->items = NULL;
  
  // find by how much we translate the pasted selection
  gnome_canvas_get_scroll_offsets(canvas, &sx, &sy);
  gdk_window_get_geometry(GTK_WIDGET(canvas)->window, NULL, NULL, &wx, &wy, NULL);
  gnome_canvas_window_to_world(canvas, sx + wx/2, sy + wy/2, &cx, &cy);
  cx -= ui.cur_page->hoffset;
  cy -= ui.cur_page->voffset;
  if (cx + (ui.selection->bbox.right-ui.selection->bbox.left)/2 > ui.cur_page->width)
    cx = ui.cur_page->width - (ui.selection->bbox.right-ui.selection->bbox.left)/2;
  if (cx - (ui.selection->bbox.right-ui.selection->bbox.left)/2 < 0)
    cx = (ui.selection->bbox.right-ui.selection->bbox.left)/2;
  if (cy + (ui.selection->bbox.bottom-ui.selection->bbox.top)/2 > ui.cur_page->height)
    cy = ui.cur_page->height - (ui.selection->bbox.bottom-ui.selection->bbox.top)/2;
  if (cy - (ui.selection->bbox.bottom-ui.selection->bbox.top)/2 < 0)
    cy = (ui.selection->bbox.bottom-ui.selection->bbox.top)/2;
  hoffset = cx - (ui.selection->bbox.right+ui.selection->bbox.left)/2;
  voffset = cy - (ui.selection->bbox.top+ui.selection->bbox.bottom)/2;
  ui.selection->bbox.left += hoffset;
  ui.selection->bbox.right += hoffset;
  ui.selection->bbox.top += voffset;
  ui.selection->bbox.bottom += voffset;

  ui.selection->canvas_item = gnome_canvas_item_new(ui.cur_layer->group,
      gnome_canvas_rect_get_type(), "width-pixels", 1,
      "outline-color-rgba", 0x000000ff,
      "fill-color-rgba", 0x80808040,
      "x1", ui.selection->bbox.left, "x2", ui.selection->bbox.right, 
      "y1", ui.selection->bbox.top, "y2", ui.selection->bbox.bottom, NULL);
  make_dashed(ui.selection->canvas_item);

  while (nitems-- > 0) {
    item = g_new(struct Item, 1);
    ui.selection->items = g_list_append(ui.selection->items, item);
    ui.cur_layer->items = g_list_append(ui.cur_layer->items, item);
    ui.cur_layer->nitems++;
    g_memmove(&item->type, p, sizeof(int)); p+= sizeof(int);
    if (item->type == ITEM_STROKE) {
      g_memmove(&item->brush, p, sizeof(struct Brush)); p+= sizeof(struct Brush);
      g_memmove(&npts, p, sizeof(int)); p+= sizeof(int);
      item->path = gnome_canvas_points_new(npts);
      pf = (double *)p;
      for (i=0; i<npts; i++) {
        item->path->coords[2*i] = pf[2*i] + hoffset;
        item->path->coords[2*i+1] = pf[2*i+1] + voffset;
      }
      p+= 2*item->path->num_points*sizeof(double);
      if (item->brush.variable_width) {
        item->widths = g_memdup(p, (item->path->num_points-1)*sizeof(double));
        p+= (item->path->num_points-1)*sizeof(double);
      }
      else item->widths = NULL;
      update_item_bbox(item);
      make_canvas_item_one(ui.cur_layer->group, item);
    }
    if (item->type == ITEM_TEXT) {
      g_memmove(&item->brush, p, sizeof(struct Brush)); p+= sizeof(struct Brush);
      g_memmove(&item->bbox.left, p, sizeof(double)); p+= sizeof(double);
      g_memmove(&item->bbox.top, p, sizeof(double)); p+= sizeof(double);
      item->bbox.left += hoffset;
      item->bbox.top += voffset;
      g_memmove(&len, p, sizeof(int)); p+= sizeof(int);
      item->text = g_malloc(len+1);
      g_memmove(item->text, p, len+1); p+= len+1;
      g_memmove(&len, p, sizeof(int)); p+= sizeof(int);
      item->font_name = g_malloc(len+1);
      g_memmove(item->font_name, p, len+1); p+= len+1;
      g_memmove(&item->font_size, p, sizeof(double)); p+= sizeof(double);
      make_canvas_item_one(ui.cur_layer->group, item);
    }
    if (item->type == ITEM_IMAGE) {
      item->canvas_item = NULL;
      item->image_png = NULL;
      item->image_png_len = 0;
      g_memmove(&item->bbox, p, sizeof(struct BBox)); p+= sizeof(struct BBox);
      item->bbox.left += hoffset;
      item->bbox.right += hoffset;
      item->bbox.top += voffset;
      item->bbox.bottom += voffset;
      g_memmove(&item->image_png_len, p, sizeof(gsize)); p+= sizeof(gsize);
      if (item->image_png_len > 0) {
        item->image_png = g_memdup(p, item->image_png_len);
        item->image = pixbuf_from_buffer(item->image_png, item->image_png_len);
        p+= item->image_png_len;
      } else {
        item->image = NULL;
      }
      make_canvas_item_one(ui.cur_layer->group, item);
    }
  }

  prepare_new_undo();
  undo->type = ITEM_PASTE;
  undo->layer = ui.cur_layer;
  undo->itemlist = g_list_copy(ui.selection->items);  
  
  gtk_selection_data_free(sel_data);
  update_copy_paste_enabled();
  update_color_menu();
  update_thickness_buttons();
  update_color_buttons();
  update_font_button();  
  update_cursor(); // FIXME: can't know if pointer is within selection!
}

// paste external text
void clipboard_paste_text(gchar *text)
{
  struct Item *item;
  double pt[2];

  reset_selection();
  get_current_pointer_coords(pt);
  set_current_page(pt);  

  ui.selection = g_new(struct Selection, 1);
  ui.selection->type = ITEM_SELECTRECT;
  ui.selection->layer = ui.cur_layer;
  ui.selection->items = NULL;

  item = g_new(struct Item, 1);
  ui.selection->items = g_list_append(ui.selection->items, item);
  ui.cur_layer->items = g_list_append(ui.cur_layer->items, item);
  ui.cur_layer->nitems++;
  item->type = ITEM_TEXT;
  g_memmove(&(item->brush), &(ui.brushes[ui.cur_mapping][TOOL_PEN]), sizeof(struct Brush));
  item->text = text; // text was newly allocated, we keep it
  item->font_name = g_strdup(ui.font_name);
  item->font_size = ui.font_size;
  item->bbox.left = pt[0]; item->bbox.top = pt[1];
  make_canvas_item_one(ui.cur_layer->group, item);
  update_item_bbox(item);

  // move the text to fit on the page if needed
  if (item->bbox.right > ui.cur_page->width) item->bbox.left += ui.cur_page->width-item->bbox.right;
  if (item->bbox.left < 0) item->bbox.left = 0;
  if (item->bbox.bottom > ui.cur_page->height) item->bbox.top += ui.cur_page->height-item->bbox.bottom;
  if (item->bbox.top < 0) item->bbox.top = 0;
  gnome_canvas_item_set(item->canvas_item, "x", item->bbox.left, "y", item->bbox.top, NULL);
  update_item_bbox(item);
  
  ui.selection->bbox = item->bbox;
  ui.selection->canvas_item = gnome_canvas_item_new(ui.cur_layer->group,
      gnome_canvas_rect_get_type(), "width-pixels", 1,
      "outline-color-rgba", 0x000000ff,
      "fill-color-rgba", 0x80808040,
      "x1", ui.selection->bbox.left, "x2", ui.selection->bbox.right, 
      "y1", ui.selection->bbox.top, "y2", ui.selection->bbox.bottom, NULL);
  make_dashed(ui.selection->canvas_item);

  prepare_new_undo();
  undo->type = ITEM_PASTE;
  undo->layer = ui.cur_layer;
  undo->itemlist = g_list_copy(ui.selection->items);  
  
  update_copy_paste_enabled();
  update_color_menu();
  update_thickness_buttons();
  update_color_buttons();
  update_font_button();  
  update_cursor(); // FIXME: can't know if pointer is within selection!
}

// paste an external image
void clipboard_paste_image(GdkPixbuf *pixbuf)
{
  double pt[2];

  reset_selection();

  get_current_pointer_coords(pt);
  set_current_page(pt);  

  create_image_from_pixbuf(pixbuf, pt);
}

// work out what format the clipboard data is in, and paste accordingly
void clipboard_paste(void)
{
  GtkSelectionData *sel_data;
  GtkClipboard *clipboard;
  GdkPixbuf *pixbuf;
  gchar *text;

  if (ui.cur_layer == NULL) return;
  
  ui.cur_item_type = ITEM_PASTE;
  clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
  // try xournal data
  sel_data = gtk_clipboard_wait_for_contents(
      clipboard,
      gdk_atom_intern(XOURNAL_TARGET_ATOM, FALSE));
  // avoid a bug (mostly win32, also xclip?) presenting images as xournal data
  if (gtk_selection_data_get_data_type(sel_data)!=gdk_atom_intern(XOURNAL_TARGET_ATOM, FALSE))
    { gtk_selection_data_free(sel_data); sel_data = NULL; }
  ui.cur_item_type = ITEM_NONE;
  if (sel_data != NULL) { 
    clipboard_paste_from_xournal(sel_data);
    return;
  } 
  // try image data
  pixbuf = gtk_clipboard_wait_for_image(clipboard);
  if (pixbuf != NULL) {
    clipboard_paste_image(pixbuf);
    return;
  }
  // try text data
  text = gtk_clipboard_wait_for_text(clipboard);
  if (text != NULL) {
    clipboard_paste_text(text);
    return;
  }
}
