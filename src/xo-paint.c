#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <math.h>
#include <string.h>
#include <gtk/gtk.h>
#include <libgnomecanvas/libgnomecanvas.h>

#include <libart_lgpl/art_vpath_dash.h>

#include "xournal.h"
#include "xo-callbacks.h"
#include "xo-interface.h"
#include "xo-support.h"
#include "xo-misc.h"
#include "xo-paint.h"

/************** drawing nice cursors *********/

static char cursor_pen_bits[] = {
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static char cursor_eraser_bits[] = {
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0x0f, 0x08, 0x08, 0x08, 0x08,
   0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0xf8, 0x0f,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static char cursor_eraser_mask[] = {
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0x0f, 0xf8, 0x0f, 0xf8, 0x0f,
   0xf8, 0x0f, 0xf8, 0x0f, 0xf8, 0x0f, 0xf8, 0x0f, 0xf8, 0x0f, 0xf8, 0x0f,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

void set_cursor_busy(gboolean busy)
{
  GdkCursor *cursor;
  
  if (busy) {
    cursor = gdk_cursor_new(GDK_WATCH);
    gdk_window_set_cursor(GTK_WIDGET(winMain)->window, cursor);
    gdk_window_set_cursor(GTK_WIDGET(canvas)->window, cursor);
    gdk_cursor_unref(cursor);
  }
  else {
    gdk_window_set_cursor(GTK_WIDGET(winMain)->window, NULL);
    update_cursor();
  }
  gdk_display_sync(gdk_display_get_default());
}

void update_cursor(void)
{
  GdkPixmap *source, *mask;
  GdkColor fg = {0, 0, 0, 0}, bg = {0, 65535, 65535, 65535};

  if (GTK_WIDGET(canvas)->window == NULL) return;
  
  if (ui.cursor!=NULL) { 
    gdk_cursor_unref(ui.cursor);
    ui.cursor = NULL;
  }
  if (ui.cur_item_type == ITEM_MOVESEL_VERT)
    ui.cursor = gdk_cursor_new(GDK_SB_V_DOUBLE_ARROW);
  else if (ui.cur_item_type == ITEM_MOVESEL)
    ui.cursor = gdk_cursor_new(GDK_FLEUR);
  else if (ui.toolno[ui.cur_mapping] == TOOL_PEN) {
    fg.red = (ui.cur_brush->color_rgba >> 16) & 0xff00;
    fg.green = (ui.cur_brush->color_rgba >> 8) & 0xff00;
    fg.blue = (ui.cur_brush->color_rgba >> 0) & 0xff00;
    source = gdk_bitmap_create_from_data(NULL, cursor_pen_bits, 16, 16);
    ui.cursor = gdk_cursor_new_from_pixmap(source, source, &fg, &bg, 7, 7);
    gdk_bitmap_unref(source);
  }
  else if (ui.toolno[ui.cur_mapping] == TOOL_ERASER) {
    source = gdk_bitmap_create_from_data(NULL, cursor_eraser_bits, 16, 16);
    mask = gdk_bitmap_create_from_data(NULL, cursor_eraser_mask, 16, 16);
    ui.cursor = gdk_cursor_new_from_pixmap(source, mask, &fg, &bg, 7, 7);
    gdk_bitmap_unref(source);
    gdk_bitmap_unref(mask);
  }
  else if (ui.toolno[ui.cur_mapping] == TOOL_HIGHLIGHTER) {
    source = gdk_bitmap_create_from_data(NULL, cursor_eraser_bits, 16, 16);
    mask = gdk_bitmap_create_from_data(NULL, cursor_eraser_mask, 16, 16);
    bg.red = (ui.cur_brush->color_rgba >> 16) & 0xff00;
    bg.green = (ui.cur_brush->color_rgba >> 8) & 0xff00;
    bg.blue = (ui.cur_brush->color_rgba >> 0) & 0xff00;
    ui.cursor = gdk_cursor_new_from_pixmap(source, mask, &fg, &bg, 7, 7);
    gdk_bitmap_unref(source);
    gdk_bitmap_unref(mask);
  }
  else if (ui.cur_item_type == ITEM_SELECTRECT) {
    ui.cursor = gdk_cursor_new(GDK_TCROSS);
  }
  else if (ui.toolno[ui.cur_mapping] == TOOL_HAND) {
    ui.cursor = gdk_cursor_new(GDK_HAND1);
  }
  
  gdk_window_set_cursor(GTK_WIDGET(canvas)->window, ui.cursor);
}


/************** painting strokes *************/

#define SUBDIVIDE_MAXDIST 5.0

void subdivide_cur_path(null)
{
  int n, pieces, k;
  double *p;
  double x0, y0, x1, y1;

  for (n=0, p=ui.cur_path.coords; n<ui.cur_path.num_points-1; n++, p+=2) {
    pieces = (int)floor(hypot(p[2]-p[0], p[3]-p[1])/SUBDIVIDE_MAXDIST);
    if (pieces>1) {
      x0 = p[0]; y0 = p[1];
      x1 = p[2]; y1 = p[3];
      realloc_cur_path(ui.cur_path.num_points+pieces-1);
      g_memmove(ui.cur_path.coords+2*(n+pieces), ui.cur_path.coords+2*(n+1),
                    2*(ui.cur_path.num_points-n-1)*sizeof(double));
      p = ui.cur_path.coords+2*n;
      ui.cur_path.num_points += pieces-1;
      n += (pieces-1);
      for (k=1; k<pieces; k++) {
        p+=2;
        p[0] = x0 + k*(x1-x0)/pieces;
        p[1] = y0 + k*(y1-y0)/pieces;
      } 
    }
  }
}

void create_new_stroke(GdkEvent *event)
{
  ui.cur_item_type = ITEM_STROKE;
  ui.cur_item = g_new(struct Item, 1);
  ui.cur_item->type = ITEM_STROKE;
  g_memmove(&(ui.cur_item->brush), ui.cur_brush, sizeof(struct Brush));
  ui.cur_item->path = &ui.cur_path;
  realloc_cur_path(2);
  ui.cur_path.num_points = 1;
  get_pointer_coords(event, ui.cur_path.coords);
  
  if (ui.ruler[ui.cur_mapping]) 
    ui.cur_item->canvas_item = gnome_canvas_item_new(ui.cur_layer->group,
      gnome_canvas_line_get_type(),
      "cap-style", GDK_CAP_ROUND, "join-style", GDK_JOIN_ROUND,
      "fill-color-rgba", ui.cur_item->brush.color_rgba,
      "width-units", ui.cur_item->brush.thickness, NULL);
  else
    ui.cur_item->canvas_item = gnome_canvas_item_new(
      ui.cur_layer->group, gnome_canvas_group_get_type(), NULL);
}

void continue_stroke(GdkEvent *event)
{
  GnomeCanvasPoints seg;
  double *pt;

  if (ui.ruler[ui.cur_mapping]) {
    pt = ui.cur_path.coords;
  } else {
    realloc_cur_path(ui.cur_path.num_points+1);
    pt = ui.cur_path.coords + 2*(ui.cur_path.num_points-1);
  } 
  
  get_pointer_coords(event, pt+2);
  
  if (ui.ruler[ui.cur_mapping])
    ui.cur_path.num_points = 2;
  else {
    if (hypot(pt[0]-pt[2], pt[1]-pt[3]) < PIXEL_MOTION_THRESHOLD/ui.zoom)
      return;  // not a meaningful motion
    ui.cur_path.num_points++;
  }

  seg.coords = pt; 
  seg.num_points = 2;
  seg.ref_count = 1;
  
  /* note: we're using a piece of the cur_path array. This is ok because
     upon creation the line just copies the contents of the GnomeCanvasPoints
     into an internal structure */

  if (ui.ruler[ui.cur_mapping])
    gnome_canvas_item_set(ui.cur_item->canvas_item, "points", &seg, NULL);
  else
    gnome_canvas_item_new((GnomeCanvasGroup *)ui.cur_item->canvas_item,
       gnome_canvas_line_get_type(), "points", &seg,
       "cap-style", GDK_CAP_ROUND, "join-style", GDK_JOIN_ROUND,
       "fill-color-rgba", ui.cur_item->brush.color_rgba,
       "width-units", ui.cur_item->brush.thickness, NULL);
}

void finalize_stroke(void)
{
  if (ui.cur_path.num_points == 1) { // GnomeCanvas doesn't like num_points=1
    ui.cur_path.coords[2] = ui.cur_path.coords[0]+0.1;
    ui.cur_path.coords[3] = ui.cur_path.coords[1];
    ui.cur_path.num_points = 2;
  }
  
  subdivide_cur_path(); // split the segment so eraser will work

  ui.cur_item->path = gnome_canvas_points_new(ui.cur_path.num_points);
  g_memmove(ui.cur_item->path->coords, ui.cur_path.coords, 
      2*ui.cur_path.num_points*sizeof(double));
  update_item_bbox(ui.cur_item);
  ui.cur_path.num_points = 0;

  // destroy the entire group of temporary line segments
  gtk_object_destroy(GTK_OBJECT(ui.cur_item->canvas_item));
  // make a new line item to replace it
  ui.cur_item->canvas_item = gnome_canvas_item_new(ui.cur_layer->group, 
     gnome_canvas_line_get_type(), "points", ui.cur_item->path,
     "cap-style", GDK_CAP_ROUND, "join-style", GDK_JOIN_ROUND,
     "fill-color-rgba", ui.cur_item->brush.color_rgba,
     "width-units", ui.cur_item->brush.thickness, NULL);

  // add undo information
  prepare_new_undo();
  undo->type = ITEM_STROKE;
  undo->item = ui.cur_item;
  undo->layer = ui.cur_layer;

  // store the item on top of the layer stack
  ui.cur_layer->items = g_list_append(ui.cur_layer->items, ui.cur_item);
  ui.cur_layer->nitems++;
  ui.cur_item = NULL;
  ui.cur_item_type = ITEM_NONE;
}

/************** eraser tool *************/

void erase_stroke_portions(struct Item *item, double x, double y, double radius,
                   gboolean whole_strokes, struct UndoErasureData *erasure)
{
  int i;
  double *pt;
  struct Item *newhead, *newtail;
  gboolean need_recalc = FALSE;

  for (i=0, pt=item->path->coords; i<item->path->num_points; i++, pt+=2) {
    if (hypot(pt[0]-x, pt[1]-y) <= radius) { // found an intersection
      // FIXME: need to test if line SEGMENT hits the circle
      // hide the canvas item, and create erasure data if needed
      if (erasure == NULL) {
        item->type = ITEM_TEMP_STROKE;
        gnome_canvas_item_hide(item->canvas_item);  
            /*  we'll use this hidden item as an insertion point later */
        erasure = (struct UndoErasureData *)g_malloc(sizeof(struct UndoErasureData));
        item->erasure = erasure;
        erasure->item = item;
        erasure->npos = g_list_index(ui.cur_layer->items, item);
        erasure->nrepl = 0;
        erasure->replacement_items = NULL;
      }
      // split the stroke
      newhead = newtail = NULL;
      if (!whole_strokes) {
        if (i>=2) {
          newhead = (struct Item *)g_malloc(sizeof(struct Item));
          newhead->type = ITEM_STROKE;
          g_memmove(&newhead->brush, &item->brush, sizeof(struct Brush));
          newhead->path = gnome_canvas_points_new(i);
          g_memmove(newhead->path->coords, item->path->coords, 2*i*sizeof(double));
        }
        while (++i < item->path->num_points) {
          pt+=2;
          if (hypot(pt[0]-x, pt[1]-y) > radius) break;
        }
        if (i<item->path->num_points-1) {
          newtail = (struct Item *)g_malloc(sizeof(struct Item));
          newtail->type = ITEM_STROKE;
          g_memmove(&newtail->brush, &item->brush, sizeof(struct Brush));
          newtail->path = gnome_canvas_points_new(item->path->num_points-i);
          g_memmove(newtail->path->coords, item->path->coords+2*i, 
                           2*(item->path->num_points-i)*sizeof(double));
          newtail->canvas_item = NULL;
        }
      }
      if (item->type == ITEM_STROKE) { 
        // it's inside an erasure list - we destroy it
        gnome_canvas_points_free(item->path);
        if (item->canvas_item != NULL) 
          gtk_object_destroy(GTK_OBJECT(item->canvas_item));
        erasure->nrepl--;
        erasure->replacement_items = g_list_remove(erasure->replacement_items, item);
        g_free(item);
      }
      // add the new head
      if (newhead != NULL) {
        update_item_bbox(newhead);
        newhead->canvas_item = gnome_canvas_item_new(ui.cur_layer->group,
            gnome_canvas_line_get_type(), "points", newhead->path,
            "cap-style", GDK_CAP_ROUND, "join-style", GDK_JOIN_ROUND,
            "fill-color-rgba", newhead->brush.color_rgba,
            "width-units", newhead->brush.thickness, NULL);
        lower_canvas_item_to(ui.cur_layer->group,
                  newhead->canvas_item, erasure->item->canvas_item);
        erasure->replacement_items = g_list_prepend(erasure->replacement_items, newhead);
        erasure->nrepl++;
        // prepending ensures it won't get processed twice
      }
      // recurse into the new tail
      need_recalc = (newtail!=NULL);
      if (newtail == NULL) break;
      item = newtail;
      erasure->replacement_items = g_list_prepend(erasure->replacement_items, newtail);
      erasure->nrepl++;
      i=0; pt=item->path->coords;
    }
  }
  // add the tail if needed
  if (!need_recalc) return;
  update_item_bbox(item);
  item->canvas_item = gnome_canvas_item_new(ui.cur_layer->group,
       gnome_canvas_line_get_type(), "points", item->path,
       "cap-style", GDK_CAP_ROUND, "join-style", GDK_JOIN_ROUND,
       "fill-color-rgba", item->brush.color_rgba,
       "width-units", item->brush.thickness, NULL);
  lower_canvas_item_to(ui.cur_layer->group, item->canvas_item, 
                                      erasure->item->canvas_item);
}


void do_eraser(GdkEvent *event, double radius, gboolean whole_strokes)
{
  struct Item *item, *repl;
  GList *itemlist, *repllist;
  double pos[2];
  struct BBox eraserbox;
  
  get_pointer_coords(event, pos);
  eraserbox.left = pos[0]-radius;
  eraserbox.right = pos[0]+radius;
  eraserbox.top = pos[1]-radius;
  eraserbox.bottom = pos[1]+radius;
  for (itemlist = ui.cur_layer->items; itemlist!=NULL; itemlist = itemlist->next) {
    item = (struct Item *)itemlist->data;
    if (item->type == ITEM_STROKE) {
      if (!have_intersect(&(item->bbox), &eraserbox)) continue;
      erase_stroke_portions(item, pos[0], pos[1], radius, whole_strokes, NULL);
    } else if (item->type == ITEM_TEMP_STROKE) {
      repllist = item->erasure->replacement_items;
      while (repllist!=NULL) {
        repl = (struct Item *)repllist->data;
          // we may delete the item soon! so advance now in the list
        repllist = repllist->next; 
        if (have_intersect(&(repl->bbox), &eraserbox))
          erase_stroke_portions(repl, pos[0], pos[1], radius, whole_strokes, item->erasure);
      }
    }
  }
}

void finalize_erasure(void)
{
  GList *itemlist, *partlist;
  struct Item *item, *part;
  
  prepare_new_undo();
  undo->type = ITEM_ERASURE;
  undo->layer = ui.cur_layer;
  undo->erasurelist = NULL;
  
  itemlist = ui.cur_layer->items;
  while (itemlist!=NULL) {
    item = (struct Item *)itemlist->data;
    itemlist = itemlist->next;
    if (item->type != ITEM_TEMP_STROKE) continue;
    item->type = ITEM_STROKE;
    ui.cur_layer->items = g_list_remove(ui.cur_layer->items, item);
    // the item has an invisible canvas item, which used to act as anchor
    if (item->canvas_item!=NULL) {
      gtk_object_destroy(GTK_OBJECT(item->canvas_item));
      item->canvas_item = NULL;
    }
    undo->erasurelist = g_list_append(undo->erasurelist, item->erasure);
    // add the new strokes into the current layer
    for (partlist = item->erasure->replacement_items; partlist!=NULL; partlist = partlist->next)
      ui.cur_layer->items = g_list_insert_before(
                      ui.cur_layer->items, itemlist, partlist->data);
    ui.cur_layer->nitems += item->erasure->nrepl-1;
  }
    
  ui.cur_item = NULL;
  ui.cur_item_type = ITEM_NONE;
  
  /* NOTE: the list of erasures goes in the depth order of the layer;
     this guarantees that, upon undo, the erasure->npos fields give the
     correct position where each item should be reinserted as the list
     is traversed in the forward direction */
}

/************ selection tools ***********/

void make_dashed(GnomeCanvasItem *item)
{
  double dashlen[2];
  ArtVpathDash dash;
  
  dash.n_dash = 2;
  dash.offset = 3.0;
  dash.dash = dashlen;
  dashlen[0] = dashlen[1] = 6.0;
  gnome_canvas_item_set(item, "dash", &dash, NULL);
}


void start_selectrect(GdkEvent *event)
{
  double pt[2];
  reset_selection();
  
  ui.cur_item_type = ITEM_SELECTRECT;
  ui.selection = g_new(struct Selection, 1);
  ui.selection->type = ITEM_SELECTRECT;
  ui.selection->items = NULL;
  ui.selection->layer = ui.cur_layer;

  get_pointer_coords(event, pt);
  ui.selection->bbox.left = ui.selection->bbox.right = pt[0];
  ui.selection->bbox.top = ui.selection->bbox.bottom = pt[1];
 
  ui.selection->canvas_item = gnome_canvas_item_new(ui.cur_layer->group,
      gnome_canvas_rect_get_type(), "width-pixels", 1, 
      "outline-color-rgba", 0x000000ff,
      "fill-color-rgba", 0x80808040,
      "x1", pt[0], "x2", pt[0], "y1", pt[1], "y2", pt[1], NULL);
  update_cursor();
}

void finalize_selectrect(void)
{
  double x1, x2, y1, y2;
  GList *itemlist;
  struct Item *item;

  
  ui.cur_item_type = ITEM_NONE;

  if (ui.selection->bbox.left > ui.selection->bbox.right) {
    x1 = ui.selection->bbox.right;  x2 = ui.selection->bbox.left;
    ui.selection->bbox.left = x1;   ui.selection->bbox.right = x2;
  } else {
    x1 = ui.selection->bbox.left;  x2 = ui.selection->bbox.right;
  }

  if (ui.selection->bbox.top > ui.selection->bbox.bottom) {
    y1 = ui.selection->bbox.bottom;  y2 = ui.selection->bbox.top;
    ui.selection->bbox.top = y1;   ui.selection->bbox.bottom = y2;
  } else {
    y1 = ui.selection->bbox.top;  y2 = ui.selection->bbox.bottom;
  }
  
  for (itemlist = ui.selection->layer->items; itemlist!=NULL; itemlist = itemlist->next) {
    item = (struct Item *)itemlist->data;
    if (item->bbox.left >= x1 && item->bbox.right <= x2 &&
          item->bbox.top >= y1 && item->bbox.bottom <= y2) {
      ui.selection->items = g_list_append(ui.selection->items, item); 
    }
  }
  
  if (ui.selection->items == NULL) reset_selection();
  else make_dashed(ui.selection->canvas_item);
  update_cursor();
  update_copy_paste_enabled();
}

gboolean start_movesel(GdkEvent *event)
{
  double pt[2];
  
  if (ui.selection==NULL) return FALSE;
  if (ui.cur_layer != ui.selection->layer) return FALSE;
  
  get_pointer_coords(event, pt);
  if (ui.selection->type == ITEM_SELECTRECT) {
    if (pt[0]<ui.selection->bbox.left || pt[0]>ui.selection->bbox.right ||
        pt[1]<ui.selection->bbox.top  || pt[1]>ui.selection->bbox.bottom)
      return FALSE;
    ui.cur_item_type = ITEM_MOVESEL;
    ui.selection->anchor_x = ui.selection->last_x = pt[0];
    ui.selection->anchor_y = ui.selection->last_y = pt[1];
    ui.selection->orig_pageno = ui.pageno;
    ui.selection->move_pageno = ui.pageno;
    ui.selection->move_layer = ui.selection->layer;
    ui.selection->move_pagedelta = 0.;
    gnome_canvas_item_set(ui.selection->canvas_item, "dash", NULL, NULL);
    update_cursor();
    return TRUE;
  }
  return FALSE;
}

void start_vertspace(GdkEvent *event)
{
  double pt[2];
  GList *itemlist;
  struct Item *item;

  reset_selection();
  ui.cur_item_type = ITEM_MOVESEL_VERT;
  ui.selection = g_new(struct Selection, 1);
  ui.selection->type = ITEM_MOVESEL_VERT;
  ui.selection->items = NULL;
  ui.selection->layer = ui.cur_layer;

  get_pointer_coords(event, pt);
  ui.selection->bbox.top = ui.selection->bbox.bottom = pt[1];
  for (itemlist = ui.cur_layer->items; itemlist!=NULL; itemlist = itemlist->next) {
    item = (struct Item *)itemlist->data;
    if (item->bbox.top >= pt[1]) {
      ui.selection->items = g_list_append(ui.selection->items, item); 
      if (item->bbox.bottom > ui.selection->bbox.bottom)
        ui.selection->bbox.bottom = item->bbox.bottom;
    }
  }

  ui.selection->anchor_x = ui.selection->last_x = 0;
  ui.selection->anchor_y = ui.selection->last_y = pt[1];
  ui.selection->orig_pageno = ui.pageno;
  ui.selection->move_pageno = ui.pageno;
  ui.selection->move_layer = ui.selection->layer;
  ui.selection->move_pagedelta = 0.;
  ui.selection->canvas_item = gnome_canvas_item_new(ui.cur_layer->group,
      gnome_canvas_rect_get_type(), "width-pixels", 1, 
      "outline-color-rgba", 0x000000ff,
      "fill-color-rgba", 0x80808040,
      "x1", -100.0, "x2", ui.cur_page->width+100, "y1", pt[1], "y2", pt[1], NULL);
  update_cursor();
}

void continue_movesel(GdkEvent *event)
{
  double pt[2], dx, dy, upmargin;
  GList *list;
  struct Item *item;
  int tmppageno;
  struct Page *tmppage;
  
  get_pointer_coords(event, pt);
  if (ui.cur_item_type == ITEM_MOVESEL_VERT) pt[0] = 0;
  pt[1] += ui.selection->move_pagedelta;

  // check for page jumps
  if (ui.cur_item_type == ITEM_MOVESEL_VERT)
    upmargin = ui.selection->bbox.bottom - ui.selection->bbox.top;
  else upmargin = VIEW_CONTINUOUS_SKIP;
  tmppageno = ui.selection->move_pageno;
  tmppage = g_list_nth_data(journal.pages, tmppageno);
  while (ui.view_continuous && (pt[1] < - upmargin)) {
    if (tmppageno == 0) break;
    tmppageno--;
    tmppage = g_list_nth_data(journal.pages, tmppageno);
    pt[1] += tmppage->height + VIEW_CONTINUOUS_SKIP;
    ui.selection->move_pagedelta += tmppage->height + VIEW_CONTINUOUS_SKIP;
  }
  while (ui.view_continuous && (pt[1] > tmppage->height+VIEW_CONTINUOUS_SKIP)) {
    if (tmppageno == journal.npages-1) break;
    pt[1] -= tmppage->height + VIEW_CONTINUOUS_SKIP;
    ui.selection->move_pagedelta -= tmppage->height + VIEW_CONTINUOUS_SKIP;
    tmppageno++;
    tmppage = g_list_nth_data(journal.pages, tmppageno);
  }
  
  if (tmppageno != ui.selection->move_pageno) {
    // move to a new page !
    ui.selection->move_pageno = tmppageno;
    if (tmppageno == ui.selection->orig_pageno)
      ui.selection->move_layer = ui.selection->layer;
    else
      ui.selection->move_layer = (struct Layer *)(g_list_last(
        ((struct Page *)g_list_nth_data(journal.pages, tmppageno))->layers)->data);
    gnome_canvas_item_reparent(ui.selection->canvas_item, ui.selection->move_layer->group);
    for (list = ui.selection->items; list!=NULL; list = list->next) {
      item = (struct Item *)list->data;
      if (item->canvas_item!=NULL)
        gnome_canvas_item_reparent(item->canvas_item, ui.selection->move_layer->group);
    }
    // avoid a refresh bug
    gnome_canvas_item_move(GNOME_CANVAS_ITEM(ui.selection->move_layer->group), 0., 0.);
    if (ui.cur_item_type == ITEM_MOVESEL_VERT)
      gnome_canvas_item_set(ui.selection->canvas_item,
        "x2", tmppage->width+100, 
        "y1", ui.selection->anchor_y+ui.selection->move_pagedelta, NULL);
  }
  
  // now, process things normally

  dx = pt[0] - ui.selection->last_x;
  dy = pt[1] - ui.selection->last_y;
  if (hypot(dx,dy) < 1) return; // don't move subpixel
  ui.selection->last_x = pt[0];
  ui.selection->last_y = pt[1];

  // move the canvas items
  if (ui.cur_item_type == ITEM_MOVESEL_VERT)
    gnome_canvas_item_set(ui.selection->canvas_item, "y2", pt[1], NULL);
  else 
    gnome_canvas_item_move(ui.selection->canvas_item, dx, dy);
  
  for (list = ui.selection->items; list != NULL; list = list->next) {
    item = (struct Item *)list->data;
    if (item->canvas_item != NULL)
      gnome_canvas_item_move(item->canvas_item, dx, dy);
  }
}

void finalize_movesel(void)
{
  GList *list, *link;
  struct Item *item;
  
  if (ui.selection->items != NULL) {
    prepare_new_undo();
    undo->type = ITEM_MOVESEL;
    undo->itemlist = g_list_copy(ui.selection->items);
    undo->val_x = ui.selection->last_x - ui.selection->anchor_x;
    undo->val_y = ui.selection->last_y - ui.selection->anchor_y;
    undo->layer = ui.selection->layer;
    undo->layer2 = ui.selection->move_layer;
    undo->auxlist = NULL;
    // build auxlist = pointers to Item's just before ours (for depths)
    for (list = ui.selection->items; list!=NULL; list = list->next) {
      link = g_list_find(ui.selection->layer->items, list->data);
      if (link!=NULL) link = link->prev;
      undo->auxlist = g_list_append(undo->auxlist, ((link!=NULL) ? link->data : NULL));
    }
    ui.selection->layer = ui.selection->move_layer;
    move_journal_items_by(undo->itemlist, undo->val_x, undo->val_y,
                          undo->layer, undo->layer2, 
                          (undo->layer == undo->layer2)?undo->auxlist:NULL);
  }

  if (ui.selection->move_pageno!=ui.selection->orig_pageno) 
    do_switch_page(ui.selection->move_pageno, FALSE, FALSE);
    
  if (ui.cur_item_type == ITEM_MOVESEL_VERT)
    reset_selection();
  else {
    ui.selection->bbox.left += undo->val_x;
    ui.selection->bbox.right += undo->val_x;
    ui.selection->bbox.top += undo->val_y;
    ui.selection->bbox.bottom += undo->val_y;
    make_dashed(ui.selection->canvas_item);
  }
  ui.cur_item_type = ITEM_NONE;
  update_cursor();
}


void selection_delete(void)
{
  struct UndoErasureData *erasure;
  GList *itemlist;
  struct Item *item;
  
  if (ui.selection == NULL) return;
  prepare_new_undo();
  undo->type = ITEM_ERASURE;
  undo->layer = ui.selection->layer;
  undo->erasurelist = NULL;
  for (itemlist = ui.selection->items; itemlist!=NULL; itemlist = itemlist->next) {
    item = (struct Item *)itemlist->data;
    if (item->canvas_item!=NULL)
      gtk_object_destroy(GTK_OBJECT(item->canvas_item));
    erasure = g_new(struct UndoErasureData, 1);
    erasure->item = item;
    erasure->npos = g_list_index(ui.selection->layer->items, item);
    erasure->nrepl = 0;
    erasure->replacement_items = NULL;
    ui.selection->layer->items = g_list_remove(ui.selection->layer->items, item);
    ui.selection->layer->nitems--;
    undo->erasurelist = g_list_prepend(undo->erasurelist, erasure);
  }
  reset_selection();

  /* NOTE: the erasurelist is built backwards; this guarantees that,
     upon undo, the erasure->npos fields give the correct position
     where each item should be reinserted as the list is traversed in
     the forward direction */
}

void callback_clipboard_get(GtkClipboard *clipboard,
                            GtkSelectionData *selection_data,
                            guint info, gpointer user_data)
{
  int length;
  
  g_memmove(&length, user_data, sizeof(int));
  gtk_selection_data_set(selection_data,
     gdk_atom_intern("_XOURNAL", FALSE), 8, user_data, length);
}

void callback_clipboard_clear(GtkClipboard *clipboard, gpointer user_data)
{
  g_free(user_data);
}

void selection_to_clip(void)
{
  int bufsz, nitems;
  char *buf, *p;
  GList *list;
  struct Item *item;
  GtkTargetEntry target;
  
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
    }
    else bufsz+= sizeof(int); // type
  }
  p = buf = g_malloc(bufsz);
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
    }
  }
  
  target.target = "_XOURNAL";
  target.flags = 0;
  target.info = 0;
  
  gtk_clipboard_set_with_data(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), 
       &target, 1,
       callback_clipboard_get, callback_clipboard_clear, buf);
}


