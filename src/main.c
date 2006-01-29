#define ENABLE_XINPUT_BUGFIX  1
/* change the above to 0 if you are experiencing calibration problems with
   XInput and want to try things differently 
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>
#include <gtk/gtk.h>
#include <libgnomecanvas/libgnomecanvas.h>

#include "xournal.h"
#include "xo-interface.h"
#include "xo-support.h"
#include "xo-callbacks.h"
#include "xo-misc.h"
#include "xo-file.h"

GtkWidget *winMain;
GnomeCanvas *canvas;

struct Journal journal; // the journal
struct BgPdf bgpdf;  // the PDF loader stuff
struct UIData ui;   // the user interface data
struct UndoItem *undo, *redo; // the undo and redo stacks

void hide_unimplemented(void)
{
  gtk_widget_hide(GET_COMPONENT("filePrintOptions"));
  gtk_widget_hide(GET_COMPONENT("journalFlatten"));
  gtk_widget_hide(GET_COMPONENT("papercolorOther"));
  gtk_widget_hide(GET_COMPONENT("journalApplyAllPages"));
  gtk_widget_hide(GET_COMPONENT("toolsText"));
  gtk_widget_hide(GET_COMPONENT("buttonText"));
  gtk_widget_hide(GET_COMPONENT("toolsSelectRegion"));
  gtk_widget_hide(GET_COMPONENT("buttonSelectRegion"));
  gtk_widget_hide(GET_COMPONENT("colorOther"));
  gtk_widget_hide(GET_COMPONENT("toolsTextFont"));
  gtk_widget_hide(GET_COMPONENT("toolsDefaultText"));
  gtk_widget_hide(GET_COMPONENT("optionsSavePreferences"));
  gtk_widget_hide(GET_COMPONENT("helpIndex"));
}

void init_stuff (int argc, char *argv[])
{
  GtkWidget *w;
  GList *dev_list;
  GdkDevice *device;
  GdkScreen *screen;
  int i;
  struct Brush *b;
  gboolean can_xinput;

  // we need an empty canvas prior to creating the journal structures
  canvas = GNOME_CANVAS (gnome_canvas_new_aa ());

  // initialize data
  // TODO: load this from a preferences file

  ui.default_page.height = 792.0;
  ui.default_page.width = 612.0;
  ui.default_page.bg = g_new(struct Background, 1);
  ui.default_page.bg->type = BG_SOLID;
  ui.default_page.bg->color_no = COLOR_WHITE;
  ui.default_page.bg->color_rgba = predef_bgcolors_rgba[COLOR_WHITE];
  ui.default_page.bg->ruling = RULING_LINED;
  ui.default_page.bg->canvas_item = NULL;
  
  ui.zoom = DEFAULT_ZOOM;
  ui.view_continuous = TRUE;
  ui.fullscreen = FALSE;
  
  ui.allow_xinput = TRUE;
  ui.layerbox_length = 0;

  if (argc > 2 || (argc == 2 && argv[1][0] == '-')) {
    printf("Invalid command line parameters.\n"
           "Usage: %s [filename.xoj]\n", argv[0]);
    gtk_exit(0);
  }
   
  undo = NULL; redo = NULL;
  journal.pages = NULL;
  bgpdf.status = STATUS_NOT_INIT;
 
  if (argc == 1) new_journal();
  else if (!open_journal(argv[1])) {
    new_journal();
    w = gtk_message_dialog_new(GTK_WINDOW (winMain), GTK_DIALOG_DESTROY_WITH_PARENT,
       GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "Error opening file '%s'", argv[1]);
    gtk_dialog_run(GTK_DIALOG(w));
    gtk_widget_destroy(w);
  }
  
  ui.cur_item_type = ITEM_NONE;
  ui.cur_item = NULL;
  ui.cur_path.coords = NULL;
  ui.cur_path_storage_alloc = 0;
  ui.cur_path.ref_count = 1;
  ui.toolno = TOOL_PEN;
  ui.cur_brush = ui.brushes + TOOL_PEN;
  ui.ruler = FALSE;
  ui.selection = NULL;
  ui.cursor = NULL;

  ui.brushes[TOOL_PEN].color_no = COLOR_BLACK;
  ui.brushes[TOOL_ERASER].color_no = COLOR_WHITE;
  ui.brushes[TOOL_HIGHLIGHTER].color_no = COLOR_YELLOW;
  for (i=0; i < NUM_STROKE_TOOLS; i++) {
    b = ui.brushes + i;
    b->tool_type = i;
    b->color_rgba = predef_colors_rgba[b->color_no];
    if (i == TOOL_HIGHLIGHTER) {
      b->color_rgba &= HILITER_ALPHA_MASK;
    }
    b->thickness_no = THICKNESS_MEDIUM;
    b->thickness = predef_thickness[i][b->thickness_no];
    b->tool_options = 0;
    g_memmove(ui.default_brushes+i, ui.brushes+i, sizeof(struct Brush));
  }

  // initialize various interface elements
  
  gtk_window_set_default_size(GTK_WINDOW (winMain), 720, 480);
  update_toolbar_and_menu();
  // set up and initialize the canvas

  gtk_widget_show (GTK_WIDGET (canvas));
  w = GET_COMPONENT("scrolledwindowMain");
  gtk_container_add (GTK_CONTAINER (w), GTK_WIDGET (canvas));
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW (w), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_events (GTK_WIDGET (canvas), GDK_EXPOSURE_MASK | GDK_POINTER_MOTION_MASK | GDK_BUTTON_MOTION_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_KEY_PRESS_MASK | GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
  gtk_widget_set_extension_events(GTK_WIDGET (canvas), GDK_EXTENSION_EVENTS_ALL);
  gnome_canvas_set_pixels_per_unit (canvas, ui.zoom);
  gnome_canvas_set_center_scroll_region (canvas, TRUE);
  gtk_layout_get_hadjustment(GTK_LAYOUT (canvas))->step_increment = 30;
  gtk_layout_get_vadjustment(GTK_LAYOUT (canvas))->step_increment = 30;

  // set up the page size and canvas size
  update_page_stuff();

  g_signal_connect ((gpointer) canvas, "button_press_event",
                    G_CALLBACK (on_canvas_button_press_event),
                    NULL);
  g_signal_connect ((gpointer) canvas, "button_release_event",
                    G_CALLBACK (on_canvas_button_release_event),
                    NULL);
  g_signal_connect ((gpointer) canvas, "enter_notify_event",
                    G_CALLBACK (on_canvas_enter_notify_event),
                    NULL);
  g_signal_connect ((gpointer) canvas, "expose_event",
                    G_CALLBACK (on_canvas_expose_event),
                    NULL);
  g_signal_connect ((gpointer) canvas, "key_press_event",
                    G_CALLBACK (on_canvas_key_press_event),
                    NULL);
  g_signal_connect ((gpointer) canvas, "motion_notify_event",
                    G_CALLBACK (on_canvas_motion_notify_event),
                    NULL);
  g_signal_connect ((gpointer) gtk_layout_get_vadjustment(GTK_LAYOUT(canvas)),
                    "value-changed", G_CALLBACK (on_vscroll_changed),
                    NULL);
  g_object_set_data (G_OBJECT (winMain), "canvas", canvas);

  screen = gtk_widget_get_screen(winMain);
  ui.screen_width = gdk_screen_get_width(screen);
  ui.screen_height = gdk_screen_get_height(screen);
  
  ui.saved_toolno = -1;

  can_xinput = FALSE;
  dev_list = gdk_devices_list();
  while (dev_list != NULL) {
    device = (GdkDevice *)dev_list->data;
    if (device->source == GDK_SOURCE_PEN || device->source == GDK_SOURCE_ERASER) {
      /* get around a GDK bug: map the valuator range CORRECTLY to [0,1] */
