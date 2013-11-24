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

// data manipulation misc functions

#ifndef __XO_MISC_H__
#define __XO_MISC_H__

struct Page *new_page(struct Page *template);
struct Page *new_page_with_bg(struct Background *bg, double width, double height);
void set_current_page(gdouble *pt);
void realloc_cur_path(int n);
void realloc_cur_widths(int n);
void clear_redo_stack(void);
void clear_undo_stack(void);
void prepare_new_undo(void);
void delete_journal(struct Journal *j);
void delete_page(struct Page *pg);
void delete_layer(struct Layer *l);

// referenced strings

struct Refstring *new_refstring(const char *s);
struct Refstring *refstring_ref(struct Refstring *rs);
void refstring_unref(struct Refstring *rs);

// helper functions

int finite_sized(double x);
void get_pointer_coords(GdkEvent *event, double *ret);
double get_pressure_multiplier(GdkEvent *event);
void fix_xinput_coords(GdkEvent *event);
void update_item_bbox(struct Item *item);
void make_page_clipbox(struct Page *pg);
void make_canvas_items(void);
void make_canvas_item_one(GooCanvasItem *group, struct Item *item);
void update_canvas_bg(struct Page *pg);
gboolean is_visible(struct Page *pg);
void rescale_bg_pixmaps(void);

gboolean have_intersect(struct BBox *a, struct BBox *b);
void lower_canvas_item_to(GooCanvasItem *g, GooCanvasItem *item, GooCanvasItem *after);

void xo_rgb_to_GdkColor(guint rgba, GdkColor *color);
guint32 xo_GdkColor_to_rgba(GdkColor gdkcolor, guint16 alpha) ;
void xo_rgba_to_GdkRGBA(guint rgba, GdkRGBA *color);
guint32 xo_GdkRGBA_to_rgba(GdkRGBA *gdkcolor);


// interface misc functions

void update_thickness_buttons(void);
void update_color_buttons(void);
void update_tool_buttons(void);
void update_tool_menu(void);
void update_ruler_indicator(void);
void update_color_menu(void);
void update_pen_props_menu(void);
void update_eraser_props_menu(void);
void update_highlighter_props_menu(void);
void update_mappings_menu_linkings(void);
void update_mappings_menu(void);
void update_page_stuff(void);
void update_toolbar_and_menu(void);
void update_file_name(char *filename);
void update_undo_redo_enabled(void);
void update_copy_paste_enabled(void);
void update_vbox_order(int *order);

gchar *make_cur_font_name(void);
void update_font_button(void);

void update_mapping_linkings(int toolno);
void do_switch_page(int pg, gboolean rescroll, gboolean refresh_all);
void set_cur_color(int color_no, guint color_rgba);
void recolor_temp_text(int color_no, guint color_rgba);
void process_color_activate(GtkMenuItem *menuitem, int color_no, guint color_rgba);
void process_thickness_activate(GtkMenuItem *menuitem, int tool, int val);
void process_papercolor_activate(GtkMenuItem *menuitem, int color, guint rgba);
void process_paperstyle_activate(GtkMenuItem *menuitem, int style);

gboolean ok_to_close(void);

void reset_focus(void);

// selection / clipboard stuff

void reset_selection(void);
void move_journal_items_by(GList *itemlist, double dx, double dy,
                           struct Layer *l1, struct Layer *l2, GList *depths);
void resize_journal_items_by(GList *itemlist, double scaling_x, double scaling_y,
                             double offset_x, double offset_y);


// switch between mappings

void switch_mapping(int m);
void process_mapping_activate(GtkMenuItem *menuitem, int m, int tool);

// always allow accels
void allow_all_accels(void);
gboolean can_accel(GtkWidget *widget, guint id, gpointer data);
void add_scroll_bindings(void);

gboolean is_event_within_textview(GdkEventButton *event);

void hide_unimplemented(void);

void do_fullscreen(gboolean active);

// fix GTK+ 2.16/2.17 issues with XInput events
gboolean filter_extended_events(GtkWidget *widget, GdkEventMotion *event, gpointer user_data);
// gboolean fix_extended_events(GtkWidget *widget, GdkEvent *event, gpointer user_data);

// help with focus
gboolean handle_activate_signal(GtkWidget *widget, gpointer user_data);
void unset_flags(GtkWidget *w, gpointer flag);
gboolean intercept_activate_events(GtkWidget *w, GdkEvent *ev, gpointer data);
void install_focus_hooks(GtkWidget *w, gpointer data);

// wrapper for a function no longer provided by poppler 0.17+
void
wrapper_poppler_page_render_to_pixbuf (PopplerPage *page,
			       int src_x, int src_y,
			       int src_width, int src_height,
			       double scale,
			       int rotation,
			       GdkPixbuf *pixbuf);

// defines for paper rulings

#define RULING_MARGIN_COLOR 0xff0080ff
#define RULING_COLOR 0x40a0ffff
#define RULING_COLOR_STR "0x40a0ffff"
#define RULING_THICKNESS 0.5
#define RULING_THICKNESS_STR "0.5"
#define RULING_LEFTMARGIN 72.0
#define RULING_TOPMARGIN 80.0
#define RULING_SPACING 24.0
#define RULING_BOTTOMMARGIN RULING_SPACING
#define RULING_GRAPHSPACING 14.17

void xo_unset_focus(GtkWidget *w, gpointer unused);

GdkDeviceManager *xo_device_manager_get(GdkWindow *window);
GList *xo_devices_list(GtkWidget *w);
GList *xo_gdkwindow_devices_list(GdkWindow *window);
gboolean xo_event_motion_device_is_core(GdkEventMotion  *event);
gboolean xo_event_button__device_is_core(GdkEventButton  *event);
gboolean xo_gtkwidget_device_is_core(GtkWidget *w, GdkDevice* device);

void xo_goo_canvas_item_show(GooCanvasItem *item);
void xo_goo_canvas_item_hide(GooCanvasItem *item);
void xo_goo_canvas_item_reposition(GooCanvasItem *item, gdouble x, gdouble y);

void xo_canvas_set_pixels_per_unit(void);

GdkPixbuf* xo_wrapper_poppler_page_render_to_pixbuf (PopplerPage *page,
						     int src_x, int src_y,
						     int src_width, int src_height,
						     double scale,
						     int rotation);



void xo_canvas_get_scroll_offsets_in_pixels(GooCanvas *canvas, gdouble *x, gdouble *y) ;
void xo_canvas_get_scroll_offsets_in_world(GooCanvas *canvas, gdouble *x, gdouble *y) ;

void xo_pointer_get_current_coords(double *ret);
void xo_canvas_item_resize(GooCanvasItem  *item, gdouble newWidth, gdouble newHeight, gboolean scaleToFit);


#endif
