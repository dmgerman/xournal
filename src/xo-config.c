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

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <goocanvas.h>

#include "xournal.h"

guint xo_predef_colors_rgba[XO_COLOR_MAX] =
  { 0x000000ff, 0x3333ccff, 0xff0000ff, 0x008000ff,
    0x808080ff, 0x00c0ffff, 0x00ff00ff, 0xff00ffff,
    0xff8000ff, 0xffff00ff, 0xffffffff };

guint xo_predef_bgcolors_rgba[XO_COLOR_MAX] = // meaningless ones set to white
  { 0xffffffff, 0xa0e8ffff, 0xffc0d4ff, 0x80ffc0ff,
    0xffffffff, 0xa0e8ffff, 0x80ffc0ff, 0xffc0d4ff,
    0xffc080ff, 0xffff80ff, 0xffffffff };


// config files are not reentrant and we only need one per application
static GKeyFile *configFile;

static const char *xo_vorder_usernames[XO_VBOX_MAIN_NITEMS+1] = 
  {"drawarea", "menu", "main_toolbar", "pen_toolbar", "statusbar", NULL};

static const char *xo_tool_names[XO_NUM_TOOLS] = {"pen", 
				     "eraser", 
				     "highlighter", 
				     "text", 
				     "selectregion", 
				     "selectrect", 
				     "vertspace", 
				     "hand", 
				     "image"};

static const char *xo_color_names[XO_COLOR_MAX] = {
  "black", "blue", "red", "green",
  "gray", "lightblue", "lightgreen", 
  "magenta", "orange", "yellow", "white"};

static const char *xo_bg_type_names[3] = {"solid", "pixmap", "pdf"};

static const char *xo_bg_color_names[XO_COLOR_MAX] = {"", "blue", "pink", "green",
   "", "", "", "", "orange", "yellow", "white"};
static const char *xo_bg_style_names[4] = {"plain", "lined", "ruled", "graph"};
static const char *xo_file_domain_names[3] = {"absolute", "attach", "clone"};
static const char *xo_unit_names[4] = {"cm", "in", "px", "pt"};

static double xo_predef_thickness[XO_NUM_STROKE_TOOLS][XO_THICKNESS_MAX] =
  { { 0.42, 0.85, 1.41,  2.26, 5.67 }, // pen thicknesses = 0.15, 0.3, 0.5, 0.8, 2 mm
    { 2.83, 2.83, 8.50, 19.84, 19.84 }, // eraser thicknesses = 1, 3, 7 mm
    { 2.83, 2.83, 8.50, 19.84, 19.84 }, // highlighter thicknesses = 1, 3, 7 mm
  };


static gboolean xo_keyval_parse_float(GKeyFile *configFile, const gchar *group, const gchar *key, double *val, double inf, double sup)
{
  gchar *ret, *end;
  double conv;
  
  ret = g_key_file_get_value(configFile, group, key, NULL);
  if (ret==NULL) return FALSE;
  conv = g_ascii_strtod(ret, &end);
  if (*end!=0) { g_free(ret); return FALSE; }
  g_free(ret);
  if (conv < inf || conv > sup) return FALSE;
  *val = conv;
  return TRUE;
}

static gboolean xo_keyval_parse_floatlist(GKeyFile *configFile, const gchar *group, const gchar *key, double *val, int n, double inf, double sup)
{
  gchar *ret, *end;
  double conv[5];
  int i;

  if (n>5) return FALSE;
  ret = g_key_file_get_value(configFile, group, key, NULL);
  if (ret==NULL) return FALSE;
  end = ret;
  for (i=0; i<n; i++) {
    conv[i] = g_ascii_strtod(end, &end);
    if ((i==n-1 && *end!=0) || (i<n-1 && *end!=';') ||
        (conv[i] < inf) || (conv[i] > sup)) { g_free(ret); return FALSE; }
    end++;
  }
  g_free(ret);
  for (i=0; i<n; i++) val[i] = conv[i];
  return TRUE;
}

static gboolean xo_keyval_parse_int(GKeyFile *configFile, const gchar *group, const gchar *key, int *val, int inf, int sup)
{
  gchar *ret, *end;
  int conv;
  
  ret = g_key_file_get_value(configFile, group, key, NULL);
  if (ret==NULL) return FALSE;
  conv = strtol(ret, &end, 10);
  if (*end!=0) { g_free(ret); return FALSE; }
  g_free(ret);
  if (conv < inf || conv > sup) return FALSE;
  *val = conv;
  return TRUE;
}

static gboolean xo_keyval_parse_enum(GKeyFile *configFile, const gchar *group, const gchar *key, int *val, const char **names, int n)
{
  gchar *ret;
  int i;
  
  ret = g_key_file_get_value(configFile, group, key, NULL);
  if (ret==NULL) return FALSE;
  for (i=0; i<n; i++) {
    if (!names[i][0]) continue; // "" is for invalid values
    if (!g_ascii_strcasecmp(ret, names[i]))
      { *val = i; g_free(ret); return TRUE; }
  }
  return FALSE;
}

gboolean xo_keyval_parse_enum_color(GKeyFile *configFile, const gchar *group, const gchar *key, int *val, guint *val_rgba, 
				    const char **names, const guint *predef_rgba, int n)
{
  gchar *ret;
  int i;
  
  ret = g_key_file_get_value(configFile, group, key, NULL);
  if (ret==NULL) return FALSE;
  for (i=0; i<n; i++) {
    if (!names[i][0]) continue; // "" is for invalid values
    if (!g_ascii_strcasecmp(ret, names[i]))
      { *val = i; *val_rgba = predef_rgba[i]; g_free(ret); return TRUE; }
  }
  if (ret[0]=='#') {
    *val = XO_COLOR_OTHER;
    *val_rgba = strtoul(ret+1, NULL, 16);
    g_free(ret);
    return TRUE;
  }
  return FALSE;
}

