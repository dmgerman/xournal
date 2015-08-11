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


void set_cursor_busy(gboolean busy);
void update_cursor(void);
void update_cursor_for_resize(double *pt);

void create_new_stroke(GdkEvent *event);
void continue_stroke(GdkEvent *event);
void finalize_stroke(void);
void abort_stroke(void);
void subdivide_cur_path(void);

void do_eraser(GdkEvent *event, double radius, gboolean whole_strokes);
void finalize_erasure(void);

void do_hand(GdkEvent *event);

/* text functions */

#ifdef WIN32
#define DEFAULT_FONT "Arial"
#else
#define DEFAULT_FONT "Sans"
#endif
#define DEFAULT_FONT_SIZE 12

void start_text(GdkEvent *event, struct Item *item);
void end_text(void);
void update_text_item_displayfont(struct Item *item);
void rescale_text_items(void);
struct Item *click_is_in_text(struct Layer *layer, double x, double y);
struct Item *click_is_in_text_or_image(struct Layer *layer, double x, double y);
void refont_text_item(struct Item *item, gchar *font_name, double font_size);
void process_font_sel(gchar *str);
