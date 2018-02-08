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
#include <libgnomecanvas/libgnomecanvas.h>

#include "xournal.h"
#include "xo-callbacks.h"
#include "xo-misc.h"
#include "xo-paint.h"

/************** drawing nice cursors *********/

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

#define PEN_CURSOR_RADIUS 1
#define HILITER_CURSOR_RADIUS 3
#define HILITER_BORDER_RADIUS 4

GdkCursor *make_pen_cursor(guint color_rgba)
{
  int rowstride, x, y;
  guchar col[4], *pixels;

  if (ui.pen_cursor == TRUE) {  
    return gdk_cursor_new(GDK_PENCIL);
  }
  
  if (ui.pen_cursor_pix == NULL) {
    ui.pen_cursor_pix = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 16, 16);
    if (ui.pen_cursor_pix == NULL) return NULL; // couldn't create pixbuf
    gdk_pixbuf_fill(ui.pen_cursor_pix, 0xffffff00); // transparent white
  }
  rowstride = gdk_pixbuf_get_rowstride(ui.pen_cursor_pix);
  pixels = gdk_pixbuf_get_pixels(ui.pen_cursor_pix);

  col[0] = (color_rgba >> 24) & 0xff;
  col[1] = (color_rgba >> 16) & 0xff;
  col[2] = (color_rgba >> 8) & 0xff;
  col[3] = 0xff; // solid
  for (x = 8-PEN_CURSOR_RADIUS; x <= 8+PEN_CURSOR_RADIUS; x++)
    for (y = 8-PEN_CURSOR_RADIUS; y <= 8+PEN_CURSOR_RADIUS; y++)
      g_memmove(pixels + y*rowstride + x*4, col, 4);
  
  return gdk_cursor_new_from_pixbuf(gdk_display_get_default(), ui.pen_cursor_pix, 7, 7);
}

GdkCursor *make_hiliter_cursor(guint color_rgba)
{
  int rowstride, x, y;
  guchar col[4], *pixels;
  
  if (ui.hiliter_cursor_pix == NULL) {
    ui.hiliter_cursor_pix = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 16, 16);
    if (ui.hiliter_cursor_pix == NULL) return NULL; // couldn't create pixbuf
    gdk_pixbuf_fill(ui.hiliter_cursor_pix, 0xffffff00); // transparent white
  }
  rowstride = gdk_pixbuf_get_rowstride(ui.hiliter_cursor_pix);
  pixels = gdk_pixbuf_get_pixels(ui.hiliter_cursor_pix);

  col[0] = col[1] = col[2] = 0; // black
  col[3] = 0xff; // solid
  for (x = 8-HILITER_BORDER_RADIUS; x <= 8+HILITER_BORDER_RADIUS; x++)
    for (y = 8-HILITER_BORDER_RADIUS; y <= 8+HILITER_BORDER_RADIUS; y++)
      g_memmove(pixels + y*rowstride + x*4, col, 4);

  col[0] = (color_rgba >> 24) & 0xff;
  col[1] = (color_rgba >> 16) & 0xff;
  col[2] = (color_rgba >> 8) & 0xff;
  col[3] = 0xff; // solid
  for (x = 8-HILITER_CURSOR_RADIUS; x <= 8+HILITER_CURSOR_RADIUS; x++)
    for (y = 8-HILITER_CURSOR_RADIUS; y <= 8+HILITER_CURSOR_RADIUS; y++)
      g_memmove(pixels + y*rowstride + x*4, col, 4);
  
  return gdk_cursor_new_from_pixbuf(gdk_display_get_default(), ui.hiliter_cursor_pix, 7, 7);
}

