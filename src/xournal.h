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

#include <gtk/gtk.h>
#include <libgnomecanvas/libgnomecanvas.h>
#include <poppler/glib/poppler.h>

// #define INPUT_DEBUG
/* uncomment this line if you experience event-processing problems
   and want to list the input events received by xournal. Caution, lots
   of output (redirect to a file). */

// #define ENABLE_XINPUT_BUGFIX
/* uncomment this line if you are experiencing calibration problems with
   XInput and want to try things differently. Especially useful on older
   distributions (up to around 2010). */

#define FILE_DIALOG_SIZE_BUGFIX
/* ugly, but should help users with versions of GTK+ that suffer from the
   "tiny file dialog" syndrome, without hurting those with well-behaved
   versions of GTK+. Comment out if you'd prefer not to include this fix. */

// PREF FILES INFO

#define CONFIG_DIR ".xournal"
#define MRU_FILE "recent-files"
#define MRU_SIZE 8 
#define CONFIG_FILE "config"

// apparently, not all Win32/64 compilers define WIN32 (?)

#ifdef _WIN32
#define WIN32
#endif

// version string for about box

#ifdef WIN32
#define VERSION_STRING VERSION "-win32"
#else
#define VERSION_STRING VERSION
#endif

// DATA STRUCTURES AND CONSTANTS

#define PIXEL_MOTION_THRESHOLD 0.3
#define MAX_AXES 12
#define EPSILON 1E-7
#define MAX_ZOOM 20.0
#define DISPLAY_DPI_DEFAULT 96.0
#define MIN_ZOOM 0.2
#define RESIZE_MARGIN 6.0
#define MAX_SAFE_RENDER_DPI 720 // max dpi at which PDF bg's get rendered

#define VBOX_MAIN_NITEMS 5 // number of interface items in vboxMain

// default touch device
#define DEFAULT_DEVICE_FOR_TOUCH "Touchscr"

/* a string (+ aux data) that maintains a refcount */

typedef struct Refstring {
  int nref;
  char *s;
  gpointer aux;
} Refstring;


/* The journal is mostly a list of pages. Each page is a list of layers,
   and a background. Each layer is a list of items, from bottom to top.
*/

typedef struct Background {
  int type;
  GnomeCanvasItem *canvas_item;
  int color_no;
  guint color_rgba;
  int ruling;
  GdkPixbuf *pixbuf;
  Refstring *filename;
  int file_domain;
  int file_page_seq;
  double pixbuf_scale; // for PIXMAP, this is the *current* zoom value
                       // for PDF, this is the *requested* zoom value
  int pixel_height, pixel_width; // PDF only: pixel size of current pixbuf
} Background;

#define BG_SOLID 0
#define BG_PIXMAP 1
#define BG_PDF 2

#define RULING_NONE 0
#define RULING_LINED 1
#define RULING_RULED 2
#define RULING_GRAPH 3

#define DOMAIN_ABSOLUTE 0
#define DOMAIN_ATTACH 1
#define DOMAIN_CLONE 2  // only while loading file

typedef struct Brush {
  int tool_type;
  int color_no;
  guint color_rgba;
  int thickness_no;
  double thickness;
  int tool_options;
  gboolean ruler, recognizer, variable_width;
} Brush;

#define COLOR_BLACK      0
#define COLOR_BLUE       1
#define COLOR_RED        2
#define COLOR_GREEN      3
#define COLOR_GRAY       4
#define COLOR_LIGHTBLUE  5
#define COLOR_LIGHTGREEN 6
#define COLOR_MAGENTA    7
#define COLOR_ORANGE     8
#define COLOR_YELLOW     9
#define COLOR_WHITE     10
#define COLOR_OTHER     -1
#define COLOR_MAX       11

extern guint predef_colors_rgba[COLOR_MAX];
extern guint predef_bgcolors_rgba[COLOR_MAX];

#define THICKNESS_VERYFINE  0
#define THICKNESS_FINE      1
#define THICKNESS_MEDIUM    2
#define THICKNESS_THICK     3
#define THICKNESS_VERYTHICK 4
#define THICKNESS_MAX       5

#define TOOL_PEN          0
#define TOOL_ERASER       1
#define TOOL_HIGHLIGHTER  2
#define TOOL_TEXT         3
#define TOOL_SELECTREGION 4
#define TOOL_SELECTRECT   5
#define TOOL_VERTSPACE    6
#define TOOL_HAND         7
#define TOOL_IMAGE        8
#define NUM_STROKE_TOOLS  3
#define NUM_TOOLS         9
#define NUM_BUTTONS       3

