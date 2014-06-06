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
#include <libart_lgpl/art_vpath_dash.h>
#include <libart_lgpl/art_svp_point.h>
#include <libart_lgpl/art_svp_vpath.h>

#include "xournal.h"
#include "xo-callbacks.h"
#include "xo-interface.h"
#include "xo-support.h"
#include "xo-misc.h"
#include "xo-paint.h"
#include "xo-selection.h"

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
  
  if (ui.selection->items == NULL) {
    // if we clicked inside a text zone or image?  
    item = click_is_in_text_or_image(ui.selection->layer, x1, y1);
    if (item!=NULL && item==click_is_in_text_or_image(ui.selection->layer, x2, y2)) {
      ui.selection->items = g_list_append(ui.selection->items, item);
      g_memmove(&(ui.selection->bbox), &(item->bbox), sizeof(struct BBox));
      gnome_canvas_item_set(ui.selection->canvas_item,
        "x1", item->bbox.left, "x2", item->bbox.right, 
        "y1", item->bbox.top, "y2", item->bbox.bottom, NULL);
    }
  }
  
  if (ui.selection->items == NULL) reset_selection();
  else make_dashed(ui.selection->canvas_item);
  update_cursor();
  update_copy_paste_enabled();
  update_font_button();
}


void start_selectregion(GdkEvent *event)
{
  double pt[2];
  reset_selection();
  
  ui.cur_item_type = ITEM_SELECTREGION;
  ui.selection = g_new(struct Selection, 1);
  ui.selection->type = ITEM_SELECTREGION;
  ui.selection->items = NULL;
  ui.selection->layer = ui.cur_layer;

  get_pointer_coords(event, pt);
  ui.selection->bbox.left = ui.selection->bbox.right = pt[0];
  ui.selection->bbox.top = ui.selection->bbox.bottom = pt[1];
  
  realloc_cur_path(1);
  ui.cur_path.num_points = 1;
  ui.cur_path.coords[0] = ui.cur_path.coords[2] = pt[0];
  ui.cur_path.coords[1] = ui.cur_path.coords[3] = pt[1];
 
  ui.selection->canvas_item = gnome_canvas_item_new(ui.cur_layer->group,
      gnome_canvas_polygon_get_type(), "width-pixels", 1, 
      "outline-color-rgba", 0x000000ff,
      "fill-color-rgba", 0x80808040,
      NULL);
  make_dashed(ui.selection->canvas_item);
  update_cursor();
}

void continue_selectregion(GdkEvent *event)
{
  double *pt;
  
  realloc_cur_path(ui.cur_path.num_points+1);
  pt = ui.cur_path.coords + 2*ui.cur_path.num_points;
  get_pointer_coords(event, pt);
  if (hypot(pt[0]-pt[-2], pt[1]-pt[-1]) < PIXEL_MOTION_THRESHOLD/ui.zoom)
    return; // not a meaningful motion
  ui.cur_path.num_points++;
  if (ui.cur_path.num_points>2)
    gnome_canvas_item_set(ui.selection->canvas_item, 
     "points", &ui.cur_path, NULL);
}

/* check whether a point, resp. an item, is inside a lasso selection */

gboolean hittest_point(ArtSVP *lassosvp, double x, double y)
{
  return art_svp_point_wind(lassosvp, x, y)%2;
}

gboolean hittest_item(ArtSVP *lassosvp, struct Item *item)
{
  int i;
  
  if (item->type == ITEM_STROKE) {
    for (i=0; i<item->path->num_points; i++)
      if (!hittest_point(lassosvp, item->path->coords[2*i], item->path->coords[2*i+1])) 
        return FALSE;
    return TRUE;
  }
  else 
    return (hittest_point(lassosvp, item->bbox.left, item->bbox.top) &&
            hittest_point(lassosvp, item->bbox.right, item->bbox.top) &&
            hittest_point(lassosvp, item->bbox.left, item->bbox.bottom) &&
            hittest_point(lassosvp, item->bbox.right, item->bbox.bottom));
}