void update_cursor(void)
{
  GdkPixmap *source, *mask;
  GdkColor fg = {0, 0, 0, 0}, bg = {0, 65535, 65535, 65535};

  ui.is_sel_cursor = FALSE;
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
    ui.cursor = make_pen_cursor(ui.cur_brush->color_rgba);
  }
  else if (ui.toolno[ui.cur_mapping] == TOOL_ERASER) {
    ui.cursor = make_hiliter_cursor(0xffffffff);
  }
  else if (ui.toolno[ui.cur_mapping] == TOOL_HIGHLIGHTER) {
    ui.cursor = make_hiliter_cursor(ui.cur_brush->color_rgba);
  }
  else if (ui.cur_item_type == ITEM_SELECTRECT) {
    ui.cursor = gdk_cursor_new(GDK_TCROSS);
  }
  else if (ui.toolno[ui.cur_mapping] == TOOL_HAND) {
    ui.cursor = gdk_cursor_new(GDK_HAND1);
  }
  else if (ui.toolno[ui.cur_mapping] == TOOL_TEXT) {
    ui.cursor = gdk_cursor_new(GDK_XTERM);
  }
  
  gdk_window_set_cursor(GTK_WIDGET(canvas)->window, ui.cursor);
}

/* adjust the cursor shape if it hovers near a selection box */

void update_cursor_for_resize(double *pt)
{
  gboolean in_range_x, in_range_y;
  gboolean can_resize_left, can_resize_right, can_resize_bottom, can_resize_top;
  gdouble resize_margin;
  GdkCursorType newcursor;

  // if we're not even close to the box in some direction, return immediately
  resize_margin = RESIZE_MARGIN / ui.zoom;
  if (pt[0]<ui.selection->bbox.left-resize_margin || pt[0]>ui.selection->bbox.right+resize_margin
   || pt[1]<ui.selection->bbox.top-resize_margin || pt[1]>ui.selection->bbox.bottom+resize_margin)
  {
    if (ui.is_sel_cursor) update_cursor();
    return;
  }

  ui.is_sel_cursor = TRUE;
  can_resize_left = (pt[0] < ui.selection->bbox.left+resize_margin);
  can_resize_right = (pt[0] > ui.selection->bbox.right-resize_margin);
  can_resize_top = (pt[1] < ui.selection->bbox.top+resize_margin);
  can_resize_bottom = (pt[1] > ui.selection->bbox.bottom-resize_margin);

  if (can_resize_left) {
    if (can_resize_top) newcursor = GDK_TOP_LEFT_CORNER;
    else if (can_resize_bottom) newcursor = GDK_BOTTOM_LEFT_CORNER;
    else newcursor = GDK_LEFT_SIDE;
  }
  else if (can_resize_right) {
    if (can_resize_top) newcursor = GDK_TOP_RIGHT_CORNER;
    else if (can_resize_bottom) newcursor = GDK_BOTTOM_RIGHT_CORNER;
    else newcursor = GDK_RIGHT_SIDE;
  }
  else if (can_resize_top) newcursor = GDK_TOP_SIDE;
  else if (can_resize_bottom) newcursor = GDK_BOTTOM_SIDE;
  else newcursor = GDK_FLEUR;

  if (ui.cursor!=NULL && ui.cursor->type == newcursor) return;
  if (ui.cursor!=NULL) gdk_cursor_unref(ui.cursor);
  ui.cursor = gdk_cursor_new(newcursor);
  gdk_window_set_cursor(GTK_WIDGET(canvas)->window, ui.cursor);
}

/************** painting strokes *************/

#define SUBDIVIDE_MAXDIST 5.0

void subdivide_cur_path(void)
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
  
  if (ui.cur_brush->ruler) {
    ui.cur_item->canvas_item = gnome_canvas_item_new(ui.cur_layer->group,
      gnome_canvas_line_get_type(),
      "cap-style", GDK_CAP_ROUND, "join-style", GDK_JOIN_ROUND,
      "fill-color-rgba", ui.cur_item->brush.color_rgba,
      "width-units", ui.cur_item->brush.thickness, NULL);
    ui.cur_item->brush.variable_width = FALSE;
  } else
    ui.cur_item->canvas_item = gnome_canvas_item_new(
      ui.cur_layer->group, gnome_canvas_group_get_type(), NULL);
}