#define TOOLOPT_ERASER_STANDARD     0
#define TOOLOPT_ERASER_WHITEOUT     1
#define TOOLOPT_ERASER_STROKES      2

extern double predef_thickness[NUM_STROKE_TOOLS][THICKNESS_MAX];

typedef struct BBox {
  double left, right, top, bottom;
} BBox;

struct UndoErasureData;

typedef struct Item {
  int type;
  struct Brush brush; // the brush to use, if ITEM_STROKE
  // 'brush' also contains color info for text items
  GnomeCanvasPoints *path;
  gdouble *widths;
  GnomeCanvasItem *canvas_item; // the corresponding canvas item, or NULL
  struct BBox bbox;
  struct UndoErasureData *erasure; // for temporary use during erasures
  // the following fields for ITEM_TEXT:
  gchar *text;
  gchar *font_name;
  gdouble font_size;
  GtkWidget *widget; // the widget while text is being edited (ITEM_TEMP_TEXT)
  // the following fields for ITEM_IMAGE:
  GdkPixbuf *image;  // the image
  gchar *image_png;  // PNG of original image, for save and clipboard
  gsize image_png_len;
} Item;

// item type values for Item.type, UndoItem.type, ui.cur_item_type ...
// (not all are valid in all places)
#define ITEM_NONE -1
#define ITEM_STROKE 0
#define ITEM_TEMP_STROKE 1
#define ITEM_ERASURE 2
#define ITEM_SELECTRECT 3
#define ITEM_MOVESEL 4
#define ITEM_PASTE 5
#define ITEM_NEW_LAYER 6
#define ITEM_DELETE_LAYER 7
#define ITEM_NEW_BG_ONE 8
#define ITEM_NEW_BG_RESIZE 9
#define ITEM_PAPER_RESIZE 10
#define ITEM_NEW_DEFAULT_BG 11
#define ITEM_NEW_PAGE 13
#define ITEM_DELETE_PAGE 14
#define ITEM_REPAINTSEL 15
#define ITEM_MOVESEL_VERT 16
#define ITEM_HAND 17
#define ITEM_TEXT 18
#define ITEM_TEMP_TEXT 19
#define ITEM_TEXT_EDIT 20
#define ITEM_TEXT_ATTRIB 21
#define ITEM_RESIZESEL 22
#define ITEM_RECOGNIZER 23
#define ITEM_IMAGE 24
#define ITEM_SELECTREGION 25
#define ITEM_TEXT_PENDING 26 // will do when button release
#define ITEM_IMAGE_PENDING 27 // will do when button release

typedef struct Layer {
  GList *items; // the items on the layer, from bottom to top
  int nitems;
  GnomeCanvasGroup *group;
} Layer;

typedef struct Page {
  GList *layers; // the layers on the page
  int nlayers;
  double height, width;
  double hoffset, voffset; // offsets of canvas group rel. to canvas root
  struct Background *bg;
  GnomeCanvasGroup *group;
} Page;

typedef struct Journal {
  GList *pages;  // the pages in the journal
  int npages;
  int last_attach_no; // for naming of attached backgrounds
} Journal;

typedef struct Selection {
  int type;  // ITEM_SELECTRECT, ITEM_MOVESEL_VERT, ITEM_SELECTREGION
  BBox bbox; // the rectangle bbox of the selection
  struct Layer *layer; // the layer on which the selection lives
  double anchor_x, anchor_y, last_x, last_y; // for selection motion
  gboolean resizing_top, resizing_bottom, resizing_left, resizing_right; // for selection resizing
  double new_x1, new_x2, new_y1, new_y2; // for selection resizing
  GnomeCanvasItem *canvas_item; // if the selection box is on screen 
  GList *items; // the selected items (a list of struct Item)
  int move_pageno, orig_pageno; // if selection moves to a different page
  struct Layer *move_layer;
  float move_pagedelta;
} Selection;