static gboolean xo_keyval_parse_boolean(GKeyFile *configFile, const gchar *group, const gchar *key, gboolean *val)
{
  gchar *ret;
  
  ret = g_key_file_get_value(configFile, group, key, NULL);
  if (ret==NULL) return FALSE;
  if (!g_ascii_strcasecmp(ret, "true")) 
    { *val = TRUE; g_free(ret); return TRUE; }
  if (!g_ascii_strcasecmp(ret, "false")) 
    { *val = FALSE; g_free(ret); return TRUE; }
  g_free(ret);
  return FALSE;
}

static gboolean xo_keyval_parse_string(GKeyFile *configFile, const gchar *group, const gchar *key, gchar **val)
{
  gchar *ret;
  
  ret = g_key_file_get_value(configFile, group, key, NULL);
  if (ret==NULL) return FALSE;
  if (strlen(ret) == 0) {
    *val = NULL;
    g_free(ret);
  } 
  else *val = ret;
  return TRUE;
}

static gboolean xo_keyval_parse_vorderlist(GKeyFile *configFile, const gchar *group, const gchar *key, int *order)
{
  gchar *ret, *p;
  int tmp[XO_VBOX_MAIN_NITEMS];
  int i, n, l;

  ret = g_key_file_get_value(configFile, group, key, NULL);
  if (ret==NULL) return FALSE;
  
  for (i=0; i<XO_VBOX_MAIN_NITEMS; i++) tmp[i] = -1;
  n = 0; p = ret;
  while (*p==' ') p++;
  while (*p!=0) {
    if (n>XO_VBOX_MAIN_NITEMS) return FALSE; // too many items
    for (i=0; i<XO_VBOX_MAIN_NITEMS; i++) {
      if (!g_str_has_prefix(p, xo_vorder_usernames[i])) continue;
      l = strlen(xo_vorder_usernames[i]);
      if (p[l]==' '||p[l]==0) { p+=l; break; }
    }
    if (i>=XO_VBOX_MAIN_NITEMS) { g_free(ret); return FALSE; } // parse error
    // we found item #i
    tmp[n++] = i;
    while (*p==' ') p++;
  }
  
  for (n=0; n<XO_VBOX_MAIN_NITEMS; n++) order[n] = tmp[n];
  g_free(ret);
  return TRUE;
}



