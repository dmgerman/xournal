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

#ifndef XO_BOOKMARK_H

#define XO_BOOKMARK_H

#include "xournal.h"

// The collection of bookmarks for the entire Journal

extern GtkListStore * bookmark_liststore;

typedef enum {
  XO_BOOKMARK_COL_TITLE = 0,   // ghar *
  XO_BOOKMARK_COL_PAGENUM, // gint
  XO_BOOKMARK_COL_LAYER,   // gpointer to a Layer struct
  XO_BOOKMARK_COL_ITEM,    // gpointer to a Item struct
  XO_BOOKMARK_COL_COUNT
} t_bookmark_column;


/*
GtkListStore *xo_bookmarks_list_init(void);
void xo_bookmark_list_add(GtkListStore * list_store, Item * bookmark, const gchar * title, int page_num, Layer * layer);
Item * xo_bookmark_create(const gchar* title, double page_pos);
GnomeCanvasItem *xo_bookmark_canvas_create_item(Item * bookmark, GnomeCanvasGroup * group);
void xo_bookmark_delete(void);
void xo_bookmarks_end(Item * bookmark);
void xo_bookmarks_list_clear(GtkListStore * list_store);
gboolean xo_bookmark_list_find_entry(GtkListStore * list_store, Item * bookmark, GtkTreeIter * outIter);
*/

#define XO_BOOKMARK_PAGE_OFFSET_IN_POINTS 50

Item * xo_bookmark_create(const gchar* title, double page_pos);
void xo_bookmark_delete_selected(void);



#endif /* XO_BOOKMARK_H */