typedef struct UIData {
  int pageno, layerno; // the current page and layer
  struct Page *cur_page;
  struct Layer *cur_layer;
  gboolean saved; // is file saved ?
  struct Brush *cur_brush;  // the brush in use (one of brushes[...])
  int toolno[NUM_BUTTONS+2];  // the number of the currently selected tool; two more reserved for eraser tip and touch device
  struct Brush brushes[NUM_BUTTONS+1][NUM_STROKE_TOOLS]; // the current pen, eraser, hiliter
  struct Brush default_brushes[NUM_STROKE_TOOLS]; // the default ones
  int linked_brush[NUM_BUTTONS+1]; // whether brushes are linked across buttons
  int cur_mapping; // the current button number for mappings
  gboolean button_switch_mapping; // button clicks switch button 1 mappings
  gboolean use_erasertip;
  gboolean touch_as_handtool; // always map touch device to hand tool?
  gboolean pen_disables_touch; // pen proximity should disable touch device?
  gboolean in_proximity;
  char *device_for_touch;
  int which_mouse_button; // the mouse button drawing the current path
  int which_unswitch_button; // if button_switch_mapping, the mouse button that switched the mapping
  struct Page default_page;  // the model for the default page
  int layerbox_length;  // the number of entries registered in the layers combo-box
  struct Item *cur_item; // the item being drawn, or NULL
  int cur_item_type;
  GnomeCanvasPoints cur_path; // the path being drawn
  gdouble *cur_widths; // width array for the path being drawn
  int cur_path_storage_alloc;
  int cur_widths_storage_alloc;
  double zoom; // zoom factor, in pixels per pt
  gboolean use_xinput; // use input devices instead of core pointer
  gboolean allow_xinput; // allow use of xinput ?
  gboolean discard_corepointer; // discard core pointer events in XInput mode
  gboolean pressure_sensitivity; // use pen pressure to control stroke width?
  double width_minimum_multiplier, width_maximum_multiplier; // calibration for pressure sensitivity
  gboolean is_corestroke; // this stroke is painted with core pointer
  gboolean saved_is_corestroke;
  GdkDevice *stroke_device; // who's painting this stroke
  gboolean ignore_other_devices;
  gboolean ignore_btn_reported_up; // config setting: ignore button reported up
  gboolean current_ignore_btn_reported_up;
  int screen_width, screen_height; // initial screen size, for XInput events
  double hand_refpt[2];
  int hand_scrollto_cx, hand_scrollto_cy;
  gboolean hand_scrollto_pending;
  char *filename;
  gchar *default_path; // default path for new notes
  gchar *default_image; // path for previous image
  gboolean fullscreen, maximize_at_start;
  int view_continuous; // view mode is continuous ?
  gboolean in_update_page_stuff; // semaphore to avoid scrollbar retroaction
  struct Selection *selection;
  GdkCursor *cursor;
  GdkPixbuf *pen_cursor_pix, *hiliter_cursor_pix;
  gboolean pen_cursor; // use pencil cursor (default is a dot in current color)
  gboolean progressive_bg; // update PDF bg's one at a time
  char *mrufile, *configfile; // file names for MRU & config
  char *mru[MRU_SIZE]; // MRU data
  GtkWidget *mrumenu[MRU_SIZE];
  gboolean bg_apply_all_pages;
  int window_default_width, window_default_height, scrollbar_step_increment;
  gboolean print_ruling; // print the paper ruling ?
  gboolean exportpdf_prefer_legacy; // prefer legacy code for export-to-pdf?
  gboolean exportpdf_layers; // export PDF one layer at a time
  gboolean new_page_bg_from_pdf; // do new pages get a duplicated PDF/image background?
  int default_unit; // the default unit for paper sizes
  int startuptool; // the default tool at startup
  int zoom_step_increment; // the increment in the zoom dialog box
  double zoom_step_factor; // the multiplicative factor in zoom in/out
  double startup_zoom;
  gboolean autoload_pdf_xoj;
  gboolean autocreate_new_xoj;
  gboolean autosave_enabled, autosave_loop_running, autosave_need_catchup;
  GList *autosave_filename_list;
  int autosave_delay;
  gboolean need_autosave;
#if GLIB_CHECK_VERSION(2,6,0)
  GKeyFile *config_data;
#endif
  int vertical_order[2][VBOX_MAIN_NITEMS]; // the order of interface components
  gchar *default_font_name, *font_name;
  gdouble default_font_size, font_size;
  gulong resize_signal_handler;
  gdouble hiliter_opacity;
  guint hiliter_alpha_mask;
  gboolean left_handed; // left-handed mode?
  gboolean auto_save_prefs; // auto-save preferences ?
  gboolean shorten_menus; // shorten menus ?
  gchar *shorten_menu_items; // which items to hide
  gboolean is_sel_cursor; // displaying a selection-related cursor
  gint pre_fullscreen_width, pre_fullscreen_height; // for win32 fullscreen
#if GTK_CHECK_VERSION(2,10,0)
  GtkPrintSettings *print_settings;
#endif
  gboolean poppler_force_cairo; // force poppler to use cairo
  gboolean warned_generate_fontconfig; // for win32 fontconfig cache
} UIData;