void continue_stroke(GdkEvent *event)
{
  GnomeCanvasPoints seg;
  double *pt, current_width, pressure;

  if (ui.cur_brush->ruler) {
    pt = ui.cur_path.coords;
  } else {
    realloc_cur_path(ui.cur_path.num_points+1);
    pt = ui.cur_path.coords + 2*(ui.cur_path.num_points-1);
  } 
  
  get_pointer_coords(event, pt+2);

  if (ui.cur_item->brush.variable_width) {
    realloc_cur_widths(ui.cur_path.num_points);
    pressure = get_pressure_multiplier(event);
    if (pressure > ui.width_minimum_multiplier) 
      current_width = ui.cur_item->brush.thickness*get_pressure_multiplier(event);
    else { // reported pressure is 0.
      if (ui.cur_path.num_points >= 2) current_width = ui.cur_widths[ui.cur_path.num_points-2];
      else current_width = ui.cur_item->brush.thickness;
    }
    ui.cur_widths[ui.cur_path.num_points-1] = current_width;
  }
  else current_width = ui.cur_item->brush.thickness;
  
  if (ui.cur_brush->ruler)
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

  if (ui.cur_brush->ruler)
    gnome_canvas_item_set(ui.cur_item->canvas_item, "points", &seg, NULL);
  else
    gnome_canvas_item_new((GnomeCanvasGroup *)ui.cur_item->canvas_item,
       gnome_canvas_line_get_type(), "points", &seg,
       "cap-style", GDK_CAP_ROUND, "join-style", GDK_JOIN_ROUND,
       "fill-color-rgba", ui.cur_item->brush.color_rgba,
       "width-units", current_width, NULL);
}

void abort_stroke(void)
{
  if (ui.cur_item_type != ITEM_STROKE || ui.cur_item == NULL) return;
  ui.cur_path.num_points = 0;
  gtk_object_destroy(GTK_OBJECT(ui.cur_item->canvas_item));
  g_free(ui.cur_item);
  ui.cur_item = NULL;
  ui.cur_item_type = ITEM_NONE;
}

#define HOOK_MAX_ANGLE_COS 0.9

gboolean fix_origin_if_needed(double *pt)
{
  double dotproduct,len1,len2;
  dotproduct = (pt[2]-pt[0])*(pt[4]-pt[2]) + (pt[3]-pt[1])*(pt[5]-pt[3]);
  len1 = hypot(pt[2]-pt[0],pt[3]-pt[1]);
  len2 = hypot(pt[4]-pt[2],pt[5]-pt[3]);
  if (dotproduct < HOOK_MAX_ANGLE_COS * len1 * len2) {
    // straighten
/*
    if (dotproduct > 0 && len2 > EPSILON) {
      pt[0] = pt[2]-dotproduct*(pt[4]-pt[2])/len2/len2;
      pt[1] = pt[3]-dotproduct*(pt[5]-pt[3])/len2/len2;
    } else */
    {
      pt[0] = pt[2];
      pt[1] = pt[3];
    }
    return TRUE;
  }
  return FALSE;
}