void xo_config_load(gchar *configFileName, xo_defaults *defaults)
{

  double f;
  gboolean b;
  int i, j;
  gchar *str;

  // no support for keyval files before Glib 2.6.0
  if (glib_minor_version<6) return; 
  configFile = g_key_file_new();
  if (!g_key_file_load_from_file(configFile, 
				 configFileName,
				 G_KEY_FILE_KEEP_COMMENTS, NULL)) {
    g_key_file_free(configFile);
    configFile = g_key_file_new();
    g_key_file_set_comment(configFile, NULL, NULL, 
			   _(" Xournal configuration file.\n"
			     " This file is generated automatically upon saving preferences.\n"
			     " Use caution when editing this file manually.\n"),
			   NULL);
    return;
  }

  // parse keys from the keyfile to set defaults
  if (xo_keyval_parse_float(configFile,"general", "display_dpi", &f, 10., 500.))
    defaults->default_zoom = f/72.0;
  if (xo_keyval_parse_float(configFile,"general", "initial_zoom", &f, 
			 XO_MIN_ZOOM*100/defaults->default_zoom, 
			 XO_MAX_ZOOM*100/defaults->default_zoom))
    defaults->zoom = defaults->startup_zoom = defaults->default_zoom*f/100.0;


  xo_keyval_parse_boolean(configFile,"general", "window_maximize", &defaults->maximize_at_start);
  xo_keyval_parse_boolean(configFile,"general", "window_fullscreen", &defaults->fullscreen);

  xo_keyval_parse_int(configFile,"general", "window_width", &defaults->window_default_width, 10, 5000);
  xo_keyval_parse_int(configFile,"general", "window_height", &defaults->window_default_height, 10, 5000);
  xo_keyval_parse_int(configFile,"general", "scrollbar_speed", &defaults->scrollbar_step_increment, 1, 5000);
  xo_keyval_parse_int(configFile,"general", "zoom_dialog_increment", &defaults->zoom_step_increment, 1, 500);
  xo_keyval_parse_float(configFile,"general", "zoom_step_factor", &defaults->zoom_step_factor, 1., 5.);
  xo_keyval_parse_boolean(configFile,"general", "view_continuous", &defaults->view_continuous);
  xo_keyval_parse_boolean(configFile,"general", "use_xinput", &defaults->allow_xinput);
  xo_keyval_parse_boolean(configFile,"general", "discard_corepointer", &defaults->discard_corepointer);
  xo_keyval_parse_boolean(configFile,"general", "ignore_other_devices", &defaults->ignore_other_devices);
  xo_keyval_parse_boolean(configFile,"general", "use_erasertip", &defaults->use_erasertip);
  xo_keyval_parse_boolean(configFile,"general", "buttons_switch_mappings", &defaults->button_switch_mapping);
  xo_keyval_parse_boolean(configFile,"general", "autoload_pdf_xoj", &defaults->autoload_pdf_xoj);
  xo_keyval_parse_string(configFile,"general", "default_path", &defaults->default_path);
  xo_keyval_parse_boolean(configFile,"general", "pressure_sensitivity", &defaults->pressure_sensitivity);
  xo_keyval_parse_float(configFile,"general", "width_minimum_multiplier", &defaults->width_minimum_multiplier, 0., 10.);
  xo_keyval_parse_float(configFile,"general", "width_maximum_multiplier", &defaults->width_maximum_multiplier, 0., 10.);

  xo_keyval_parse_vorderlist(configFile,"general", "interface_order", defaults->vertical_order[0]);
  xo_keyval_parse_vorderlist(configFile,"general", "interface_fullscreen", defaults->vertical_order[1]);
  xo_keyval_parse_boolean(configFile,"general", "interface_lefthanded", &defaults->left_handed);
  xo_keyval_parse_boolean(configFile,"general", "shorten_menus", &defaults->shorten_menus);
  if (xo_keyval_parse_string(configFile,"general", "shorten_menu_items", &str) && str != NULL) {
    g_free(defaults->shorten_menu_items); 
    defaults->shorten_menu_items = str;
  }
  xo_keyval_parse_float(configFile,"general", "highlighter_opacity", &defaults->hiliter_opacity, 0., 1.);
  xo_keyval_parse_boolean(configFile,"general", "autosave_prefs", &defaults->auto_save_prefs);
  xo_keyval_parse_boolean(configFile,"general", "poppler_force_cairo", &defaults->poppler_force_cairo);
  
  xo_keyval_parse_float(configFile,"paper", "width", &defaults->page_width, 1., 5000.);
  xo_keyval_parse_float(configFile,"paper", "height", &defaults->page_height, 1., 5000.);
  xo_keyval_parse_enum_color(configFile,"paper", "color", 
     &(defaults->page_bg_color_no), &(defaults->page_bg_color_rgba), 
     xo_bg_color_names, xo_predef_bg_colors_rgba, XO_COLOR_MAX);
  xo_keyval_parse_enum(configFile,"paper", "style", &(defaults->page_bg_ruling), xo_bg_style_names, 4);


  xo_keyval_parse_boolean(configFile,"paper", "apply_all", &defaults->bg_apply_all_pages);
  xo_keyval_parse_enum(configFile,"paper", "default_unit", &defaults->default_unit, xo_unit_names, 4);

  xo_keyval_parse_boolean(configFile,"paper", "progressive_bg", &defaults->progressive_bg);
  xo_keyval_parse_boolean(configFile,"paper", "print_ruling", &defaults->print_ruling);

  xo_keyval_parse_int(configFile,"paper", "gs_bitmap_dpi", &defaults->gs_bitmap_dpi, 1, 1200);
  xo_keyval_parse_int(configFile,"paper", "pdftoppm_printing_dpi", &defaults->pdftoppm_printing_dpi, 1, 1200);


  xo_keyval_parse_enum(configFile,"tools", "startup_tool", &defaults->startuptool, xo_tool_names, XO_NUM_TOOLS);
  defaults->toolno[0] = defaults->startuptool;
  xo_keyval_parse_boolean(configFile,"tools", "pen_cursor", &defaults->pen_cursor);
  xo_keyval_parse_enum_color(configFile,"tools", "pen_color", 
     &(defaults->brushes[0][XO_TOOL_PEN].color_no), &(defaults->brushes[0][XO_TOOL_PEN].color_rgba),
     xo_color_names, xo_predef_colors_rgba, XO_COLOR_MAX);
  xo_keyval_parse_int(configFile,"tools", "pen_thickness", &(defaults->brushes[0][XO_TOOL_PEN].thickness_no), 0, 4);
  xo_keyval_parse_boolean(configFile,"tools", "pen_ruler", &(defaults->brushes[0][XO_TOOL_PEN].ruler));
  xo_keyval_parse_boolean(configFile,"tools", "pen_recognizer", &(defaults->brushes[0][XO_TOOL_PEN].recognizer));
  xo_keyval_parse_int(configFile,"tools", "eraser_thickness", &(defaults->brushes[0][XO_TOOL_ERASER].thickness_no), 1, 3);
  xo_keyval_parse_int(configFile,"tools", "eraser_mode", &(defaults->brushes[0][XO_TOOL_ERASER].tool_options), 0, 2);
  xo_keyval_parse_enum_color(configFile,"tools", "highlighter_color", 
     &(defaults->brushes[0][XO_TOOL_HIGHLIGHTER].color_no), &(defaults->brushes[0][XO_TOOL_HIGHLIGHTER].color_rgba),
     xo_color_names, xo_predef_colors_rgba, XO_COLOR_MAX);
  xo_keyval_parse_int(configFile,"tools", "highlighter_thickness", &(defaults->brushes[0][XO_TOOL_HIGHLIGHTER].thickness_no), 0, 4);
  xo_keyval_parse_boolean(configFile,"tools", "highlighter_ruler", &(defaults->brushes[0][XO_TOOL_HIGHLIGHTER].ruler));
  xo_keyval_parse_boolean(configFile,"tools", "highlighter_recognizer", &(defaults->brushes[0][XO_TOOL_HIGHLIGHTER].recognizer));
  defaults->brushes[0][XO_TOOL_PEN].variable_width = defaults->pressure_sensitivity;
  for (i=0; i< XO_NUM_STROKE_TOOLS; i++)
    for (j=1; j<=XO_NUM_BUTTONS; j++)
      g_memmove(&(defaults->brushes[j][i]), &(defaults->brushes[0][i]), sizeof(xo_brush));

  xo_keyval_parse_enum(configFile,"tools", "btn2_tool", &(defaults->toolno[1]), xo_tool_names, XO_NUM_TOOLS);
  if (xo_keyval_parse_boolean(configFile,"tools", "btn2_linked", &b))
    defaults->linked_brush[1] = b?XO_BRUSH_LINKED:XO_BRUSH_STATIC;
  xo_keyval_parse_enum(configFile,"tools", "btn3_tool", &(defaults->toolno[2]), xo_tool_names, XO_NUM_TOOLS);
  if (xo_keyval_parse_boolean(configFile,"tools", "btn3_linked", &b))
    defaults->linked_brush[2] = b?XO_BRUSH_LINKED:XO_BRUSH_STATIC;
  if (defaults->linked_brush[1]!=XO_BRUSH_LINKED) {
    if (defaults->toolno[1]==XO_TOOL_PEN || defaults->toolno[1]==XO_TOOL_HIGHLIGHTER) {
      xo_keyval_parse_boolean(configFile,"tools", "btn2_ruler", &(defaults->brushes[1][defaults->toolno[1]].ruler));
      xo_keyval_parse_boolean(configFile,"tools", "btn2_recognizer", &(defaults->brushes[1][defaults->toolno[1]].recognizer));
      xo_keyval_parse_enum_color(configFile,"tools", "btn2_color", 
         &(defaults->brushes[1][defaults->toolno[1]].color_no), &(defaults->brushes[1][defaults->toolno[1]].color_rgba), 
         xo_color_names, xo_predef_colors_rgba, XO_COLOR_MAX);
    }
    if (defaults->toolno[1]<XO_NUM_STROKE_TOOLS)
      xo_keyval_parse_int(configFile,"tools", "btn2_thickness", &(defaults->brushes[1][defaults->toolno[1]].thickness_no), 0, 4);
    if (defaults->toolno[1]==XO_TOOL_ERASER)
      xo_keyval_parse_int(configFile,"tools", "btn2_erasermode", &(defaults->brushes[1][XO_TOOL_ERASER].tool_options), 0, 2);
  }
  if (defaults->linked_brush[2]!=XO_BRUSH_LINKED) {
    if (defaults->toolno[2]==XO_TOOL_PEN || defaults->toolno[2]==XO_TOOL_HIGHLIGHTER) {
      xo_keyval_parse_boolean(configFile,"tools", "btn3_ruler", &(defaults->brushes[2][defaults->toolno[2]].ruler));
      xo_keyval_parse_boolean(configFile,"tools", "btn3_recognizer", &(defaults->brushes[2][defaults->toolno[2]].recognizer));
      xo_keyval_parse_enum_color(configFile,"tools", "btn3_color", 
         &(defaults->brushes[2][defaults->toolno[2]].color_no), &(defaults->brushes[2][defaults->toolno[2]].color_rgba), 
         xo_color_names, xo_predef_colors_rgba, XO_COLOR_MAX);
    }
    if (defaults->toolno[2]<XO_NUM_STROKE_TOOLS)
      xo_keyval_parse_int(configFile,"tools", "btn3_thickness", &(defaults->brushes[2][defaults->toolno[2]].thickness_no), 0, 4);
    if (defaults->toolno[2]==XO_TOOL_ERASER)
      xo_keyval_parse_int(configFile,"tools", "btn3_erasermode", &(defaults->brushes[2][XO_TOOL_ERASER].tool_options), 0, 2);
  }
  xo_keyval_parse_floatlist(configFile,"tools", "pen_thicknesses", xo_predef_thickness[XO_TOOL_PEN], 5, 0.01, 1000.0);
  xo_keyval_parse_floatlist(configFile,"tools", "eraser_thicknesses", xo_predef_thickness[XO_TOOL_ERASER]+1, 3, 0.01, 1000.0);
  xo_keyval_parse_floatlist(configFile,"tools", "highlighter_thicknesses", xo_predef_thickness[XO_TOOL_HIGHLIGHTER]+1, 3, 0.01, 1000.0);
  if (xo_keyval_parse_string(configFile,"tools", "default_font", &str))
    if (str!=NULL) { g_free(defaults->default_font_name); defaults->default_font_name = str; }
  xo_keyval_parse_float(configFile,"tools", "default_font_size", &defaults->default_font_size, 1., 200.);
}