void clipboard_paste(void)
{
  GtkSelectionData *sel_data;
  unsigned char *p;
  int nitems, npts, i;
  GList *list;
  struct Item *item;
  double hoffset, voffset, cx, cy;
  double *pf;
  int sx, sy, wx, wy;
  
  if (ui.cur_layer == NULL) return;
  
  ui.cur_item_type = ITEM_PASTE;
  sel_data = gtk_clipboard_wait_for_contents(
      gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
      gdk_atom_intern("_XOURNAL", FALSE));
  ui.cur_item_type = ITEM_NONE;
  if (sel_data == NULL) return; // paste failed
  
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
      update_item_bbox(item);
      item->canvas_item = gnome_canvas_item_new(ui.cur_layer->group,
             gnome_canvas_line_get_type(), "points", item->path,
             "cap-style", GDK_CAP_ROUND, "join-style", GDK_JOIN_ROUND,
             "fill-color-rgba", item->brush.color_rgba,
             "width-units", item->brush.thickness, NULL);
    }
  }

  prepare_new_undo();
  undo->type = ITEM_PASTE;
  undo->layer = ui.cur_layer;
  undo->itemlist = g_list_copy(ui.selection->items);  
  
  gtk_selection_data_free(sel_data);
  update_copy_paste_enabled();
}