void finalize_stroke(void)
{
  gboolean need_refresh = FALSE;
  
  if (ui.cur_path.num_points == 1) { // GnomeCanvas doesn't like num_points=1
    ui.cur_path.coords[2] = ui.cur_path.coords[0]+0.1;
    ui.cur_path.coords[3] = ui.cur_path.coords[1];
    ui.cur_path.num_points = 2;
    ui.cur_item->brush.variable_width = FALSE;
  }
  
  /* fix AES pen mess on Lenovo X1 Yoga 2nd gen and similar... */
  if (ui.fix_stroke_origin && ui.cur_path.num_points > 2) {
    need_refresh = fix_origin_if_needed(ui.cur_path.coords);
  }
  
  if (!ui.cur_item->brush.variable_width)
    subdivide_cur_path(); // split the segment so eraser will work

  ui.cur_item->path = gnome_canvas_points_new(ui.cur_path.num_points);
  g_memmove(ui.cur_item->path->coords, ui.cur_path.coords, 
      2*ui.cur_path.num_points*sizeof(double));
  if (ui.cur_item->brush.variable_width)
    ui.cur_item->widths = (gdouble *)g_memdup(ui.cur_widths, 
                            (ui.cur_path.num_points-1)*sizeof(gdouble));
  else ui.cur_item->widths = NULL;
  update_item_bbox(ui.cur_item);
  ui.cur_path.num_points = 0;

  if (!ui.cur_item->brush.variable_width || need_refresh) {
    // destroy the entire group of temporary line segments
    gtk_object_destroy(GTK_OBJECT(ui.cur_item->canvas_item));
    // make a new line item to replace it
    make_canvas_item_one(ui.cur_layer->group, ui.cur_item);
  }

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
          if (newhead->brush.variable_width)
            newhead->widths = (gdouble *)g_memdup(item->widths, (i-1)*sizeof(gdouble));
          else newhead->widths = NULL;
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
          if (newtail->brush.variable_width)
            newtail->widths = (gdouble *)g_memdup(item->widths+i, 
              (item->path->num_points-i-1)*sizeof(gdouble));
          else newtail->widths = NULL;
          newtail->canvas_item = NULL;
        }
      }
      if (item->type == ITEM_STROKE) { 
        // it's inside an erasure list - we destroy it
        gnome_canvas_points_free(item->path);
        if (item->brush.variable_width) g_free(item->widths);
        if (item->canvas_item != NULL) 
          gtk_object_destroy(GTK_OBJECT(item->canvas_item));
        erasure->nrepl--;
        erasure->replacement_items = g_list_remove(erasure->replacement_items, item);
        g_free(item);
      }
      // add the new head
      if (newhead != NULL) {
        update_item_bbox(newhead);
        make_canvas_item_one(ui.cur_layer->group, newhead);
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
  make_canvas_item_one(ui.cur_layer->group, item);
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
  struct Item *item;
  
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


gboolean do_hand_scrollto(gpointer data)
{
  ui.hand_scrollto_pending = FALSE;
  gnome_canvas_scroll_to(canvas, ui.hand_scrollto_cx, ui.hand_scrollto_cy);
  return FALSE;
}


gboolean do_continue_scrolling(gpointer data)
{
  int cx, cy, ncx, ncy;
  double speed, factor;
  
  gnome_canvas_get_scroll_offsets(canvas, &cx, &cy);
  gnome_canvas_scroll_to(canvas, cx + ui.hand_speed_x * SCROLL_FRAMETIME, cy + ui.hand_speed_y * SCROLL_FRAMETIME);
  gnome_canvas_get_scroll_offsets(canvas, &ncx, &ncy);
  
  /* Hit a border? */
  if (ncx == cx) ui.hand_speed_x = 0;
  if (ncy == cy) ui.hand_speed_y = 0;
    
  speed = sqrt(ui.hand_speed_x * ui.hand_speed_x + ui.hand_speed_y * ui.hand_speed_y);
  if (speed < SCROLL_SLOWDOWN)
    return FALSE;
  factor = (speed - SCROLL_SLOWDOWN) / speed;
  ui.hand_speed_x *= factor;
  ui.hand_speed_y *= factor;
    
  return TRUE;
}


void start_hand(GdkEvent *event)
{
  get_pointer_coords(event, ui.hand_refpt);
  ui.hand_refpt[0] += ui.cur_page->hoffset;
  ui.hand_refpt[1] += ui.cur_page->voffset;
  ui.hand_prev_time = event->button.time;
  ui.hand_speed_x = 0;
  ui.hand_speed_y = 0;
}


void do_hand(GdkEvent *event)
{
  double pt[2];
  int cx, cy;
  double dx, dy;
  int dt;
  
  get_pointer_coords(event, pt);
  pt[0] += ui.cur_page->hoffset;
  pt[1] += ui.cur_page->voffset;
  dx = -(pt[0]-ui.hand_refpt[0])*ui.zoom;
  dy = -(pt[1]-ui.hand_refpt[1])*ui.zoom;

  gnome_canvas_get_scroll_offsets(canvas, &cx, &cy);

  if (ui.lockHorizontalScroll)
    ui.hand_scrollto_cx = cx;
  else
    ui.hand_scrollto_cx = cx - (pt[0]-ui.hand_refpt[0])*ui.zoom;

  ui.hand_scrollto_cy = cy - (pt[1]-ui.hand_refpt[1])*ui.zoom;
  if (!ui.hand_scrollto_pending) g_idle_add(do_hand_scrollto, NULL);
  ui.hand_scrollto_pending = TRUE;
  dt = event->motion.time - ui.hand_prev_time;
  if (dt < SCROLL_MEASURE_INTERVAL) {
    dx += ui.hand_speed_x * (SCROLL_MEASURE_INTERVAL - dt);
    dy += ui.hand_speed_y * (SCROLL_MEASURE_INTERVAL - dt);
    dt = SCROLL_MEASURE_INTERVAL;
  }
  ui.hand_speed_x = dx / dt;
  ui.hand_speed_y = dy / dt;
  ui.hand_prev_time = event->motion.time;
}

void finalize_hand(void) {
  g_timeout_add(SCROLL_FRAMETIME, do_continue_scrolling, NULL);
}

void stop_scrolling(void) {
  ui.hand_speed_x = 0;
  ui.hand_speed_y = 0;
}


/************ TEXT FUNCTIONS **************/

// to make it easier to copy/paste at end of text box
#define WIDGET_RIGHT_MARGIN 10

void resize_textview(gpointer *toplevel, gpointer *data)
{
  GtkTextView *w;
  int width, height;
  
  /* when the text changes, resize the GtkTextView accordingly */
  if (ui.cur_item_type!=ITEM_TEXT) return;
  w = GTK_TEXT_VIEW(ui.cur_item->widget);
  width = w->width + WIDGET_RIGHT_MARGIN;
  height = w->height;
  gnome_canvas_item_set(ui.cur_item->canvas_item, 
    "size-pixels", TRUE, 
    "width", (gdouble)width, "height", (gdouble)height, NULL);
  ui.cur_item->bbox.right = ui.cur_item->bbox.left + width/ui.zoom;
  ui.cur_item->bbox.bottom = ui.cur_item->bbox.top + height/ui.zoom;
}

void start_text(GdkEvent *event, struct Item *item)
{
  double pt[2];
  GtkTextBuffer *buffer;
  GnomeCanvasItem *canvas_item;
  PangoFontDescription *font_desc;
  GdkColor color;
  GdkColor bgcolor;

  get_pointer_coords(event, pt);

  ui.cur_item_type = ITEM_TEXT;

  if (item==NULL) {
    item = g_new(struct Item, 1);
    item->text = NULL;
    item->canvas_item = NULL;
    item->bbox.left = pt[0];
    item->bbox.top = pt[1];
    item->bbox.right = ui.cur_page->width;
    item->bbox.bottom = pt[1]+100.;
    item->font_name = g_strdup(ui.font_name);
    item->font_size = ui.font_size;
    g_memmove(&(item->brush), ui.cur_brush, sizeof(struct Brush));
    ui.cur_layer->items = g_list_append(ui.cur_layer->items, item);
    ui.cur_layer->nitems++;
  }
  
  item->type = ITEM_TEMP_TEXT;
  ui.cur_item = item;
  
  font_desc = pango_font_description_from_string(item->font_name);
  pango_font_description_set_absolute_size(font_desc, 
      item->font_size*ui.zoom*PANGO_SCALE);
  item->widget = gtk_text_view_new();
  buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(item->widget));
  if (item->text!=NULL)
    gtk_text_buffer_set_text(buffer, item->text, -1);
  gtk_widget_modify_font(item->widget, font_desc);
  rgb_to_gdkcolor(item->brush.color_rgba, &color);
  gtk_widget_modify_text(item->widget, GTK_STATE_NORMAL, &color);
  text_background_color(&color, &bgcolor);
  gtk_widget_modify_base(item->widget, GTK_STATE_NORMAL, &bgcolor);
  pango_font_description_free(font_desc);

  canvas_item = gnome_canvas_item_new(ui.cur_layer->group,
    gnome_canvas_widget_get_type(),
    "x", item->bbox.left, "y", item->bbox.top, 
    "width", item->bbox.right-item->bbox.left, 
    "height", item->bbox.bottom-item->bbox.top,
    "widget", item->widget, NULL);
  // TODO: width/height?
  if (item->canvas_item!=NULL) {
    lower_canvas_item_to(ui.cur_layer->group, canvas_item, item->canvas_item);
    gtk_object_destroy(GTK_OBJECT(item->canvas_item));
  }
  item->canvas_item = canvas_item;

  gtk_widget_show(item->widget);
  ui.resize_signal_handler = 
    g_signal_connect((gpointer) winMain, "check_resize",
       G_CALLBACK(resize_textview), NULL);
  update_font_button();
  gtk_widget_set_sensitive(GET_COMPONENT("editPaste"), FALSE);
  gtk_widget_set_sensitive(GET_COMPONENT("buttonPaste"), FALSE);
  gtk_widget_grab_focus(item->widget); 
}