void finalize_selectregion(void)
{
  GList *itemlist;
  struct Item *item;
  ArtVpath *vpath;
  ArtSVP *lassosvp;
  int i, n;
  double *pt;
  
  ui.cur_item_type = ITEM_NONE;
  
  // build SVP for the lasso path
  n = ui.cur_path.num_points;
  vpath = g_malloc((n+2)*sizeof(ArtVpath));
  for (i=0; i<n; i++) { 
    vpath[i].x = ui.cur_path.coords[2*i];
    vpath[i].y = ui.cur_path.coords[2*i+1];
  }
  vpath[n].x = vpath[0].x; vpath[n].y = vpath[0].y;
  vpath[0].code = ART_MOVETO;
  for (i=1; i<=n; i++) vpath[i].code = ART_LINETO;
  vpath[n+1].code = ART_END;
  lassosvp = art_svp_from_vpath(vpath);
  g_free(vpath);

  // see which items we selected
  for (itemlist = ui.selection->layer->items; itemlist!=NULL; itemlist = itemlist->next) {
    item = (struct Item *)itemlist->data;
    if (hittest_item(lassosvp, item)) {
      // update the selection bbox
      if (ui.selection->items==NULL || ui.selection->bbox.left>item->bbox.left)
        ui.selection->bbox.left = item->bbox.left;
      if (ui.selection->items==NULL || ui.selection->bbox.right<item->bbox.right)
        ui.selection->bbox.right = item->bbox.right;
      if (ui.selection->items==NULL || ui.selection->bbox.top>item->bbox.top)
        ui.selection->bbox.top = item->bbox.top;
      if (ui.selection->items==NULL || ui.selection->bbox.bottom<item->bbox.bottom)
        ui.selection->bbox.bottom = item->bbox.bottom;
      // add the item
      ui.selection->items = g_list_append(ui.selection->items, item); 
    }
  }
  art_svp_free(lassosvp);

   // expand the bounding box by some amount (medium highlighter, or 3 pixels)
  if (ui.selection->items != NULL) {
    double margin = MAX(0.6*predef_thickness[TOOL_HIGHLIGHTER][THICKNESS_MEDIUM], 3.0/ui.zoom);
    ui.selection->bbox.top -= margin;
    ui.selection->bbox.bottom += margin;
    ui.selection->bbox.left -= margin;
    ui.selection->bbox.right += margin;
  }  

  if (ui.selection->items == NULL) {
    // if we clicked inside a text zone or image?
    pt = ui.cur_path.coords; 
    item = click_is_in_text_or_image(ui.selection->layer, pt[0], pt[1]);
    if (item!=NULL) {
      for (i=0; i<n; i++, pt+=2) {
        if (pt[0]<item->bbox.left || pt[0]>item->bbox.right || pt[1]<item->bbox.top || pt[1]>item->bbox.bottom)
          { item = NULL; break; }
      }
    }
    if (item!=NULL) {
      ui.selection->items = g_list_append(ui.selection->items, item);
      g_memmove(&(ui.selection->bbox), &(item->bbox), sizeof(struct BBox));
    }
  }

  if (ui.selection->items == NULL) reset_selection();
  else { // make a selection rectangle instead of the lasso shape
    gtk_object_destroy(GTK_OBJECT(ui.selection->canvas_item));
    ui.selection->canvas_item = gnome_canvas_item_new(ui.cur_layer->group,
      gnome_canvas_rect_get_type(), "width-pixels", 1, 
      "outline-color-rgba", 0x000000ff,
      "fill-color-rgba", 0x80808040,
      "x1", ui.selection->bbox.left, "x2", ui.selection->bbox.right, 
      "y1", ui.selection->bbox.top, "y2", ui.selection->bbox.bottom, NULL);
    make_dashed(ui.selection->canvas_item);
    ui.selection->type = ITEM_SELECTRECT;
  }

  update_cursor();
  update_copy_paste_enabled();
  update_font_button();
}


/*** moving/resizing the selection ***/