// modify the color or thickness of pen strokes in a selection

void recolor_selection(int color)
{
  GList *itemlist;
  struct Item *item;
  struct Brush *brush;
  
  if (ui.selection == NULL) return;
  prepare_new_undo();
  undo->type = ITEM_REPAINTSEL;
  undo->itemlist = NULL;
  undo->auxlist = NULL;
  for (itemlist = ui.selection->items; itemlist!=NULL; itemlist = itemlist->next) {
    item = (struct Item *)itemlist->data;
    if (item->type != ITEM_STROKE || item->brush.tool_type!=TOOL_PEN) continue;
    // store info for undo
    undo->itemlist = g_list_append(undo->itemlist, item);
    brush = (struct Brush *)g_malloc(sizeof(struct Brush));
    g_memmove(brush, &(item->brush), sizeof(struct Brush));
    undo->auxlist = g_list_append(undo->auxlist, brush);
    // repaint the stroke
    item->brush.color_no = color;
    item->brush.color_rgba = predef_colors_rgba[color];
    if (item->canvas_item!=NULL)
      gnome_canvas_item_set(item->canvas_item, 
         "fill-color-rgba", item->brush.color_rgba, NULL);
  }
}

void rethicken_selection(int val)
{
  GList *itemlist;
  struct Item *item;
  struct Brush *brush;
  
  if (ui.selection == NULL) return;
  prepare_new_undo();
  undo->type = ITEM_REPAINTSEL;
  undo->itemlist = NULL;
  undo->auxlist = NULL;
  for (itemlist = ui.selection->items; itemlist!=NULL; itemlist = itemlist->next) {
    item = (struct Item *)itemlist->data;
    if (item->type != ITEM_STROKE || item->brush.tool_type!=TOOL_PEN) continue;
    // store info for undo
    undo->itemlist = g_list_append(undo->itemlist, item);
    brush = (struct Brush *)g_malloc(sizeof(struct Brush));
    g_memmove(brush, &(item->brush), sizeof(struct Brush));
    undo->auxlist = g_list_append(undo->auxlist, brush);
    // repaint the stroke
    item->brush.thickness_no = val;
    item->brush.thickness = predef_thickness[TOOL_PEN][val];
    if (item->canvas_item!=NULL)
      gnome_canvas_item_set(item->canvas_item, 
         "width-units", item->brush.thickness, NULL);
  }
}

void do_hand(GdkEvent *event)
{
  double pt[2], val;
  int cx, cy;
  
  get_pointer_coords(event, pt);
  gnome_canvas_get_scroll_offsets(canvas, &cx, &cy);
  cx -= (pt[0]-ui.hand_refpt[0])*ui.zoom;
  cy -= (pt[1]-ui.hand_refpt[1])*ui.zoom;
  gnome_canvas_scroll_to(canvas, cx, cy);
}