void end_text_and_stop_scrolling(void)
{
  end_text();
  stop_scrolling();
}

void end_text(void)
{
  GtkTextBuffer *buffer;
  GtkTextIter start, end;
  gchar *new_text;
  struct UndoErasureData *erasure;
  GnomeCanvasItem *tmpitem;

  if (ui.cur_item_type!=ITEM_TEXT) return; // nothing for us to do!

  // finalize the text that's been edited... 
  buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ui.cur_item->widget));
  gtk_text_buffer_get_bounds(buffer, &start, &end);
  ui.cur_item->type = ITEM_TEXT;
  new_text = gtk_text_buffer_get_text(buffer, &start, &end, TRUE);
  ui.cur_item_type = ITEM_NONE;
  gtk_widget_set_sensitive(GET_COMPONENT("editPaste"), TRUE);
  gtk_widget_set_sensitive(GET_COMPONENT("buttonPaste"), TRUE);
  
  if (strlen(new_text)==0) { // erase object and cancel
    g_free(new_text);
    g_signal_handler_disconnect(winMain, ui.resize_signal_handler);
    gtk_object_destroy(GTK_OBJECT(ui.cur_item->canvas_item));
    ui.cur_item->canvas_item = NULL;
    if (ui.cur_item->text == NULL) // nothing happened
      g_free(ui.cur_item->font_name);
    else { // treat this as an erasure
      prepare_new_undo();
      undo->type = ITEM_ERASURE;
      undo->layer = ui.cur_layer;
      erasure = (struct UndoErasureData *)g_malloc(sizeof(struct UndoErasureData));
      erasure->item = ui.cur_item;
      erasure->npos = g_list_index(ui.cur_layer->items, ui.cur_item);
      erasure->nrepl = 0;
      erasure->replacement_items = NULL;
      undo->erasurelist = g_list_append(NULL, erasure);
    }
    ui.cur_layer->items = g_list_remove(ui.cur_layer->items, ui.cur_item);
    ui.cur_layer->nitems--;
    ui.cur_item = NULL;
    return;
  }

  // store undo data
  if (ui.cur_item->text==NULL || strcmp(ui.cur_item->text, new_text)) {
    prepare_new_undo();
    if (ui.cur_item->text == NULL) undo->type = ITEM_TEXT; 
    else undo->type = ITEM_TEXT_EDIT;
    undo->layer = ui.cur_layer;
    undo->item = ui.cur_item;
    undo->str = ui.cur_item->text;
  }
  else g_free(ui.cur_item->text);

  ui.cur_item->text = new_text;
  ui.cur_item->widget = NULL;
  // replace the canvas item
  tmpitem = ui.cur_item->canvas_item;
  make_canvas_item_one(ui.cur_layer->group, ui.cur_item);
  update_item_bbox(ui.cur_item);
  lower_canvas_item_to(ui.cur_layer->group, ui.cur_item->canvas_item, tmpitem);
  gtk_object_destroy(GTK_OBJECT(tmpitem));
}