static void xo_cleanup_numeric(char *s)
{
  while (*s!=0) { 
    if (*s==',') *s='.'; 
    if (*s=='1' && s[1]=='.' && s[2]=='#' && s[3]=='J') 
      { *s='i'; s[1]='n'; s[2]='f'; s[3]=' '; }
    s++; 
  }
}

static gchar *xo_verbose_vertical_order(int *order)
{
  gchar buf[80], *p; // longer than needed
  int i;

  p = buf;  
  for (i=0; i<XO_VBOX_MAIN_NITEMS; i++) {
    if (order[i]<0 || order[i]>=XO_VBOX_MAIN_NITEMS) continue;
    if (p!=buf) *(p++) = ' ';
    p = g_stpcpy(p, xo_vorder_usernames[order[i]]);
  }
  return g_strdup(buf);
}



static void xo_keyval_update(GKeyFile *configFile, const gchar *group_name, const gchar *key,
                const gchar *comment, gchar *value)
{
  gboolean has_it = g_key_file_has_key(configFile, group_name, key, NULL);
  xo_cleanup_numeric(value);
  g_key_file_set_value(configFile, group_name, key, value);
  g_free(value);
  if (!has_it) g_key_file_set_comment(configFile, group_name, key, comment, NULL);
}


void xo_config_save(xo_defaults *defaults)
{
  gchar *buf;
  FILE *f;

#if GLIB_CHECK_VERSION(2,6,0)
  // no support for keyval files before Glib 2.6.0
  if (glib_minor_version<6) return; 

  xo_keyval_update(configFile, "general", "display_dpi",
    _(" the display resolution, in pixels per inch"),
    g_strdup_printf("%.2f", defaults->default_zoom*72));
  xo_keyval_update(configFile, "general", "initial_zoom",
    _(" the initial zoom level, in percent"),
    g_strdup_printf("%.2f", 100*defaults->zoom/defaults->default_zoom));
  xo_keyval_update(configFile, "general", "window_maximize",
    _(" maximize the window at startup (true/false)"),
    g_strdup(defaults->maximize_at_start?"true":"false"));
  xo_keyval_update(configFile, "general", "window_fullscreen",
    _(" start in full screen mode (true/false)"),
    g_strdup(defaults->fullscreen?"true":"false"));
  xo_keyval_update(configFile, "general", "window_width",
    _(" the window width in pixels (when not maximized)"),
    g_strdup_printf("%d", defaults->window_default_width));
  xo_keyval_update(configFile, "general", "window_height",
    _(" the window height in pixels"),
    g_strdup_printf("%d", defaults->window_default_height));
  xo_keyval_update(configFile, "general", "scrollbar_speed",
    _(" scrollbar step increment (in pixels)"),
    g_strdup_printf("%d", defaults->scrollbar_step_increment));
  xo_keyval_update(configFile, "general", "zoom_dialog_increment",
    _(" the step increment in the zoom dialog box"),
    g_strdup_printf("%d", defaults->zoom_step_increment));
  xo_keyval_update(configFile, "general", "zoom_step_factor",
    _(" the multiplicative factor for zoom in/out"),
    g_strdup_printf("%.3f", defaults->zoom_step_factor));
  xo_keyval_update(configFile, "general", "view_continuous",
    _(" document view (true = continuous, false = single page)"),
    g_strdup(defaults->view_continuous?"true":"false"));
  xo_keyval_update(configFile, "general", "use_xinput",
    _(" use XInput extensions (true/false)"),
    g_strdup(defaults->allow_xinput?"true":"false"));
  xo_keyval_update(configFile, "general", "discard_corepointer",
    _(" discard Core Pointer events in XInput mode (true/false)"),
    g_strdup(defaults->discard_corepointer?"true":"false"));
  xo_keyval_update(configFile, "general", "ignore_other_devices",
    _(" ignore events from other devices while drawing (true/false)"),
    g_strdup(defaults->ignore_other_devices?"true":"false"));
  xo_keyval_update(configFile, "general", "use_erasertip",
    _(" always map eraser tip to eraser (true/false)"),
    g_strdup(defaults->use_erasertip?"true":"false"));
  xo_keyval_update(configFile, "general", "buttons_switch_mappings",
    _(" buttons 2 and 3 switch mappings instead of drawing (useful for some tablets) (true/false)"),
    g_strdup(defaults->button_switch_mapping?"true":"false"));
  xo_keyval_update(configFile, "general", "autoload_pdf_xoj",
    _(" automatically load filename.pdf.xoj instead of filename.pdf (true/false)"),
    g_strdup(defaults->autoload_pdf_xoj?"true":"false"));
  xo_keyval_update(configFile, "general", "default_path",
    _(" default path for open/save (leave blank for current directory)"),
    g_strdup((defaults->default_path!=NULL)?defaults->default_path:""));
  xo_keyval_update(configFile, "general", "pressure_sensitivity",
     _(" use pressure sensitivity to control pen stroke width (true/false)"),
     g_strdup(defaults->pressure_sensitivity?"true":"false"));
  xo_keyval_update(configFile, "general", "width_minimum_multiplier",
     _(" minimum width multiplier"),
     g_strdup_printf("%.2f", defaults->width_minimum_multiplier));
  xo_keyval_update(configFile, "general", "width_maximum_multiplier",
     _(" maximum width multiplier"),
     g_strdup_printf("%.2f", defaults->width_maximum_multiplier));
  xo_keyval_update(configFile, "general", "interface_order",
    _(" interface components from top to bottom\n valid values: drawarea menu main_toolbar pen_toolbar statusbar"),
    xo_verbose_vertical_order(defaults->vertical_order[0]));
  xo_keyval_update(configFile, "general", "interface_fullscreen",
    _(" interface components in fullscreen mode, from top to bottom"),
    xo_verbose_vertical_order(defaults->vertical_order[1]));
  xo_keyval_update(configFile, "general", "interface_lefthanded",
    _(" interface has left-handed scrollbar (true/false)"),
    g_strdup(defaults->left_handed?"true":"false"));
  xo_keyval_update(configFile, "general", "shorten_menus",
    _(" hide some unwanted menu or toolbar items (true/false)"),
    g_strdup(defaults->shorten_menus?"true":"false"));
  xo_keyval_update(configFile, "general", "shorten_menu_items",
    _(" interface items to hide (customize at your own risk!)\n see source file xo-interface.c for a list of item names"),
    g_strdup(defaults->shorten_menu_items));
  xo_keyval_update(configFile, "general", "highlighter_opacity",
    _(" highlighter opacity (0 to 1, default 0.5)\n warning: opacity level is not saved in xoj files!"),
    g_strdup_printf("%.2f", defaults->hiliter_opacity));
  xo_keyval_update(configFile, "general", "autosave_prefs",
    _(" auto-save preferences on exit (true/false)"),
    g_strdup(defaults->auto_save_prefs?"true":"false"));
  xo_keyval_update(configFile, "general", "poppler_force_cairo",
    _(" force PDF rendering through cairo (slower but nicer) (true/false)"),
    g_strdup(defaults->poppler_force_cairo?"true":"false"));

  xo_keyval_update(configFile, "paper", "width",
    _(" the default page width, in points (1/72 in)"),
    g_strdup_printf("%.2f", defaults->page_width));
  xo_keyval_update(configFile, "paper", "height",
    _(" the default page height, in points (1/72 in)"),
    g_strdup_printf("%.2f", defaults->page_height));
  xo_keyval_update(configFile, "paper", "color",
    _(" the default paper color"),
    (defaults->page_bg_color_no>=0)?
    g_strdup(xo_bg_color_names[defaults->page_bg_color_no]):
    g_strdup_printf("#%08x", defaults->page_bg_color_rgba));
  xo_keyval_update(configFile, "paper", "style",
    _(" the default paper style (plain, lined, ruled, or graph)"),
    g_strdup(xo_bg_style_names[defaults->page_bg_ruling]));
  xo_keyval_update(configFile, "paper", "apply_all",
    _(" apply paper style changes to all pages (true/false)"),
    g_strdup(defaults->bg_apply_all_pages?"true":"false"));
  xo_keyval_update(configFile, "paper", "default_unit",
    _(" preferred unit (cm, in, px, pt)"),
    g_strdup(xo_unit_names[defaults->default_unit]));
  xo_keyval_update(configFile, "paper", "print_ruling",
    _(" include paper ruling when printing or exporting to PDF (true/false)"),
    g_strdup(defaults->print_ruling?"true":"false"));
  xo_keyval_update(configFile, "paper", "progressive_bg",
    _(" just-in-time update of page backgrounds (true/false)"),
    g_strdup(defaults->progressive_bg?"true":"false"));
  xo_keyval_update(configFile, "paper", "gs_bitmap_dpi",
    _(" bitmap resolution of PS/PDF backgrounds rendered using ghostscript (dpi)"),
    g_strdup_printf("%d", defaults->gs_bitmap_dpi));
  xo_keyval_update(configFile, "paper", "pdftoppm_printing_dpi",
    _(" bitmap resolution of PDF backgrounds when printing with libgnomeprint (dpi)"),
    g_strdup_printf("%d", defaults->pdftoppm_printing_dpi));

  xo_keyval_update(configFile, "tools", "startup_tool",
    _(" selected tool at startup (pen, eraser, highlighter, selectregion, selectrect, vertspace, hand, image)"),
    g_strdup(xo_tool_names[defaults->startuptool]));
  xo_keyval_update(configFile, "tools", "pen_cursor",
    _(" Use the pencil from cursor theme instead of a color dot (true/false)"),
    g_strdup(defaults->pen_cursor?"true":"false"));
  xo_keyval_update(configFile, "tools", "pen_color",
    _(" default pen color"),
    (defaults->default_brushes[XO_TOOL_PEN].color_no>=0)?
    g_strdup(xo_color_names[defaults->default_brushes[XO_TOOL_PEN].color_no]):
    g_strdup_printf("#%08x", defaults->default_brushes[XO_TOOL_PEN].color_rgba));
  xo_keyval_update(configFile, "tools", "pen_thickness",
    _(" default pen thickness (fine = 1, medium = 2, thick = 3)"),
    g_strdup_printf("%d", defaults->default_brushes[XO_TOOL_PEN].thickness_no));
  xo_keyval_update(configFile, "tools", "pen_ruler",
    _(" default pen is in ruler mode (true/false)"),
    g_strdup(defaults->default_brushes[XO_TOOL_PEN].ruler?"true":"false"));
  xo_keyval_update(configFile, "tools", "pen_recognizer",
    _(" default pen is in shape recognizer mode (true/false)"),
    g_strdup(defaults->default_brushes[XO_TOOL_PEN].recognizer?"true":"false"));
  xo_keyval_update(configFile, "tools", "eraser_thickness",
    _(" default eraser thickness (fine = 1, medium = 2, thick = 3)"),
    g_strdup_printf("%d", defaults->default_brushes[XO_TOOL_ERASER].thickness_no));
  xo_keyval_update(configFile, "tools", "eraser_mode",
    _(" default eraser mode (standard = 0, whiteout = 1, strokes = 2)"),
    g_strdup_printf("%d", defaults->default_brushes[XO_TOOL_ERASER].tool_options));
  xo_keyval_update(configFile, "tools", "highlighter_color",
    _(" default highlighter color"),
    (defaults->default_brushes[XO_TOOL_HIGHLIGHTER].color_no>=0)?
    g_strdup(xo_color_names[defaults->default_brushes[XO_TOOL_HIGHLIGHTER].color_no]):
    g_strdup_printf("#%08x", defaults->default_brushes[XO_TOOL_HIGHLIGHTER].color_rgba));
  xo_keyval_update(configFile, "tools", "highlighter_thickness",
    _(" default highlighter thickness (fine = 1, medium = 2, thick = 3)"),
    g_strdup_printf("%d", defaults->default_brushes[XO_TOOL_HIGHLIGHTER].thickness_no));
  xo_keyval_update(configFile, "tools", "highlighter_ruler",
    _(" default highlighter is in ruler mode (true/false)"),
    g_strdup(defaults->default_brushes[XO_TOOL_HIGHLIGHTER].ruler?"true":"false"));
  xo_keyval_update(configFile, "tools", "highlighter_recognizer",
    _(" default highlighter is in shape recognizer mode (true/false)"),
    g_strdup(defaults->default_brushes[XO_TOOL_HIGHLIGHTER].recognizer?"true":"false"));
  xo_keyval_update(configFile, "tools", "btn2_tool",
    _(" button 2 tool (pen, eraser, highlighter, text, selectregion, selectrect, vertspace, hand, image)"),
    g_strdup(xo_tool_names[defaults->toolno[1]]));
  xo_keyval_update(configFile, "tools", "btn2_linked",
    _(" button 2 brush linked to primary brush (true/false) (overrides all other settings)"),
    g_strdup((defaults->linked_brush[1]==XO_BRUSH_LINKED)?"true":"false"));
  xo_keyval_update(configFile, "tools", "btn2_color",
    _(" button 2 brush color (for pen or highlighter only)"),
    (defaults->toolno[1]<XO_NUM_STROKE_TOOLS)?
      ((defaults->brushes[1][defaults->toolno[1]].color_no>=0)?
       g_strdup(xo_color_names[defaults->brushes[1][defaults->toolno[1]].color_no]):
       g_strdup_printf("#%08x", defaults->brushes[1][defaults->toolno[1]].color_rgba)):
      g_strdup("white"));
  xo_keyval_update(configFile, "tools", "btn2_thickness",
    _(" button 2 brush thickness (pen, eraser, or highlighter only)"),
    g_strdup_printf("%d", (defaults->toolno[1]<XO_NUM_STROKE_TOOLS)?
                            defaults->brushes[1][defaults->toolno[1]].thickness_no:0));
  xo_keyval_update(configFile, "tools", "btn2_ruler",
    _(" button 2 ruler mode (true/false) (for pen or highlighter only)"),
    g_strdup(((defaults->toolno[1]<XO_NUM_STROKE_TOOLS)?
              defaults->brushes[1][defaults->toolno[1]].ruler:FALSE)?"true":"false"));
  xo_keyval_update(configFile, "tools", "btn2_recognizer",
    _(" button 2 shape recognizer mode (true/false) (pen or highlighter only)"),
    g_strdup(((defaults->toolno[1]<XO_NUM_STROKE_TOOLS)?
              defaults->brushes[1][defaults->toolno[1]].recognizer:FALSE)?"true":"false"));
  xo_keyval_update(configFile, "tools", "btn2_erasermode",
    _(" button 2 eraser mode (eraser only)"),
    g_strdup_printf("%d", defaults->brushes[1][XO_TOOL_ERASER].tool_options));
  xo_keyval_update(configFile, "tools", "btn3_tool",
    _(" button 3 tool (pen, eraser, highlighter, text, selectregion, selectrect, vertspace, hand, image)"),
    g_strdup(xo_tool_names[defaults->toolno[2]]));
  xo_keyval_update(configFile, "tools", "btn3_linked",
    _(" button 3 brush linked to primary brush (true/false) (overrides all other settings)"),
    g_strdup((defaults->linked_brush[2]==XO_BRUSH_LINKED)?"true":"false"));
  xo_keyval_update(configFile, "tools", "btn3_color",
    _(" button 3 brush color (for pen or highlighter only)"),
    (defaults->toolno[2]<XO_NUM_STROKE_TOOLS)?
      ((defaults->brushes[2][defaults->toolno[2]].color_no>=0)?
       g_strdup(xo_color_names[defaults->brushes[2][defaults->toolno[2]].color_no]):
       g_strdup_printf("#%08x", defaults->brushes[2][defaults->toolno[2]].color_rgba)):
      g_strdup("white"));
  xo_keyval_update(configFile, "tools", "btn3_thickness",
    _(" button 3 brush thickness (pen, eraser, or highlighter only)"),
    g_strdup_printf("%d", (defaults->toolno[2]<XO_NUM_STROKE_TOOLS)?
                            defaults->brushes[2][defaults->toolno[2]].thickness_no:0));
  xo_keyval_update(configFile, "tools", "btn3_ruler",
    _(" button 3 ruler mode (true/false) (for pen or highlighter only)"),
    g_strdup(((defaults->toolno[2]<XO_NUM_STROKE_TOOLS)?
              defaults->brushes[2][defaults->toolno[2]].ruler:FALSE)?"true":"false"));
  xo_keyval_update(configFile, "tools", "btn3_recognizer",
    _(" button 3 shape recognizer mode (true/false) (pen or highlighter only)"),
    g_strdup(((defaults->toolno[2]<XO_NUM_STROKE_TOOLS)?
              defaults->brushes[2][defaults->toolno[2]].recognizer:FALSE)?"true":"false"));
  xo_keyval_update(configFile, "tools", "btn3_erasermode",
    _(" button 3 eraser mode (eraser only)"),
    g_strdup_printf("%d", defaults->brushes[2][XO_TOOL_ERASER].tool_options));

  xo_keyval_update(configFile, "tools", "pen_thicknesses",
    _(" thickness of the various pens (in points, 1 pt = 1/72 in)"),
    g_strdup_printf("%.2f;%.2f;%.2f;%.2f;%.2f", 
      xo_predef_thickness[XO_TOOL_PEN][0], xo_predef_thickness[XO_TOOL_PEN][1],
      xo_predef_thickness[XO_TOOL_PEN][2], xo_predef_thickness[XO_TOOL_PEN][3],
      xo_predef_thickness[XO_TOOL_PEN][4]));
  xo_keyval_update(configFile, "tools", "eraser_thicknesses",
		   _(" thickness of the various erasers (in points, 1 pt = 1/72 in)"),
		   g_strdup_printf("%.2f;%.2f;%.2f", 
				   xo_predef_thickness[XO_TOOL_ERASER][1], 
				   xo_predef_thickness[XO_TOOL_ERASER][2],
				   xo_predef_thickness[XO_TOOL_ERASER][3]));
  xo_keyval_update(configFile, "tools", "highlighter_thicknesses",
    _(" thickness of the various highlighters (in points, 1 pt = 1/72 in)"),
    g_strdup_printf("%.2f;%.2f;%.2f", 
		    xo_predef_thickness[XO_TOOL_HIGHLIGHTER][1], 
		    xo_predef_thickness[XO_TOOL_HIGHLIGHTER][2],
		    xo_predef_thickness[XO_TOOL_HIGHLIGHTER][3]));
  xo_keyval_update(configFile, "tools", "default_font",
    _(" name of the default font"),
    g_strdup(defaults->default_font_name));
  xo_keyval_update(configFile, "tools", "default_font_size",
    _(" default font size"),
    g_strdup_printf("%.1f", defaults->default_font_size));

  buf = g_key_file_to_data(configFile, NULL, NULL);

  if (buf == NULL) return;
  f = fopen(defaults->configFileName, "w");
  if (f==NULL) { g_free(buf); return; }
  fputs(buf, f);
  fclose(f);
  g_free(buf);
#endif
}