#define BRUSH_LINKED 0
#define BRUSH_COPIED 1
#define BRUSH_STATIC 2

typedef struct UndoErasureData {
  struct Item *item; // the item that got erased
  int npos; // its position in its layer
  int nrepl; // the number of replacement items
  GList *replacement_items;
} UndoErasureData;

typedef struct UndoItem {
  int type;
  struct Item *item; // for ITEM_STROKE, ITEM_TEXT, ITEM_TEXT_EDIT, ITEM_TEXT_ATTRIB, ITEM_IMAGE
  struct Layer *layer; // for ITEM_STROKE, ITEM_ERASURE, ITEM_PASTE, ITEM_NEW_LAYER, ITEM_DELETE_LAYER, ITEM_MOVESEL, ITEM_TEXT, ITEM_TEXT_EDIT, ITEM_RECOGNIZER, ITEM_IMAGE
  struct Layer *layer2; // for ITEM_DELETE_LAYER with val=-1, ITEM_MOVESEL
  struct Page *page;  // for ITEM_NEW_BG_ONE/RESIZE, ITEM_NEW_PAGE, ITEM_NEW_LAYER, ITEM_DELETE_LAYER, ITEM_DELETE_PAGE
  GList *erasurelist; // for ITEM_ERASURE, ITEM_RECOGNIZER
  GList *itemlist;  // for ITEM_MOVESEL, ITEM_PASTE, ITEM_REPAINTSEL, ITEM_RESIZESEL
  GList *auxlist;   // for ITEM_REPAINTSEL (brushes), ITEM_MOVESEL (depths)
  struct Background *bg;  // for ITEM_NEW_BG_ONE/RESIZE, ITEM_NEW_DEFAULT_BG
  int val; // for ITEM_NEW_PAGE, ITEM_NEW_LAYER, ITEM_DELETE_LAYER, ITEM_DELETE_PAGE
  double val_x, val_y; // for ITEM_MOVESEL, ITEM_NEW_BG_RESIZE, ITEM_PAPER_RESIZE, ITEM_NEW_DEFAULT_BG, ITEM_TEXT_ATTRIB, ITEM_RESIZESEL
  double scaling_x, scaling_y; // for ITEM_RESIZESEL
  gchar *str; // for ITEM_TEXT_EDIT, ITEM_TEXT_ATTRIB
  struct Brush *brush; // for ITEM_TEXT_ATTRIB
  struct UndoItem *next;
  int multiop;
} UndoItem;

#define MULTIOP_CONT_REDO 1 // not the last in a multiop, so keep redoing
#define MULTIOP_CONT_UNDO 2 // not the first in a multiop, so keep undoing


typedef struct BgPdfRequest {
  int pageno;
  double dpi;
} BgPdfRequest;

typedef struct BgPdfPage {
  double dpi;
  GdkPixbuf *pixbuf;
  int pixel_height, pixel_width; // pixel size of pixbuf
} BgPdfPage;

typedef struct BgPdf {
  int status; // the rest only makes sense if this is not STATUS_NOT_INIT
  guint pid; // the identifier of the idle callback
  Refstring *filename;
  int file_domain;
  gchar *file_contents; // buffer containing a copy of file data
  gsize file_length;  // size of above buffer
  int npages;
  GList *pages; // a list of BgPdfPage structures
  GList *requests; // a list of BgPdfRequest structures
  gboolean has_failed; // has failed in the past...
  PopplerDocument *document; // the poppler document
} BgPdf;

#define STATUS_NOT_INIT 0
#define STATUS_READY    1  // things are initialized and can work
// there used to be more possible values, things got streamlined...

// UTILITY MACROS

// getting a component of the interface by name
#define GET_COMPONENT(a)  GTK_WIDGET (g_object_get_data(G_OBJECT (winMain), a))

// the margin between consecutive pages in continuous view
#define VIEW_CONTINUOUS_SKIP 20.0

#define VIEW_MODE_ONE_PAGE 0 
#define VIEW_MODE_CONTINUOUS 1
#define VIEW_MODE_HORIZONTAL 2

// GLOBAL VARIABLES

// the main window and the canvas

extern GtkWidget *winMain;
extern GnomeCanvas *canvas;

// the data

extern struct Journal journal;
extern struct UIData ui;
extern struct BgPdf bgpdf;
extern struct UndoItem *undo, *redo;

extern double DEFAULT_ZOOM;

#define UNIT_CM 0
#define UNIT_IN 1
#define UNIT_PX 2
#define UNIT_PT 3