/* update the items in the canvas so they're of the right font size */

void update_text_item_displayfont(struct Item *item)
{
  PangoFontDescription *font_desc;

  if (item->type != ITEM_TEXT && item->type != ITEM_TEMP_TEXT) return;
  if (item->canvas_item==NULL) return;
  font_desc = pango_font_description_from_string(item->font_name);
  pango_font_description_set_absolute_size(font_desc, 
        item->font_size*ui.zoom*PANGO_SCALE);
  if (item->type == ITEM_TEMP_TEXT)
    gtk_widget_modify_font(item->widget, font_desc);
  else {
    gnome_canvas_item_set(item->canvas_item, "font-desc", font_desc, NULL);
    update_item_bbox(item);
  }
  pango_font_description_free(font_desc);
}

void rescale_text_items(void)
{
  GList *pagelist, *layerlist, *itemlist;
  
  for (pagelist = journal.pages; pagelist!=NULL; pagelist = pagelist->next)
    for (layerlist = ((struct Page *)pagelist->data)->layers; layerlist!=NULL; layerlist = layerlist->next)
      for (itemlist = ((struct Layer *)layerlist->data)->items; itemlist!=NULL; itemlist = itemlist->next)
        update_text_item_displayfont((struct Item *)itemlist->data);
}

struct Item *click_is_in_text(struct Layer *layer, double x, double y)
{
  GList *itemlist;
  struct Item *item, *val;
  
