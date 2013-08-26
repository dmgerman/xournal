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


#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "xo-gtk.h"
#include "xournal.h"
#include "xo-config.h"

GtkWidget *winMain;
GtkContainer   * canvasContainer;


guint xo_predef_bg_colors_rgba[XO_COLOR_MAX];
guint xo_predef_colors_rgba[XO_COLOR_MAX];

struct Refstring *new_refstring(const char *s)
{
  struct Refstring *rs = g_new(struct Refstring, 1);
  rs->nref = 1;
  if (s!=NULL) rs->s = g_strdup(s);
  else rs->s = NULL;
  rs->aux = NULL;
  return rs;
}

struct Refstring *refstring_ref(struct Refstring *rs)
{
  rs->nref++;
  return rs;
}

void refstring_unref(struct Refstring *rs)
{
  rs->nref--;
  if (rs->nref == 0) {
    if (rs->s!=NULL) g_free(rs->s);
    if (rs->aux!=NULL) g_free(rs->aux);
    g_free(rs);
  }
}


xo_page        default_page;
//xo_ui_data     ui;
xo_defaults    defaults;


xo_layer* xo_layer_new(void)
{
  xo_layer *l = g_new(xo_layer, 1);

  l->items = NULL;
  l->nitems = 0;

  return l;
}

int xo_page_layer_add(xo_page *page)
{
  page->layers = g_list_append(page->layers, xo_layer_new());
  page->nlayers++;
}

xo_background* xo_background_new(xo_background *template)
{
  xo_background* bg ;


  if (template == NULL) {
    bg = g_new0(xo_background, 1);
    // create from defaults
    bg->type = defaults.page_bg_type;
    bg->ruling = defaults.page_bg_ruling;
    bg->color_no = defaults.page_bg_color_no;
    bg->color_rgba = defaults.page_bg_color_rgba;
    bg->file_page_seq = 0;
    bg->pixbuf_scale = 1;

  } else {
    // use template
    // we copy the template to the new page. only what we need
    bg = (xo_background *)g_memdup(template, sizeof(*template));

    bg->type = template->type;
    bg->ruling = template->ruling;
    bg->color_no = template->color_no;
    bg->color_rgba = template->color_rgba;
    bg->file_domain = template->file_domain;
    bg->file_page_seq = template->file_page_seq;
    bg->pixbuf_scale = template->pixbuf_scale;
    bg->pixel_height = template->pixel_height;
    bg->pixel_width = template->pixel_width;
  }

  bg->canvas_item = NULL;

  if (bg->type == XO_BG_PIXMAP || bg->type == XO_BG_PDF) {
    g_object_ref(bg->pixbuf);
    refstring_ref(bg->filename);
  }
  return bg;
}

xo_page*   xo_page_new(xo_page *template)
{
  xo_page *pg = g_new0(xo_page, 1);

  if (template == NULL) {
    // create from defaults
    pg->height = defaults.page_height;
    pg->width = defaults.page_width;

    pg->bg = xo_background_new(NULL);

  } else {
    // use template
    // we copy the template to the new page. only what we need
    pg->height = template->height;
    pg->width = template->width;

    pg->bg = xo_background_new(template->bg);
  }
  xo_page_layer_add(pg);

  return pg;

}


xo_page*  xo_page_new_with_background(xo_background *bg, double width, double height)
{
  xo_page *pg = g_new(xo_page, 1);
  xo_page_layer_add(pg);
  pg->bg = bg;
  pg->width = width;
  pg->height = height;
  
  return pg;
}

void xo_journal_page_add(xo_journal* j)
{
  xo_page *pg = xo_page_new(j->templatePage);
  j->pages = g_list_append(j->pages, pg);
  j->npages++;
}

void xo_journal_page_template_init(xo_journal* j)
{
  assert(j->templatePage == NULL);
  j->templatePage = xo_page_new(NULL);
  assert(j->templatePage != NULL);
}

void xo_journal_ui_init(xo_journal* j, GtkWidget *mainWindow)
{
#ifdef asdfasdf
  j->uiCurrentPage = (xo_page *) j->pages->data;
  j->uiCurrentLayer = (xo_layer *) j->uiCurrentPage->layers->data;
#endif
  j->uiMainWindow = mainWindow;
}

void xo_journal_window_title_update(xo_journal*j)
{
  gchar tmp[101], *p;

  assert(j->uiMainWindow != NULL);
  if (j->filename == NULL) {
    gtk_window_set_title(GTK_WINDOW (j->uiMainWindow), _("Xournal"));
    return;
  }

  p = g_utf8_strrchr(j->filename, -1, '/');
  if (p == NULL) p = j->filename; 
  else p = g_utf8_next_char(p);

  g_snprintf(tmp, 100, _("Xournal - %s"), p);

  gtk_window_set_title(GTK_WINDOW (j->uiMainWindow), tmp);
}



void xo_journal_filename_set(xo_journal*j, gchar *filename)
{
  if (j->filename != NULL) g_free(j->filename);

  j->filename = filename;
  xo_journal_window_title_update(j);

  if (filename == NULL) 
    return;

  xo_recently_used_new(filename);
  if (filename[0]=='/') {
    xo_defaults_default_path_set(&defaults, g_path_get_dirname(filename));
  }

}