gboolean start_movesel(GdkEvent *event)
{
  double pt[2];
  
  if (ui.selection==NULL) return FALSE;
  if (ui.cur_layer != ui.selection->layer) return FALSE;
  
  get_pointer_coords(event, pt);
  if (ui.selection->type == ITEM_SELECTRECT || ui.selection->type == ITEM_SELECTREGION) {
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

gboolean start_resizesel(GdkEvent *event)
{
  double pt[2], resize_margin, hmargin, vmargin;

  if (ui.selection==NULL) return FALSE;
  if (ui.cur_layer != ui.selection->layer) return FALSE;

  get_pointer_coords(event, pt);

  if (ui.selection->type == ITEM_SELECTRECT || ui.selection->type == ITEM_SELECTREGION) {
    resize_margin = RESIZE_MARGIN/ui.zoom;
    hmargin = (ui.selection->bbox.right-ui.selection->bbox.left)*0.3;
    if (hmargin>resize_margin) hmargin = resize_margin;
    vmargin = (ui.selection->bbox.bottom-ui.selection->bbox.top)*0.3;
    if (vmargin>resize_margin) vmargin = resize_margin;

    // make sure the click is within a box slightly bigger than the selection rectangle
    if (pt[0]<ui.selection->bbox.left-resize_margin || 
        pt[0]>ui.selection->bbox.right+resize_margin ||
        pt[1]<ui.selection->bbox.top-resize_margin || 
        pt[1]>ui.selection->bbox.bottom+resize_margin)
      return FALSE;

    // now, if the click is near the edge, it's a resize operation
    // keep track of which edges we're close to, since those are the ones which should move
    ui.selection->resizing_left = (pt[0]<ui.selection->bbox.left+hmargin);
    ui.selection->resizing_right = (pt[0]>ui.selection->bbox.right-hmargin);
    ui.selection->resizing_top = (pt[1]<ui.selection->bbox.top+vmargin);
    ui.selection->resizing_bottom = (pt[1]>ui.selection->bbox.bottom-vmargin);

    // we're not near any edge, give up
    if (!(ui.selection->resizing_left || ui.selection->resizing_right ||
          ui.selection->resizing_top  || ui.selection->resizing_bottom)) 
      return FALSE;

    ui.cur_item_type = ITEM_RESIZESEL;
    ui.selection->new_y1 = ui.selection->bbox.top;
    ui.selection->new_y2 = ui.selection->bbox.bottom;
    ui.selection->new_x1 = ui.selection->bbox.left;
    ui.selection->new_x2 = ui.selection->bbox.right;
    gnome_canvas_item_set(ui.selection->canvas_item, "dash", NULL, NULL);
    update_cursor_for_resize(pt);
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
  if (ui.view_continuous == VIEW_MODE_CONTINUOUS) pt[1] += ui.selection->move_pagedelta;
  if (ui.view_continuous == VIEW_MODE_HORIZONTAL) pt[0] += ui.selection->move_pagedelta;
  if (ui.cur_item_type == ITEM_MOVESEL_VERT) pt[0] = 0;

  // check for page jumps
  if (ui.cur_item_type == ITEM_MOVESEL_VERT)
    upmargin = ui.selection->bbox.bottom - ui.selection->bbox.top;
  else upmargin = VIEW_CONTINUOUS_SKIP;
  tmppageno = ui.selection->move_pageno;
  tmppage = g_list_nth_data(journal.pages, tmppageno);
  if (ui.view_continuous == VIEW_MODE_CONTINUOUS) {
    while (pt[1] < - upmargin) {
      if (tmppageno == 0) break;
      tmppageno--;
      tmppage = g_list_nth_data(journal.pages, tmppageno);
      pt[1] += tmppage->height + VIEW_CONTINUOUS_SKIP;
      ui.selection->move_pagedelta += tmppage->height + VIEW_CONTINUOUS_SKIP;
    }
    while (pt[1] > tmppage->height+VIEW_CONTINUOUS_SKIP) {
      if (tmppageno == journal.npages-1) break;
      pt[1] -= tmppage->height + VIEW_CONTINUOUS_SKIP;
      ui.selection->move_pagedelta -= tmppage->height + VIEW_CONTINUOUS_SKIP;
      tmppageno++;
      tmppage = g_list_nth_data(journal.pages, tmppageno);
    }
  }
  if (ui.view_continuous == VIEW_MODE_HORIZONTAL) {
    while (pt[0] < -VIEW_CONTINUOUS_SKIP) {
      if (tmppageno == 0) break;
      tmppageno--;
      tmppage = g_list_nth_data(journal.pages, tmppageno);
      pt[0] += tmppage->width + VIEW_CONTINUOUS_SKIP;
      ui.selection->move_pagedelta += tmppage->width + VIEW_CONTINUOUS_SKIP;
    }
    while (pt[0] > tmppage->width+VIEW_CONTINUOUS_SKIP) {
      if (tmppageno == journal.npages-1) break;
      pt[0] -= tmppage->width + VIEW_CONTINUOUS_SKIP;
      ui.selection->move_pagedelta -= tmppage->width + VIEW_CONTINUOUS_SKIP;
      tmppageno++;
      tmppage = g_list_nth_data(journal.pages, tmppageno);
    }
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
            /* note: moving across pages for vert. space only works in vertical continuous mode */
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

void continue_resizesel(GdkEvent *event)
{
  double pt[2];

  get_pointer_coords(event, pt);

  if (ui.selection->resizing_top) ui.selection->new_y1 = pt[1];
  if (ui.selection->resizing_bottom) ui.selection->new_y2 = pt[1];
  if (ui.selection->resizing_left) ui.selection->new_x1 = pt[0];
  if (ui.selection->resizing_right) ui.selection->new_x2 = pt[0];

  gnome_canvas_item_set(ui.selection->canvas_item, 
    "x1", ui.selection->new_x1, "x2", ui.selection->new_x2,
    "y1", ui.selection->new_y1, "y2", ui.selection->new_y2, NULL);
}

void finalize_movesel(void)
{
  GList *list, *link;
  
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
    /* update selection box object's offset to be trivial, and its internal 
       coordinates to agree with those of the bbox; need this since resize
       operations will modify the box by setting its coordinates directly */
    gnome_canvas_item_affine_absolute(ui.selection->canvas_item, NULL);
    gnome_canvas_item_set(ui.selection->canvas_item, 
      "x1", ui.selection->bbox.left, "x2", ui.selection->bbox.right,
      "y1", ui.selection->bbox.top, "y2", ui.selection->bbox.bottom, NULL);
  }
  ui.cur_item_type = ITEM_NONE;
  update_cursor();
}

#define SCALING_EPSILON 0.001

void finalize_resizesel(void)
{
  struct Item *item;

  // build the affine transformation
  double offset_x, offset_y, scaling_x, scaling_y;
  scaling_x = (ui.selection->new_x2 - ui.selection->new_x1) / 
              (ui.selection->bbox.right - ui.selection->bbox.left);
  scaling_y = (ui.selection->new_y2 - ui.selection->new_y1) /
              (ui.selection->bbox.bottom - ui.selection->bbox.top);
  // couldn't undo a resize-by-zero...
  if (fabs(scaling_x)<SCALING_EPSILON) scaling_x = SCALING_EPSILON;
  if (fabs(scaling_y)<SCALING_EPSILON) scaling_y = SCALING_EPSILON;
  offset_x = ui.selection->new_x1 - ui.selection->bbox.left * scaling_x;
  offset_y = ui.selection->new_y1 - ui.selection->bbox.top * scaling_y;

  if (ui.selection->items != NULL) {
    // create the undo information
    prepare_new_undo();
    undo->type = ITEM_RESIZESEL;
    undo->itemlist = g_list_copy(ui.selection->items);
    undo->auxlist = NULL;

    undo->scaling_x = scaling_x;
    undo->scaling_y = scaling_y;
    undo->val_x = offset_x;
    undo->val_y = offset_y;

    // actually do the resize operation
    resize_journal_items_by(ui.selection->items, scaling_x, scaling_y, offset_x, offset_y);
  }

  if (scaling_x>0) {
    ui.selection->bbox.left = ui.selection->new_x1;
    ui.selection->bbox.right = ui.selection->new_x2;
  } else {
    ui.selection->bbox.left = ui.selection->new_x2;
    ui.selection->bbox.right = ui.selection->new_x1;
  }
  if (scaling_y>0) {
    ui.selection->bbox.top = ui.selection->new_y1;
    ui.selection->bbox.bottom = ui.selection->new_y2;
  } else {
    ui.selection->bbox.top = ui.selection->new_y2;
    ui.selection->bbox.bottom = ui.selection->new_y1;
  }
  make_dashed(ui.selection->canvas_item);

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

// modify the color or thickness of pen strokes in a selection

void recolor_selection(int color_no, guint color_rgba)
{
  GList *itemlist;
  struct Item *item;
  struct Brush *brush;
  GnomeCanvasGroup *group;
  
  if (ui.selection == NULL) return;
  prepare_new_undo();
  undo->type = ITEM_REPAINTSEL;
  undo->itemlist = NULL;
  undo->auxlist = NULL;
  for (itemlist = ui.selection->items; itemlist!=NULL; itemlist = itemlist->next) {
    item = (struct Item *)itemlist->data;
    if (item->type != ITEM_STROKE && item->type != ITEM_TEXT) continue;
    if (item->type == ITEM_STROKE && item->brush.tool_type!=TOOL_PEN) continue;
    // store info for undo
    undo->itemlist = g_list_append(undo->itemlist, item);
    brush = (struct Brush *)g_malloc(sizeof(struct Brush));
    g_memmove(brush, &(item->brush), sizeof(struct Brush));
    undo->auxlist = g_list_append(undo->auxlist, brush);
    // repaint the stroke
    item->brush.color_no = color_no;
    item->brush.color_rgba = color_rgba | 0xff; // no alpha
    if (item->canvas_item!=NULL) {
      if (!item->brush.variable_width)
        gnome_canvas_item_set(item->canvas_item, 
           "fill-color-rgba", item->brush.color_rgba, NULL);
      else {
        group = (GnomeCanvasGroup *) item->canvas_item->parent;
        gtk_object_destroy(GTK_OBJECT(item->canvas_item));
        make_canvas_item_one(group, item);
      }
    }
  }
}

void rethicken_selection(int val)
{
  GList *itemlist;
  struct Item *item;
  struct Brush *brush;
  GnomeCanvasGroup *group;
  
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
    if (item->canvas_item!=NULL) {
      if (!item->brush.variable_width)
        gnome_canvas_item_set(item->canvas_item, 
           "width-units", item->brush.thickness, NULL);
      else {
        group = (GnomeCanvasGroup *) item->canvas_item->parent;
        gtk_object_destroy(GTK_OBJECT(item->canvas_item));
        item->brush.variable_width = FALSE;
        make_canvas_item_one(group, item);
      }
    }
  }
}