  val = NULL;
  for (itemlist = layer->items; itemlist!=NULL; itemlist = itemlist->next) {
    item = (struct Item *)itemlist->data;
    if (item->type != ITEM_TEXT) continue;
    if (x<item->bbox.left || x>item->bbox.right) continue;
    if (y<item->bbox.top || y>item->bbox.bottom) continue;
    val = item;
  }
  return val;
}

struct Item *click_is_in_text_or_image(struct Layer *layer, double x, double y)
{
  GList *itemlist;
  struct Item *item, *val;
  
  val = NULL;
  for (itemlist = layer->items; itemlist!=NULL; itemlist = itemlist->next) {
    item = (struct Item *)itemlist->data;
    if (item->type != ITEM_TEXT && item->type != ITEM_IMAGE) continue;
    if (x<item->bbox.left || x>item->bbox.right) continue;
    if (y<item->bbox.top || y>item->bbox.bottom) continue;
    val = item;
  }
  return val;
}

void refont_text_item(struct Item *item, gchar *font_name, double font_size)
{
  if (!strcmp(font_name, item->font_name) && font_size==item->font_size) return;
  if (item->text!=NULL) {
    prepare_new_undo();
    undo->type = ITEM_TEXT_ATTRIB;
    undo->item = item;
    undo->str = item->font_name;
    undo->val_x = item->font_size;
    undo->brush = (struct Brush *)g_memdup(&(item->brush), sizeof(struct Brush));
  }
  else g_free(item->font_name);
  item->font_name = g_strdup(font_name);
  if (font_size>0.) item->font_size = font_size;
  update_text_item_displayfont(item);
}

void process_font_sel(gchar *str)
{
  gchar *p, *q;
  struct Item *it;
  gdouble size;
  GList *list;
  gboolean undo_cont;

  p = strrchr(str, ' ');
  if (p!=NULL) { 
    size = g_strtod(p+1, &q);
    if (*q!=0 || size<1.) size=0.;
    else *p=0;
  }
  else size=0.;
  g_free(ui.font_name);
  ui.font_name = str;  
  if (size>0.) ui.font_size = size;
  undo_cont = FALSE;   
  // if there's a current text item, re-font it
  if (ui.cur_item_type == ITEM_TEXT) {
    refont_text_item(ui.cur_item, str, size);
    undo_cont = (ui.cur_item->text!=NULL);   
  }
  // if there's a current selection, re-font it
  if (ui.selection!=NULL) 
    for (list=ui.selection->items; list!=NULL; list=list->next) {
      it = (struct Item *)list->data;
      if (it->type == ITEM_TEXT) {   
        if (undo_cont) undo->multiop |= MULTIOP_CONT_REDO;
        refont_text_item(it, str, size);
        if (undo_cont) undo->multiop |= MULTIOP_CONT_UNDO;
        undo_cont = TRUE;
      }
    }  
  update_font_button();
}