#if ENABLE_XINPUT_BUGFIX
      gdk_device_set_axis_use(device, 0, GDK_AXIS_IGNORE);
      gdk_device_set_axis_use(device, 1, GDK_AXIS_IGNORE);
#endif
      gdk_device_set_mode(device, GDK_MODE_SCREEN);
      can_xinput = TRUE;
    }
    dev_list = dev_list->next;
  }
  if (!can_xinput)
    gtk_widget_set_sensitive(GET_COMPONENT("optionsUseXInput"), FALSE);

  ui.use_xinput = ui.allow_xinput && can_xinput;
  ui.antialias_bg = TRUE;
  ui.emulate_eraser = FALSE;
  ui.progressive_bg = TRUE;

  gtk_check_menu_item_set_active(
    GTK_CHECK_MENU_ITEM(GET_COMPONENT("optionsUseXInput")), ui.use_xinput);
  gtk_check_menu_item_set_active(
    GTK_CHECK_MENU_ITEM(GET_COMPONENT("optionsAntialiasBG")), ui.antialias_bg);
  gtk_check_menu_item_set_active(
    GTK_CHECK_MENU_ITEM(GET_COMPONENT("optionsProgressiveBG")), ui.progressive_bg);
  gtk_check_menu_item_set_active(
    GTK_CHECK_MENU_ITEM(GET_COMPONENT("optionsEmulateEraser")), ui.emulate_eraser);

  hide_unimplemented();
    
  update_undo_redo_enabled();
  update_copy_paste_enabled();
}


int
main (int argc, char *argv[])
{
  gchar *path, *path1, *path2;
  
  gtk_set_locale ();
  gtk_init (&argc, &argv);

  add_pixmap_directory (PACKAGE_DATA_DIR "/" PACKAGE "/pixmaps");
  path = g_path_get_dirname(argv[0]);
  path1 = g_build_filename(path, "pixmaps", NULL);
  path2 = g_build_filename(path, "..", "pixmaps", NULL);
  add_pixmap_directory (path1);
  add_pixmap_directory (path2);
  add_pixmap_directory (path);
  g_free(path);
  g_free(path1);
  g_free(path2);

  /*
   * The following code was added by Glade to create one of each component
   * (except popup menus), just so that you see something after building
   * the project. Delete any components that you don't want shown initially.
   */
  winMain = create_winMain ();
  
  init_stuff (argc, argv);
  
  gtk_widget_show (winMain);
  update_cursor();

  gtk_main ();
  
  if (bgpdf.status != STATUS_NOT_INIT) shutdown_bgpdf();
  if (bgpdf.status != STATUS_NOT_INIT) end_bgpdf_shutdown();
  
  return 0;
}