xo_journal* xo_journal_new(GtkWidget *mainWindow)
{
  xo_journal *j;

  j = g_new0(struct xo_journal, 1);
  j->magic = 0x1234;

  printf("Start template page\n");
  xo_journal_page_template_init(j);
  printf("Finished template page\n");

  xo_journal_page_add(j);
  xo_journal_ui_init(j, mainWindow);
  j->saved = TRUE;
  xo_journal_filename_set(j, NULL);
  return j;

}

/*
void xo_default_page_init(void)
{
  default_page.bg = g_new(xo_background, 1);
  default_page.height = 792.0;
  default_page.width = 612.0;
  default_page.bg->type = XO_BG_SOLID;
  default_page.bg->color_no = XO_COLOR_WHITE;
  default_page.bg->color_rgba = xo_predef_bg_colors_rgba[XO_COLOR_WHITE];
  default_page.bg->ruling = XO_RULING_LINED;
}
*/

void xo_undo_redo_init(void)
{
  printf("undo redo init to be implemented\n");
}


void xo_xournal_init(void)
{
  xo_journal *j;

  xo_config_init(&defaults);

  xo_config_load(defaults.configFileName, &defaults);


  j = xo_journal_new(winMain);

  printf("the magic number here [%x][%x]\n", j->magic, j);

  g_object_set_data ((GObject*)winMain, "journal", j);

#ifdef adfasdf
  ui.font_name = g_strdup(ui.default_font_name);
  ui.font_size = ui.default_font_size;
  ui.hiliter_alpha_mask = 0xffffff00 + (guint)(255*ui.hiliter_opacity);
  ui.layerbox_length = 0;
#endif

  // we need to create a journal


  xo_undo_redo_init();
}



void xo_interface_init(void) 
{
  GError *err = NULL;
  GtkBuilder *builder;
  GtkWidget *canvas;


  builder = gtk_builder_new();

  // Search for the glade file in the CWD, then in the installed data directory
  if(!gtk_builder_add_from_file(builder, "xournal.glade", &err)) {
    err = NULL;
    if(!gtk_builder_add_from_file(builder, PACKAGE_DATA_DIR "/" PACKAGE "/xournal.glade", &err)) {
      fprintf(stderr, "ERROR: %s\n", err->message);
      exit(1); // bail out
    }
  }

  winMain = (GtkWidget *)gtk_builder_get_object (builder, "winMain");
  gtk_builder_connect_signals (builder, NULL);


  canvasContainer = (GtkContainer *)gtk_builder_get_object (builder, "scrolledWindowMain");

  assert(canvasContainer != NULL);

  canvas = goo_canvas_new ();


  gtk_widget_set_size_request (canvas, 600, 450);
  goo_canvas_set_bounds (GOO_CANVAS (canvas), 0, 0, 1000, 1000);
  gtk_widget_show (canvas);
  gtk_container_add (GTK_CONTAINER (canvasContainer), canvas);

  //#################################

  GooCanvasItem *root, *rect_item, *text_item;
  root = goo_canvas_get_root_item (GOO_CANVAS (canvas));

  /* Add a few simple items. */
  rect_item = goo_canvas_rect_new (root, 100, 100, 400, 400,
                                   "line-width", 10.0,
                                   "radius-x", 20.0,
                                   "radius-y", 10.0,
                                   "stroke-color", "yellow",
                                   "fill-color", "red",
                                   NULL);

  text_item = goo_canvas_text_new (root, "Hello World", 300, 300, -1,
				   0,
                                   "font", "Sans 24",
                                   NULL);
  goo_canvas_item_rotate (text_item, 45, 300, 300);

  //#################################



  gtk_widget_show (winMain);

  g_object_unref (G_OBJECT (builder));


  // devices...

  GList *dev_list =  gdk_device_manager_list_devices (gdk_display_get_device_manager ( gdk_display_get_default()),
					       GDK_DEVICE_TYPE_MASTER); /// gdk_devices_list();


  while (dev_list != NULL) {
    GdkDevice* device = (GdkDevice *)dev_list->data;
    printf("->Device %s\n", gdk_device_get_name (device));
    dev_list = dev_list->next;
  }
  


}

void xo_init_stuff(int argc, char *argv[])
{
  GList *dev_list;
  GdkDevice *device;

  xo_interface_init();

  xo_xournal_init();

}



void  xo_defaults_update(void)
{
  GdkWindow *win = gtk_widget_get_parent_window(winMain);
  defaults.maximize_at_start = (gdk_window_get_state(win) & GDK_WINDOW_STATE_MAXIMIZED);
  if (!defaults.maximize_at_start && !defaults.fullscreen) {
    defaults.window_default_width = gdk_window_get_width(win);
    defaults.window_default_height = gdk_window_get_height(win);
  }
}

void shutdown(void)
{
#ifdef dmg
  if (bgpdf.status != STATUS_NOT_INIT) shutdown_bgpdf();
#endif

  xo_recently_used_save();
  if (defaults.auto_save_prefs) {
    xo_defaults_update();
    xo_config_save(&defaults);
  }
}
	     

int main (int argc, char *argv[])
{

#ifdef ENABLE_NLS
  bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);
#endif
  
  gtk_init (&argc, &argv);

  xo_init_stuff (argc, argv);
  
  gtk_main ();
  
  return 0;
}