void xo_config_init(xo_defaults *defaults)
{
   int i, j;

  // File names

  gchar *tmpPath = g_build_filename(g_get_home_dir(), XO_CONFIG_DIR, NULL);
  // create dir. it is created this will simply fail
  mkdir(tmpPath, 0700); // safer (XO_RU data may be confidential)

  defaults->ruFileName = g_build_filename(tmpPath, XO_RU_FILE, NULL);
  defaults->configFileName = g_build_filename(tmpPath, XO_CONFIG_FILE, NULL);

  g_free(tmpPath);

  defaults->default_zoom = XO_DISPLAY_DPI_DEFAULT/72.0;
  defaults->zoom = defaults->startup_zoom = 1.0* defaults->default_zoom;

  defaults->view_continuous = TRUE;
  defaults->allow_xinput = TRUE;
  defaults->discard_corepointer = FALSE;
  defaults->ignore_other_devices = TRUE;
  defaults->left_handed = FALSE;
  defaults->shorten_menus = FALSE;
  defaults->shorten_menu_items = g_strdup(XO_DEFAULT_SHORTEN_MENUS);
  defaults->auto_save_prefs = FALSE;

  defaults->bg_apply_all_pages = FALSE;
  defaults->use_erasertip = FALSE;
  defaults->window_default_width = 720;
  defaults->window_default_height = 480;
  defaults->maximize_at_start = FALSE;
  defaults->fullscreen = FALSE;
  defaults->scrollbar_step_increment = 30;
  defaults->zoom_step_increment = 1;
  defaults->zoom_step_factor = 1.5;
  defaults->progressive_bg = TRUE;
  defaults->print_ruling = TRUE;
  defaults->default_unit = XO_UNIT_CM;
  defaults->default_path = NULL;
  defaults->default_image = NULL;
  defaults->default_font_name = g_strdup(XO_FONT_DEFAULT);
  defaults->default_font_size = XO_FONT_DEFAULT_SIZE;
  defaults->pressure_sensitivity = FALSE;
  defaults->width_minimum_multiplier = 0.0;
  defaults->width_maximum_multiplier = 1.25;
  defaults->button_switch_mapping = FALSE;
  defaults->autoload_pdf_xoj = FALSE;
  defaults->poppler_force_cairo = FALSE;
  
  // the default UI vertical order
  defaults->vertical_order[0][0] = 1; 
  defaults->vertical_order[0][1] = 2; 
  defaults->vertical_order[0][2] = 3; 
  defaults->vertical_order[0][3] = 0; 
  defaults->vertical_order[0][4] = 4;
  defaults->vertical_order[1][0] = 2;
  defaults->vertical_order[1][1] = 3;
  defaults->vertical_order[1][2] = 0;
  defaults->vertical_order[1][3] = defaults->vertical_order[1][4] = -1;

  defaults->toolno[0] = defaults->startuptool = XO_TOOL_PEN;
  for (i=1; i<= XO_NUM_BUTTONS; i++) {
    defaults->toolno[i] = XO_TOOL_ERASER;
  }

  for (i=0; i<=XO_NUM_BUTTONS; i++)
    defaults->linked_brush[i] = XO_BRUSH_LINKED;

  defaults->brushes[0][XO_TOOL_PEN].color_no = XO_COLOR_BLACK;
  defaults->brushes[0][XO_TOOL_ERASER].color_no = XO_COLOR_WHITE;
  defaults->brushes[0][XO_TOOL_HIGHLIGHTER].color_no = XO_COLOR_YELLOW;
  for (i=0; i < XO_NUM_STROKE_TOOLS; i++) {
    defaults->brushes[0][i].thickness_no = XO_THICKNESS_MEDIUM;
    defaults->brushes[0][i].tool_options = 0;
    defaults->brushes[0][i].ruler = FALSE;
    defaults->brushes[0][i].recognizer = FALSE;
    defaults->brushes[0][i].variable_width = FALSE;
  }
  for (i=0; i< XO_NUM_STROKE_TOOLS; i++)
    for (j=1; j<=XO_NUM_BUTTONS; j++)
      g_memmove(&(defaults->brushes[j][i]), &(defaults->brushes[0][i]), sizeof(xo_brush));

  // predef_thickness is already initialized as a global variable
  defaults->gs_bitmap_dpi = 144;
  defaults->pdftoppm_printing_dpi = 150;
  
  defaults->hiliter_opacity = 0.5;
  defaults->pen_cursor = FALSE;
  

  defaults->page_height = 792.0;
  defaults->page_width = 612.0;
  defaults->page_bg_type = XO_BG_SOLID;
  defaults->page_bg_color_no = XO_COLOR_WHITE;
  defaults->page_bg_color_rgba = xo_predef_bg_colors_rgba[XO_COLOR_WHITE];
  defaults->page_bg_ruling = XO_RULING_LINED;

}

void xo_defaults_default_path_set(xo_defaults *defaults, gchar *path)
{
  if (defaults->default_path != NULL) {
    g_free(defaults->default_path);
  }
  defaults->default_path = path;
  
}
