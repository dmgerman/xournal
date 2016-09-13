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

#include <signal.h>
#include <memory.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <libgnomecanvas/libgnomecanvas.h>
#include <zlib.h>
#include <math.h>
#include <locale.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <poppler/glib/poppler.h>

#ifdef GDK_WINDOWING_X11
 #include <gdk/gdkx.h>
 #include <X11/Xlib.h>
#endif

#include "xournal.h"
#include "xo-interface.h"
#include "xo-support.h"
#include "xo-callbacks.h"
#include "xo-misc.h"
#include "xo-file.h"
#include "xo-paint.h"
#include "xo-image.h"
#include "xo-shapes.h"

const char *tool_names[NUM_TOOLS] = {"pen", "eraser", "highlighter", "text", "selectregion", "selectrect", "vertspace", "hand", "image"};
const char *color_names[COLOR_MAX] = {"black", "blue", "red", "green",
   "gray", "lightblue", "lightgreen", "magenta", "orange", "yellow", "white"};
const char *bgtype_names[3] = {"solid", "pixmap", "pdf"};
const char *bgcolor_names[COLOR_MAX] = {"", "blue", "pink", "green",
   "", "", "", "", "orange", "yellow", "white"};
const char *bgstyle_names[4] = {"plain", "lined", "ruled", "graph"};
const char *file_domain_names[3] = {"absolute", "attach", "clone"};
const char *unit_names[4] = {"cm", "in", "px", "pt"};
const char *view_mode_names[3] = {"false", "true", "horiz"}; // need 'false' & 'true' for backward compatibility
int PDFTOPPM_PRINTING_DPI, GS_BITMAP_DPI;

// gzopen() wrapper to handle non-ASCII filenames in Windows

gzFile gzopen_wrapper(const char *path, const char *mode)
{
#ifdef WIN32
   gunichar2 *utf16_path = g_utf8_to_utf16(path, -1, NULL, NULL, NULL);
   gzFile f = gzopen_w(utf16_path, mode);
   g_free(utf16_path);
   return f;
#else
   return gzopen(path, mode);
#endif
}

// creates a new empty journal

void new_journal(void)
{
  journal.npages = 1;
  journal.pages = g_list_append(NULL, new_page(&ui.default_page));
  journal.last_attach_no = 0;
  ui.pageno = 0;
  ui.layerno = 0;
  ui.cur_page = (struct Page *) journal.pages->data;
  ui.cur_layer = (struct Layer *) ui.cur_page->layers->data;
  ui.saved = TRUE;
  ui.filename = NULL;
  update_file_name(NULL);
}

// check attachment names

void chk_attach_names(void)
{
  GList *list;
  struct Background *bg;
  
  for (list = journal.pages; list!=NULL; list = list->next) {
    bg = ((struct Page *)list->data)->bg;
    if (bg->type == BG_SOLID || bg->file_domain != DOMAIN_ATTACH ||
        bg->filename->s != NULL) continue;
    bg->filename->s = g_strdup_printf("bg_%d.png", ++journal.last_attach_no);
  }
}

/* Write image to file: returns true on success, false on error.
   The image is written as a base64 encoded PNG. */

gboolean write_image(gzFile f, Item *item)
{
  gchar *base64_str;

  if (item->image_png == NULL) {
    if (!gdk_pixbuf_save_to_buffer(item->image, &item->image_png, &item->image_png_len, "png", NULL, NULL)) {
      item->image_png_len = 0;       // failed for some reason, so forget it
      return FALSE;
    }
  }

  base64_str = g_base64_encode(item->image_png, item->image_png_len);
  gzputs(f, base64_str);
  g_free(base64_str);
  return TRUE;
}

// create pixbuf from base64 encoded PNG, or return NULL on failure

GdkPixbuf *read_pixbuf(const gchar *base64_str, gsize base64_strlen)
{
  gchar *base64_str2;
  gchar *png_buf;
  gsize png_buflen;
  GdkPixbuf *pixbuf;

  // We have to copy the string in order to null terminate it, sigh.
  base64_str2 = g_memdup(base64_str, base64_strlen+1);
  base64_str2[base64_strlen] = 0;
  png_buf = g_base64_decode(base64_str2, &png_buflen);

  pixbuf = pixbuf_from_buffer(png_buf, png_buflen);

  g_free(png_buf);
  g_free(base64_str2);
  return pixbuf;
}

// saves the journal to a file: returns true on success, false on error

gboolean save_journal(const char *filename, gboolean is_auto)
{
  gzFile f;
  struct Page *pg, *tmppg;
  struct Layer *layer;
  struct Item *item;
  int i, is_clone;
  char *tmpfn, *tmpstr;
  gboolean success;
  FILE *tmpf;
  GList *pagelist, *layerlist, *itemlist, *list;
  GtkWidget *dialog;
  
  f = gzopen_wrapper(filename, "wb");
  if (f==NULL) return FALSE;
  chk_attach_names();
  if (is_auto)
    ui.autosave_filename_list = g_list_append(ui.autosave_filename_list, g_strdup(filename));

  setlocale(LC_NUMERIC, "C");
  
  gzprintf(f, "<?xml version=\"1.0\" standalone=\"no\"?>\n"
     "<xournal version=\"" VERSION "\">\n"
     "<title>Xournal document - see http://math.mit.edu/~auroux/software/xournal/</title>\n");
  for (pagelist = journal.pages; pagelist!=NULL; pagelist = pagelist->next) {
    pg = (struct Page *)pagelist->data;
    gzprintf(f, "<page width=\"%.2f\" height=\"%.2f\">\n", pg->width, pg->height);
    gzprintf(f, "<background type=\"%s\" ", bgtype_names[pg->bg->type]); 
    if (pg->bg->type == BG_SOLID) {
      gzputs(f, "color=\"");
      if (pg->bg->color_no >= 0) gzputs(f, bgcolor_names[pg->bg->color_no]);
      else gzprintf(f, "#%08x", pg->bg->color_rgba);
      gzprintf(f, "\" style=\"%s\" ", bgstyle_names[pg->bg->ruling]);
    }
    else if (pg->bg->type == BG_PIXMAP) {
      is_clone = -1;
      for (list = journal.pages, i = 0; list!=pagelist; list = list->next, i++) {
        tmppg = (struct Page *)list->data;
        if (tmppg->bg->type == BG_PIXMAP && 
            tmppg->bg->pixbuf == pg->bg->pixbuf &&
            tmppg->bg->filename == pg->bg->filename)
          { is_clone = i; break; }
      }
      if (is_clone >= 0)
        gzprintf(f, "domain=\"clone\" filename=\"%d\" ", is_clone);
      else {
        if (pg->bg->file_domain == DOMAIN_ATTACH) {
          tmpfn = g_strdup_printf("%s.%s", filename, pg->bg->filename->s);
          if (is_auto)
            ui.autosave_filename_list = g_list_append(ui.autosave_filename_list, g_strdup(tmpfn));
          if (!gdk_pixbuf_save(pg->bg->pixbuf, tmpfn, "png", NULL, NULL) && !is_auto) {
            dialog = gtk_message_dialog_new(GTK_WINDOW(winMain), GTK_DIALOG_MODAL,
              GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, 
              _("Could not write background '%s'. Continuing anyway."), tmpfn);
            wrapper_gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
          }
          g_free(tmpfn);
        }
        tmpstr = g_markup_escape_text(pg->bg->filename->s, -1);
        gzprintf(f, "domain=\"%s\" filename=\"%s\" ", 
          file_domain_names[pg->bg->file_domain], tmpstr);
        g_free(tmpstr);
      }
    }
    else if (pg->bg->type == BG_PDF) {
      is_clone = 0;
      for (list = journal.pages; list!=pagelist; list = list->next) {
        tmppg = (struct Page *)list->data;
        if (tmppg->bg->type == BG_PDF) { is_clone = 1; break; }
      }
      if (!is_clone) {
        if (pg->bg->file_domain == DOMAIN_ATTACH) {
          tmpfn = g_strdup_printf("%s.%s", filename, pg->bg->filename->s);
          success = FALSE;
          if (bgpdf.status != STATUS_NOT_INIT && bgpdf.file_contents != NULL)
          {
            tmpf = g_fopen(tmpfn, "wb");
            if (is_auto)
              ui.autosave_filename_list = g_list_append(ui.autosave_filename_list, g_strdup(tmpfn));
            if (tmpf != NULL && fwrite(bgpdf.file_contents, 1, bgpdf.file_length, tmpf) == bgpdf.file_length)
              success = TRUE;
            fclose(tmpf);
          }
          if (!success && !is_auto) {
            dialog = gtk_message_dialog_new(GTK_WINDOW(winMain), GTK_DIALOG_MODAL,
              GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, 
              _("Could not write background '%s'. Continuing anyway."), tmpfn);
            wrapper_gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
          }
          g_free(tmpfn);
        }
        tmpstr = g_markup_escape_text(pg->bg->filename->s, -1);
        gzprintf(f, "domain=\"%s\" filename=\"%s\" ", 
          file_domain_names[pg->bg->file_domain], tmpstr);
        g_free(tmpstr);
      }
      gzprintf(f, "pageno=\"%d\" ", pg->bg->file_page_seq);
    }
    gzprintf(f, "/>\n");
    for (layerlist = pg->layers; layerlist!=NULL; layerlist = layerlist->next) {
      layer = (struct Layer *)layerlist->data;
      gzprintf(f, "<layer>\n");
      for (itemlist = layer->items; itemlist!=NULL; itemlist = itemlist->next) {
        item = (struct Item *)itemlist->data;
        if (item->type == ITEM_STROKE) {
          gzprintf(f, "<stroke tool=\"%s\" color=\"", 
                          tool_names[item->brush.tool_type]);
          if (item->brush.color_no >= 0)
            gzputs(f, color_names[item->brush.color_no]);
          else
            gzprintf(f, "#%08x", item->brush.color_rgba);
          gzprintf(f, "\" width=\"%.2f", item->brush.thickness);
          if (item->brush.variable_width)
            for (i=0;i<item->path->num_points-1;i++)
              gzprintf(f, " %.2f", item->widths[i]);
          gzprintf(f, "\">\n");
          for (i=0;i<2*item->path->num_points;i++)
            gzprintf(f, "%.2f ", item->path->coords[i]);
          gzprintf(f, "\n</stroke>\n");
        }
        if (item->type == ITEM_TEXT) {
          tmpstr = g_markup_escape_text(item->font_name, -1);
          gzprintf(f, "<text font=\"%s\" size=\"%.2f\" x=\"%.2f\" y=\"%.2f\" color=\"",
            tmpstr, item->font_size, item->bbox.left, item->bbox.top);
          g_free(tmpstr);
          if (item->brush.color_no >= 0)
            gzputs(f, color_names[item->brush.color_no]);
          else
            gzprintf(f, "#%08x", item->brush.color_rgba);
          tmpstr = g_markup_escape_text(item->text, -1);
          gzputs(f, "\">");
          gzputs(f, tmpstr); // gzprintf() can't handle > 4095 bytes
          gzputs(f, "</text>\n");
          g_free(tmpstr);
        }
        if (item->type == ITEM_IMAGE) {
          gzprintf(f, "<image left=\"%.2f\" top=\"%.2f\" right=\"%.2f\" bottom=\"%.2f\">", 
            item->bbox.left, item->bbox.top, item->bbox.right, item->bbox.bottom);
          if (!write_image(f, item)) success = FALSE;
          gzprintf(f, "</image>\n");
        }
      }
      gzprintf(f, "</layer>\n");
    }
    gzprintf(f, "</page>\n");
  }
  gzprintf(f, "</xournal>\n");
  gzclose(f);
  setlocale(LC_NUMERIC, "");

  return TRUE;
}

// autosave stuff

void autosave_cleanup(GList **list)
{
  char *filename;
  GList *l;
  for (l = *list; l!=NULL; l = l->next) {
    filename = (char*)l->data;
    g_unlink(filename);
    g_free(filename);
  }
  if (*list!=NULL) g_list_free(*list);
  *list = NULL;
}

#if !GLIB_CHECK_VERSION(2,14,0)
#define g_timeout_add_seconds(interval, function, data) g_timeout_add(1000*interval, function, data)
#endif

gboolean autosave_cb(gpointer is_catchup)
{
  GList *old_filenames;
  gchar *base_filename, *test_filename;
  int num;

  // figure out whether we actually need to auto-save, and can do so.
  if (!ui.autosave_enabled) {
    ui.autosave_need_catchup = FALSE;
    if (!is_catchup) ui.autosave_loop_running = FALSE;
    return FALSE; // kill the timeout loop, if we're in it
  }
  if (ui.saved || !ui.need_autosave) { // nothing to do
    ui.autosave_need_catchup = FALSE;
    return TRUE; // come back later, if we're in the timeout loop
  }
  if (ui.cur_item_type != ITEM_NONE) { // can't do things now, request catchup
    ui.autosave_need_catchup = TRUE;
    return TRUE; // can't do it right now, come back later
  }
  
  // generate an autosave filename
  base_filename = candidate_save_filename();
  for (num=0; num<=AUTOSAVE_MAX; num++) {
    test_filename = g_strdup_printf(AUTOSAVE_FILENAME_TEMPLATE, base_filename, num);
    if (!g_file_test(test_filename, G_FILE_TEST_EXISTS)) break;
    g_free(test_filename);
  }
  g_free(base_filename);
  if (num > AUTOSAVE_MAX) // we ran out of autosave file names... try at the next loop iteration
    return TRUE;
  // keep track of old save filenames
  old_filenames = ui.autosave_filename_list;
  ui.autosave_filename_list = NULL;
  if (save_journal(test_filename, TRUE)) { // non-interactive save -> success
    ui.need_autosave = FALSE; // no longer need an auto-save
    autosave_cleanup(&old_filenames);
  } else { // aborted
    autosave_cleanup(&ui.autosave_filename_list); 
    ui.autosave_filename_list = old_filenames;
  }
  g_free(test_filename);
  
  return TRUE; // continue with the timed loop, if we're in it
}

void init_autosave(void)
{
  if (!ui.autosave_enabled) return;
  if (ui.autosave_loop_running) return; // already running
  g_timeout_add_seconds(ui.autosave_delay, autosave_cb, NULL);
  ui.autosave_loop_running = TRUE;
  ui.autosave_need_catchup = FALSE;
  ui.need_autosave = !ui.saved;
}

void delete_autosave(char *filename)
{
  char *attach_filename;
  int k;

  g_unlink(filename);
  attach_filename = g_strdup_printf("%s.bg.pdf", filename);
  g_unlink(attach_filename);
  k = 1;
  do {
    g_free(attach_filename);
    attach_filename = g_strdup_printf("%s.bg_%d.png", filename, k++);
  } 
  while (!g_unlink(attach_filename));
  g_free(attach_filename);
}

char *check_for_autosave(char *filename)
{
  int num, count;
  char *test_filename, *filter_str, *cand_filename;
  GtkWidget *dialog;
  GtkResponseType response;
  GtkFileFilter *filt_all, *filt_autosave;

  count = 0;
  for (num=0; num<=AUTOSAVE_MAX; num++) {
    test_filename = g_strdup_printf(AUTOSAVE_FILENAME_TEMPLATE, filename, num);
    if (g_file_test(test_filename, G_FILE_TEST_EXISTS)) {
      if (!count) cand_filename = g_strdup(test_filename);
      count++;
    }
    g_free(test_filename);
  }
  if (count == 0) return g_strdup(filename); // no auto-saves

  // auto-save file found, ask user what to do about it.
  dialog = gtk_message_dialog_new(GTK_WINDOW (winMain), GTK_DIALOG_MODAL,
     GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE, 
     _("%d auto-save files were found, including '%s'"), count, xo_basename(cand_filename, TRUE));
  gtk_dialog_add_button(GTK_DIALOG(dialog), _("Ignore"), GTK_RESPONSE_NO);
  gtk_dialog_add_button(GTK_DIALOG(dialog), _("Restore auto-save"), GTK_RESPONSE_YES);
  gtk_dialog_add_button(GTK_DIALOG(dialog), _("Delete auto-saves"), GTK_RESPONSE_REJECT);
  gtk_dialog_set_default_response(GTK_DIALOG (dialog), GTK_RESPONSE_NO);
  response = wrapper_gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
  
  if (response == GTK_RESPONSE_REJECT) {  // delete all auto-saves + attachments
    set_cursor_busy(TRUE);
    for (num=0; num<=AUTOSAVE_MAX; num++) {
      test_filename = g_strdup_printf(AUTOSAVE_FILENAME_TEMPLATE, filename, num);
      if (g_file_test(test_filename, G_FILE_TEST_EXISTS))
        delete_autosave(test_filename);
      g_free(test_filename);
    }
    set_cursor_busy(FALSE);
  }
  
  if (response != GTK_RESPONSE_YES) {
    g_free(cand_filename);
    return g_strdup(filename); // ignore/delete
  }
  
  // restore: ask user to pick one, if there's more than one
  if (count > 1) {
    dialog = gtk_file_chooser_dialog_new(_("Multiple auto-saves found"), GTK_WINDOW (winMain),
         GTK_FILE_CHOOSER_ACTION_OPEN, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
         GTK_STOCK_OPEN, GTK_RESPONSE_OK, NULL);
#ifdef FILE_DIALOG_SIZE_BUGFIX
    gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 400);
#endif
    gtk_file_chooser_set_filename(GTK_FILE_CHOOSER (dialog), cand_filename);
//  gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER (dialog), xo_basename(cand_filename, FALSE));
    g_free(cand_filename);
    filt_all = gtk_file_filter_new();
    gtk_file_filter_set_name(filt_all, _("All files"));
    gtk_file_filter_add_pattern(filt_all, "*");
    filt_autosave = gtk_file_filter_new();
    filter_str = g_strdup_printf(AUTOSAVE_FILENAME_FILTER, xo_basename(filename, TRUE));
    gtk_file_filter_set_name(filt_autosave, _("Auto-save files"));
    gtk_file_filter_add_pattern(filt_autosave, filter_str);
    g_free(filter_str);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER (dialog), filt_autosave);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER (dialog), filt_all);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    if (wrapper_gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_OK) {
      gtk_widget_destroy(dialog);
      return g_strdup(filename);
    }
    cand_filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    gtk_widget_destroy(dialog);
  }
  // cand_filename is the autosave we want to open
  return cand_filename;
}


// closes a journal: returns true on success, false on abort

gboolean close_journal(void)
{
  if (!ok_to_close()) return FALSE;
  
  // free everything...
  reset_selection();
  reset_recognizer();
  clear_redo_stack();
  clear_undo_stack();

  shutdown_bgpdf();
  delete_journal(&journal);
  autosave_cleanup(&ui.autosave_filename_list);
  
  return TRUE;
  /* note: various members of ui and journal are now in invalid states,
     use new_journal() to reinitialize them */
}

// sanitize a string containing floats, in case it may have , instead of .
// also replace Windows-produced 1.#J by inf

void cleanup_numeric(char *s)
{
  while (*s!=0) { 
    if (*s==',') *s='.'; 
    if (*s=='1' && s[1]=='.' && s[2]=='#' && s[3]=='J') 
      { *s='i'; s[1]='n'; s[2]='f'; s[3]=' '; }
    s++; 
  }
}

// the XML parser functions for open_journal()

struct Journal tmpJournal;
struct Page *tmpPage;
struct Layer *tmpLayer;
struct Item *tmpItem;
char *tmpFilename;
struct Background *tmpBg_pdf;

GError *xoj_invalid(void)
{
  return g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT, _("Invalid file contents"));
}

void xoj_parser_start_element(GMarkupParseContext *context,
   const gchar *element_name, const gchar **attribute_names, 
   const gchar **attribute_values, gpointer user_data, GError **error)
{
  int has_attr, i;
  char *ptr, *tmpptr;
  struct Background *tmpbg;
  char *tmpbg_filename;
  gdouble val;
  GtkWidget *dialog;
  
  if (!strcmp(element_name, "title") || !strcmp(element_name, "xournal")) {
    if (tmpPage != NULL) {
      *error = xoj_invalid();
      return;
    }
    // nothing special to do
  }
  else if (!strcmp(element_name, "page")) { // start of a page
    if (tmpPage != NULL) {
      *error = xoj_invalid();
      return;
    }
    tmpPage = (struct Page *)g_malloc(sizeof(struct Page));
    tmpPage->layers = NULL;
    tmpPage->nlayers = 0;
    tmpPage->group = NULL;
    tmpPage->bg = g_new(struct Background, 1);
    tmpPage->bg->type = -1;
    tmpPage->bg->canvas_item = NULL;
    tmpPage->bg->pixbuf = NULL;
    tmpPage->bg->filename = NULL;
    tmpJournal.pages = g_list_append(tmpJournal.pages, tmpPage);
    tmpJournal.npages++;
    // scan for height and width attributes
    has_attr = 0;
    while (*attribute_names!=NULL) {
      if (!strcmp(*attribute_names, "width")) {
        if (has_attr & 1) *error = xoj_invalid();
        cleanup_numeric((gchar *)*attribute_values);
        tmpPage->width = g_ascii_strtod(*attribute_values, &ptr);
        if (ptr == *attribute_values) *error = xoj_invalid();
        has_attr |= 1;
      }
      else if (!strcmp(*attribute_names, "height")) {
        if (has_attr & 2) *error = xoj_invalid();
        cleanup_numeric((gchar *)*attribute_values);
        tmpPage->height = g_ascii_strtod(*attribute_values, &ptr);
        if (ptr == *attribute_values) *error = xoj_invalid();
        has_attr |= 2;
      }
      else *error = xoj_invalid();
      attribute_names++;
      attribute_values++;
    }
    if (has_attr!=3) *error = xoj_invalid();
  }
  else if (!strcmp(element_name, "background")) {
    if (tmpPage == NULL || tmpLayer !=NULL || tmpPage->bg->type >= 0) {
      *error = xoj_invalid();
      return;
    }
    has_attr = 0;
    while (*attribute_names!=NULL) {
      if (!strcmp(*attribute_names, "type")) {
        if (has_attr) *error = xoj_invalid();
        for (i=0; i<3; i++)
          if (!strcmp(*attribute_values, bgtype_names[i]))
            tmpPage->bg->type = i;
        if (tmpPage->bg->type < 0) *error = xoj_invalid();
        has_attr |= 1;
        if (tmpPage->bg->type == BG_PDF) {
          if (tmpBg_pdf == NULL) tmpBg_pdf = tmpPage->bg;
          else {
            has_attr |= 24;
            tmpPage->bg->filename = refstring_ref(tmpBg_pdf->filename);
            tmpPage->bg->file_domain = tmpBg_pdf->file_domain;
          }
        }
      }
      else if (!strcmp(*attribute_names, "color")) {
        if (tmpPage->bg->type != BG_SOLID) *error = xoj_invalid();
        if (has_attr & 2) *error = xoj_invalid();
        tmpPage->bg->color_no = COLOR_OTHER;
        for (i=0; i<COLOR_MAX; i++)
          if (!strcmp(*attribute_values, bgcolor_names[i])) {
            tmpPage->bg->color_no = i;
            tmpPage->bg->color_rgba = predef_bgcolors_rgba[i];
          }
        // there's also the case of hex (#rrggbbaa) colors
        if (tmpPage->bg->color_no == COLOR_OTHER && **attribute_values == '#') {
          tmpPage->bg->color_rgba = strtoul(*attribute_values + 1, &ptr, 16);
          if (*ptr!=0) *error = xoj_invalid();
        }
        has_attr |= 2;
      }
      else if (!strcmp(*attribute_names, "style")) {
        if (tmpPage->bg->type != BG_SOLID) *error = xoj_invalid();
        if (has_attr & 4) *error = xoj_invalid();
        tmpPage->bg->ruling = -1;
        for (i=0; i<4; i++)
          if (!strcmp(*attribute_values, bgstyle_names[i]))
            tmpPage->bg->ruling = i;
        if (tmpPage->bg->ruling < 0) *error = xoj_invalid();
        has_attr |= 4;
      }
      else if (!strcmp(*attribute_names, "domain")) {
        if (tmpPage->bg->type <= BG_SOLID || (has_attr & 8))
          { *error = xoj_invalid(); return; }
        tmpPage->bg->file_domain = -1;
        for (i=0; i<3; i++)
          if (!strcmp(*attribute_values, file_domain_names[i]))
            tmpPage->bg->file_domain = i;
        if (tmpPage->bg->file_domain < 0)
          { *error = xoj_invalid(); return; }
        has_attr |= 8;
      }
      else if (!strcmp(*attribute_names, "filename")) {
        if (tmpPage->bg->type <= BG_SOLID || (has_attr != 9)) 
          { *error = xoj_invalid(); return; }
        if (tmpPage->bg->file_domain == DOMAIN_CLONE) {
          // filename is a page number
          i = strtol(*attribute_values, &ptr, 10);
          if (ptr == *attribute_values || i < 0 || i > tmpJournal.npages-2)
            { *error = xoj_invalid(); return; }
          tmpbg = ((struct Page *)g_list_nth_data(tmpJournal.pages, i))->bg;
          if (tmpbg->type != tmpPage->bg->type)
            { *error = xoj_invalid(); return; }
          tmpPage->bg->filename = refstring_ref(tmpbg->filename);
          tmpPage->bg->pixbuf = tmpbg->pixbuf;
          if (tmpbg->pixbuf!=NULL) g_object_ref(tmpbg->pixbuf);
          tmpPage->bg->file_domain = tmpbg->file_domain;
        }
        else {
          tmpPage->bg->filename = new_refstring(*attribute_values);
          if (tmpPage->bg->type == BG_PIXMAP) {
            if (tmpPage->bg->file_domain == DOMAIN_ATTACH) {
              tmpbg_filename = g_strdup_printf("%s.%s", tmpFilename, *attribute_values);
              if (sscanf(*attribute_values, "bg_%d.png", &i) == 1)
                if (i > tmpJournal.last_attach_no) 
                  tmpJournal.last_attach_no = i;
            }
            else tmpbg_filename = g_strdup(*attribute_values);
            tmpPage->bg->pixbuf = gdk_pixbuf_new_from_file(tmpbg_filename, NULL);
            if (tmpPage->bg->pixbuf == NULL) {
              dialog = gtk_message_dialog_new(GTK_WINDOW(winMain), GTK_DIALOG_MODAL,
                GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, 
                _("Could not open background '%s'. Setting background to white."),
                tmpbg_filename);
              wrapper_gtk_dialog_run(GTK_DIALOG(dialog));
              gtk_widget_destroy(dialog);
              tmpPage->bg->pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 1, 1);
              gdk_pixbuf_fill(tmpPage->bg->pixbuf, 0xffffffff); // solid white
            }
            g_free(tmpbg_filename);
          }
        }
        has_attr |= 16;
      }
      else if (!strcmp(*attribute_names, "pageno")) {
        if (tmpPage->bg->type != BG_PDF || (has_attr & 32))
          { *error = xoj_invalid(); return; }
        tmpPage->bg->file_page_seq = strtol(*attribute_values, &ptr, 10);
        if (ptr == *attribute_values) *error = xoj_invalid();
        has_attr |= 32;
      }
      else *error = xoj_invalid();
      attribute_names++;
      attribute_values++;
    }
    if (tmpPage->bg->type < 0) *error = xoj_invalid();
    if (tmpPage->bg->type == BG_SOLID && has_attr != 7) *error = xoj_invalid();
    if (tmpPage->bg->type == BG_PIXMAP && has_attr != 25) *error = xoj_invalid();
    if (tmpPage->bg->type == BG_PDF && has_attr != 57) *error = xoj_invalid();
  }
  else if (!strcmp(element_name, "layer")) { // start of a layer
    if (tmpPage == NULL || tmpLayer != NULL) {
      *error = xoj_invalid();
      return;
    }
    tmpLayer = (struct Layer *)g_malloc(sizeof(struct Layer));
    tmpLayer->items = NULL;
    tmpLayer->nitems = 0;
    tmpLayer->group = NULL;
    tmpPage->layers = g_list_append(tmpPage->layers, tmpLayer);
    tmpPage->nlayers++;
  }
  else if (!strcmp(element_name, "stroke")) { // start of a stroke
    if (tmpLayer == NULL || tmpItem != NULL) {
      *error = xoj_invalid();
      return;
    }
    tmpItem = (struct Item *)g_malloc(sizeof(struct Item));
    tmpItem->type = ITEM_STROKE;
    tmpItem->path = NULL;
    tmpItem->canvas_item = NULL;
    tmpItem->widths = NULL;
    tmpLayer->items = g_list_append(tmpLayer->items, tmpItem);
    tmpLayer->nitems++;
    // scan for tool, color, and width attributes
    has_attr = 0;
    while (*attribute_names!=NULL) {
      if (!strcmp(*attribute_names, "width")) {
        if (has_attr & 1) *error = xoj_invalid();
        cleanup_numeric((gchar *)*attribute_values);
        tmpItem->brush.thickness = g_ascii_strtod(*attribute_values, &ptr);
        if (ptr == *attribute_values) *error = xoj_invalid();
        i = 0;
        while (*ptr!=0) {
          realloc_cur_widths(i+1);
          ui.cur_widths[i] = g_ascii_strtod(ptr, &tmpptr);
          if (tmpptr == ptr) break;
          ptr = tmpptr;
          i++;
        }
        tmpItem->brush.variable_width = (i>0);
        if (i>0) {
          tmpItem->brush.variable_width = TRUE;
          tmpItem->widths = (gdouble *) g_memdup(ui.cur_widths, i*sizeof(gdouble));
          ui.cur_path.num_points =  i+1;
        }
        has_attr |= 1;
      }
      else if (!strcmp(*attribute_names, "color")) {
        if (has_attr & 2) *error = xoj_invalid();
        tmpItem->brush.color_no = COLOR_OTHER;
        for (i=0; i<COLOR_MAX; i++)
          if (!strcmp(*attribute_values, color_names[i])) {
            tmpItem->brush.color_no = i;
            tmpItem->brush.color_rgba = predef_colors_rgba[i];
          }
        // there's also the case of hex (#rrggbbaa) colors
        if (tmpItem->brush.color_no == COLOR_OTHER && **attribute_values == '#') {
          tmpItem->brush.color_rgba = strtoul(*attribute_values + 1, &ptr, 16);
          if (*ptr!=0) *error = xoj_invalid();
        }
        has_attr |= 2;
      }
      else if (!strcmp(*attribute_names, "tool")) {
        if (has_attr & 4) *error = xoj_invalid();
        tmpItem->brush.tool_type = -1;
        for (i=0; i<NUM_STROKE_TOOLS; i++)
          if (!strcmp(*attribute_values, tool_names[i])) {
            tmpItem->brush.tool_type = i;
          }
        if (tmpItem->brush.tool_type == -1) *error = xoj_invalid();
        has_attr |= 4;
      }
      else *error = xoj_invalid();
      attribute_names++;
      attribute_values++;
    }
    if (has_attr!=7) *error = xoj_invalid();
    // finish filling the brush info
    tmpItem->brush.thickness_no = 0;  // who cares ?
    tmpItem->brush.tool_options = 0;  // who cares ?
    tmpItem->brush.ruler = FALSE;
    tmpItem->brush.recognizer = FALSE;
    if (tmpItem->brush.tool_type == TOOL_HIGHLIGHTER) {
      if (tmpItem->brush.color_no >= 0)
        tmpItem->brush.color_rgba &= ui.hiliter_alpha_mask;
    }
  }
  else if (!strcmp(element_name, "text")) { // start of a text item
    if (tmpLayer == NULL || tmpItem != NULL) {
      *error = xoj_invalid();
      return;
    }
    tmpItem = (struct Item *)g_malloc0(sizeof(struct Item));
    tmpItem->type = ITEM_TEXT;
    tmpItem->canvas_item = NULL;
    tmpLayer->items = g_list_append(tmpLayer->items, tmpItem);
    tmpLayer->nitems++;
    // scan for font, size, x, y, and color attributes
    has_attr = 0;
    while (*attribute_names!=NULL) {
      if (!strcmp(*attribute_names, "font")) {
        if (has_attr & 1) *error = xoj_invalid();
        tmpItem->font_name = g_strdup(*attribute_values);
        has_attr |= 1;
      }
      else if (!strcmp(*attribute_names, "size")) {
        if (has_attr & 2) *error = xoj_invalid();
        cleanup_numeric((gchar *)*attribute_values);
        tmpItem->font_size = g_ascii_strtod(*attribute_values, &ptr);
        if (ptr == *attribute_values) *error = xoj_invalid();
        has_attr |= 2;
      }
      else if (!strcmp(*attribute_names, "x")) {
        if (has_attr & 4) *error = xoj_invalid();
        cleanup_numeric((gchar *)*attribute_values);
        tmpItem->bbox.left = g_ascii_strtod(*attribute_values, &ptr);
        if (ptr == *attribute_values) *error = xoj_invalid();
        has_attr |= 4;
      }
      else if (!strcmp(*attribute_names, "y")) {
        if (has_attr & 8) *error = xoj_invalid();
        cleanup_numeric((gchar *)*attribute_values);
        tmpItem->bbox.top = g_ascii_strtod(*attribute_values, &ptr);
        if (ptr == *attribute_values) *error = xoj_invalid();
        has_attr |= 8;
      }
      else if (!strcmp(*attribute_names, "color")) {
        if (has_attr & 16) *error = xoj_invalid();
        tmpItem->brush.color_no = COLOR_OTHER;
        for (i=0; i<COLOR_MAX; i++)
          if (!strcmp(*attribute_values, color_names[i])) {
            tmpItem->brush.color_no = i;
            tmpItem->brush.color_rgba = predef_colors_rgba[i];
          }
        // there's also the case of hex (#rrggbbaa) colors
        if (tmpItem->brush.color_no == COLOR_OTHER && **attribute_values == '#') {
          tmpItem->brush.color_rgba = strtoul(*attribute_values + 1, &ptr, 16);
          if (*ptr!=0) *error = xoj_invalid();
        }
        has_attr |= 16;
      }
      else *error = xoj_invalid();
      attribute_names++;
      attribute_values++;
    }
    if (has_attr!=31) *error = xoj_invalid();
  }
  else if (!strcmp(element_name, "image")) { // start of a image item
    if (tmpLayer == NULL || tmpItem != NULL) {
      *error = xoj_invalid();
      return;
    }
    tmpItem = (struct Item *)g_malloc0(sizeof(struct Item));
    tmpItem->type = ITEM_IMAGE;
    tmpItem->canvas_item = NULL;
    tmpItem->image=NULL;
    tmpItem->image_png = NULL;
    tmpItem->image_png_len = 0;
    tmpLayer->items = g_list_append(tmpLayer->items, tmpItem);
    tmpLayer->nitems++;
    // scan for x, y
    has_attr = 0;
    while (*attribute_names!=NULL) {
      if (!strcmp(*attribute_names, "left")) {
        if (has_attr & 1) *error = xoj_invalid();
        cleanup_numeric((gchar *)*attribute_values);
        tmpItem->bbox.left = g_ascii_strtod(*attribute_values, &ptr);
        if (ptr == *attribute_values) *error = xoj_invalid();
        has_attr |= 1;
      }
      else if (!strcmp(*attribute_names, "top")) {
        if (has_attr & 2) *error = xoj_invalid();
        cleanup_numeric((gchar *)*attribute_values);
        tmpItem->bbox.top = g_ascii_strtod(*attribute_values, &ptr);
        if (ptr == *attribute_values) *error = xoj_invalid();
        has_attr |= 2;
      }
      else if (!strcmp(*attribute_names, "right")) {
        if (has_attr & 4) *error = xoj_invalid();
        cleanup_numeric((gchar *)*attribute_values);
        tmpItem->bbox.right = g_ascii_strtod(*attribute_values, &ptr);
        if (ptr == *attribute_values) *error = xoj_invalid();
        has_attr |= 4;
      }
      else if (!strcmp(*attribute_names, "bottom")) {
        if (has_attr & 8) *error = xoj_invalid();
        cleanup_numeric((gchar *)*attribute_values);
        tmpItem->bbox.bottom = g_ascii_strtod(*attribute_values, &ptr);
        if (ptr == *attribute_values) *error = xoj_invalid();
        has_attr |= 8;
      }
      else *error = xoj_invalid();
      attribute_names++;
      attribute_values++;
    }
    if (has_attr!=15) *error = xoj_invalid();
  }
}

void xoj_parser_end_element(GMarkupParseContext *context,
   const gchar *element_name, gpointer user_data, GError **error)
{
  if (!strcmp(element_name, "page")) {
    if (tmpPage == NULL || tmpLayer != NULL) {
      *error = xoj_invalid();
      return;
    }
    if (tmpPage->nlayers == 0 || tmpPage->bg->type < 0) *error = xoj_invalid();
    tmpPage = NULL;
  }
  if (!strcmp(element_name, "layer")) {
    if (tmpLayer == NULL || tmpItem != NULL) {
      *error = xoj_invalid();
      return;
    }
    tmpLayer = NULL;
  }
  if (!strcmp(element_name, "stroke")) {
    if (tmpItem == NULL) {
      *error = xoj_invalid();
      return;
    }
    update_item_bbox(tmpItem);
    tmpItem = NULL;
  }
  if (!strcmp(element_name, "text")) {
    if (tmpItem == NULL) {
      *error = xoj_invalid();
      return;
    }
    tmpItem = NULL;
  }
  if (!strcmp(element_name, "image")) {
    if (tmpItem == NULL) {
      *error = xoj_invalid();
      return;
    }
    tmpItem = NULL;
  }
}

void xoj_parser_text(GMarkupParseContext *context,
   const gchar *text, gsize text_len, gpointer user_data, GError **error)
{
  const gchar *element_name, *ptr;
  int n;
  
  element_name = g_markup_parse_context_get_element(context);
  if (element_name == NULL) return;
  if (!strcmp(element_name, "stroke")) {
    cleanup_numeric((gchar *)text);
    ptr = text;
    n = 0;
    while (text_len > 0) {
      realloc_cur_path(n/2 + 1);
      ui.cur_path.coords[n] = g_ascii_strtod(text, (char **)(&ptr));
      if (ptr == text) break;
      text_len -= (ptr - text);
      text = ptr;
      if (!finite_sized(ui.cur_path.coords[n])) {
        if (n>=2) ui.cur_path.coords[n] = ui.cur_path.coords[n-2];
        else ui.cur_path.coords[n] = 0;
      }
      n++;
    }
    if (n<4 || n&1 || 
        (tmpItem->brush.variable_width && (n!=2*ui.cur_path.num_points))) 
      { *error = xoj_invalid(); return; } // wrong number of points
    tmpItem->path = gnome_canvas_points_new(n/2);
    g_memmove(tmpItem->path->coords, ui.cur_path.coords, n*sizeof(double));
  }
  if (!strcmp(element_name, "text")) {
    tmpItem->text = g_malloc(text_len+1);
    g_memmove(tmpItem->text, text, text_len);
    tmpItem->text[text_len]=0;
  }
  if (!strcmp(element_name, "image")) {
    tmpItem->image = read_pixbuf(text, text_len);
  }
}

gboolean user_wants_second_chance(char **filename)
{
  GtkWidget *dialog;
  GtkFileFilter *filt_all, *filt_pdf;
  GtkResponseType response;

  dialog = gtk_message_dialog_new(GTK_WINDOW(winMain), GTK_DIALOG_MODAL,
    GTK_MESSAGE_ERROR, GTK_BUTTONS_YES_NO, 
    _("Could not open background '%s'.\nSelect another file?"),
    *filename);
  response = wrapper_gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
  if (response != GTK_RESPONSE_YES) return FALSE;
  dialog = gtk_file_chooser_dialog_new(_("Open PDF"), GTK_WINDOW (winMain),
     GTK_FILE_CHOOSER_ACTION_OPEN, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
     GTK_STOCK_OPEN, GTK_RESPONSE_OK, NULL);
#ifdef FILE_DIALOG_SIZE_BUGFIX
  gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 400);
#endif
  
  filt_all = gtk_file_filter_new();
  gtk_file_filter_set_name(filt_all, _("All files"));
  gtk_file_filter_add_pattern(filt_all, "*");
  filt_pdf = gtk_file_filter_new();
  gtk_file_filter_set_name(filt_pdf, _("PDF files"));
  gtk_file_filter_add_pattern(filt_pdf, "*.pdf");
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER (dialog), filt_pdf);
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER (dialog), filt_all);

  if (ui.default_path!=NULL) gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER (dialog), ui.default_path);

  if (wrapper_gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_OK) {
    gtk_widget_destroy(dialog);
    return FALSE;
  }
  g_free(*filename);
  *filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
  gtk_widget_destroy(dialog);
  return TRUE;    
}

gboolean open_journal(char *filename)
{
  const GMarkupParser parser = { xoj_parser_start_element, 
                                 xoj_parser_end_element, 
                                 xoj_parser_text, NULL, NULL};
  GMarkupParseContext *context;
  GError *error;
  GtkWidget *dialog;
  gboolean valid;
  gzFile f;
  char buffer[1000];
  int len;
  gchar *tmpfn, *tmpfn2, *p, *q, *filename_actual;
  gboolean maybe_pdf;
  
  tmpfn = g_strdup_printf("%s.xoj", filename);
  if (ui.autoload_pdf_xoj && g_file_test(tmpfn, G_FILE_TEST_EXISTS) &&
      (g_str_has_suffix(filename, ".pdf") || g_str_has_suffix(filename, ".PDF")))
  {
    valid = open_journal(tmpfn);
    g_free(tmpfn);
    return valid;
  }
  g_free(tmpfn);

  filename_actual = check_for_autosave(filename);

  if (ui.autocreate_new_xoj && 
      !g_file_test(filename_actual, G_FILE_TEST_EXISTS) &&
      g_str_has_suffix(filename_actual, ".xoj"))
  { // doesn't exist yet -- make a new journal and pre-populate its name?
    p = g_path_get_dirname(filename_actual);
    q = xo_basename(filename_actual, FALSE);
    valid = g_file_test(p, G_FILE_TEST_IS_DIR) && (q[0]!='.');
    g_free(p);
    if (valid) { // seems legit
      dialog = gtk_message_dialog_new(GTK_WINDOW(winMain), GTK_DIALOG_MODAL,
              GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, 
              _("File '%s' doesn't exist yet. Creating a new document."), filename_actual);
      wrapper_gtk_dialog_run(GTK_DIALOG(dialog));
      gtk_widget_destroy(dialog);
      ui.saved = TRUE; // allow close journal without re-prompting
      on_fileNew_activate(NULL, NULL); // close_journal, new_journal, reset UI
      update_file_name(filename_actual);
      return TRUE;
    }
  }

  f = gzopen_wrapper(filename_actual, "rb");
  if (f==NULL) { g_free(filename_actual); return FALSE; }
  if (filename[0]=='/') {
    if (ui.default_path != NULL) g_free(ui.default_path);
    ui.default_path = g_path_get_dirname(filename);
  }
  
  context = g_markup_parse_context_new(&parser, 0, NULL, NULL);
  valid = TRUE;
  tmpJournal.npages = 0;
  tmpJournal.pages = NULL;
  tmpJournal.last_attach_no = 0;
  tmpPage = NULL;
  tmpLayer = NULL;
  tmpItem = NULL;
  tmpFilename = filename_actual;
  error = NULL;
  tmpBg_pdf = NULL;
  maybe_pdf = TRUE;

  while (valid && !gzeof(f)) {
    len = gzread(f, buffer, 1000);
    if (len<0) valid = FALSE;
    if (maybe_pdf && len>=4 && !strncmp(buffer, "%PDF", 4))
      { valid = FALSE; break; } // most likely pdf
    else maybe_pdf = FALSE;
    if (len<=0) break;
    valid = g_markup_parse_context_parse(context, buffer, len, &error);
  }
  gzclose(f);
  if (valid) valid = g_markup_parse_context_end_parse(context, &error);
  if (tmpJournal.npages == 0) valid = FALSE;
  g_markup_parse_context_free(context);
  
  if (!valid) {
    g_free(filename_actual);
    delete_journal(&tmpJournal);
    if (!maybe_pdf) return FALSE;
    // essentially same as on_fileNewBackground from here on
    ui.saved = TRUE;
    close_journal();
    while (bgpdf.status != STATUS_NOT_INIT) gtk_main_iteration();
    new_journal();
    ui.zoom = ui.startup_zoom;
    gnome_canvas_set_pixels_per_unit(canvas, ui.zoom);
    update_page_stuff();
    return init_bgpdf(filename, TRUE, DOMAIN_ABSOLUTE);
  }
  
  ui.saved = TRUE; // force close_journal() to do its job
  close_journal();
  g_memmove(&journal, &tmpJournal, sizeof(struct Journal));
  
  // if we need to initialize a fresh pdf loader
  if (tmpBg_pdf!=NULL) { 
    while (bgpdf.status != STATUS_NOT_INIT) gtk_main_iteration();
    if (tmpBg_pdf->file_domain == DOMAIN_ATTACH)
      tmpfn = g_strdup_printf("%s.%s", filename_actual, tmpBg_pdf->filename->s);
    else
      tmpfn = g_strdup(tmpBg_pdf->filename->s);
    valid = init_bgpdf(tmpfn, FALSE, tmpBg_pdf->file_domain);
    // if file name is invalid: first try in xoj file's directory
    if (!valid && tmpBg_pdf->file_domain != DOMAIN_ATTACH) {
      p = g_path_get_dirname(filename);
      q = xo_basename(tmpfn, TRUE); // xoj may specify a cross-platform file path
      tmpfn2 = g_strdup_printf("%s%c%s", p, G_DIR_SEPARATOR, q);
      g_free(p);
      valid = init_bgpdf(tmpfn2, FALSE, tmpBg_pdf->file_domain);
      if (valid) {  // change the file name...
//      printf("substituting %s -> %s\n", tmpfn, tmpfn2);
        g_free(tmpBg_pdf->filename->s);
        tmpBg_pdf->filename->s = tmpfn2;
      }
      else g_free(tmpfn2);
    }
    // if file name is invalid: next prompt user
    if (!valid && tmpBg_pdf->file_domain != DOMAIN_ATTACH)
      if (user_wants_second_chance(&tmpfn)) {
        valid = init_bgpdf(tmpfn, FALSE, tmpBg_pdf->file_domain);
        if (valid) { // change the file name...
          g_free(tmpBg_pdf->filename->s);
          tmpBg_pdf->filename->s = g_strdup(tmpfn);
        }
      }
    if (valid) {
      refstring_unref(bgpdf.filename);
      bgpdf.filename = refstring_ref(tmpBg_pdf->filename);
    } else {
      dialog = gtk_message_dialog_new(GTK_WINDOW(winMain), GTK_DIALOG_MODAL,
        GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, _("Could not open background '%s'."),
        tmpfn);
      wrapper_gtk_dialog_run(GTK_DIALOG(dialog));
      gtk_widget_destroy(dialog);
    }
    g_free(tmpfn);
  }
  
  ui.pageno = 0;
  ui.cur_page = (struct Page *)journal.pages->data;
  ui.layerno = ui.cur_page->nlayers-1;
  ui.cur_layer = (struct Layer *)(g_list_last(ui.cur_page->layers)->data);
  ui.zoom = ui.startup_zoom;
  update_file_name(g_strdup(filename));
  gnome_canvas_set_pixels_per_unit(canvas, ui.zoom);
  make_canvas_items();
  update_page_stuff();
  rescale_bg_pixmaps(); // this requests the PDF pages if need be
  gtk_adjustment_set_value(gtk_layout_get_vadjustment(GTK_LAYOUT(canvas)), 0);
  
  if (strcmp(filename, filename_actual)) { // we just restored an autosave
    ui.saved = FALSE;
    dialog = gtk_message_dialog_new(GTK_WINDOW(winMain), GTK_DIALOG_MODAL,
        GTK_MESSAGE_OTHER, GTK_BUTTONS_YES_NO, 
        _("Save this version and delete auto-save?"));
    if (wrapper_gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
      if (save_journal(filename, FALSE)) { // success: delete autosave
        delete_autosave(filename_actual);
        ui.saved = TRUE;
      } else { // failed to save; keep 
        gtk_widget_destroy(dialog);
        dialog = gtk_message_dialog_new(GTK_WINDOW (winMain), GTK_DIALOG_DESTROY_WITH_PARENT,
           GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, _("Error saving file '%s'"), filename);
        wrapper_gtk_dialog_run(GTK_DIALOG(dialog));
      }
    }
    gtk_widget_destroy(dialog);
  }
  else ui.saved = TRUE;

  g_free(filename_actual);
  ui.need_autosave = !ui.saved;
  return TRUE;
}

/************ file backgrounds *************/

struct Background *attempt_load_pix_bg(char *filename, gboolean attach)
{
  struct Background *bg;
  GdkPixbuf *pix;
  
  pix = gdk_pixbuf_new_from_file(filename, NULL);
  if (pix == NULL) return NULL;
  
  bg = g_new(struct Background, 1);
  bg->type = BG_PIXMAP;
  bg->canvas_item = NULL;
  bg->pixbuf = pix;
  bg->pixbuf_scale = DEFAULT_ZOOM;
  if (attach) {
    bg->filename = new_refstring(NULL);
    bg->file_domain = DOMAIN_ATTACH;
  } else {
    bg->filename = new_refstring(filename);
    bg->file_domain = DOMAIN_ABSOLUTE;
  }
  return bg;
}

#define BUFSIZE 65536 // a reasonable buffer size for reads from gs pipe

GList *attempt_load_gv_bg(char *filename)
{
  struct Background *bg;
  GList *bg_list;
  GdkPixbuf *pix;
  GdkPixbufLoader *loader;
  FILE *gs_pipe, *f;
  unsigned char *buf;
  char *pipename, *quotedfilename;
  int buflen, remnlen, file_pageno;
  
  f = g_fopen(filename, "rb");
  if (f == NULL) return NULL;
  buf = g_malloc(BUFSIZE); // a reasonable buffer size
  if (fread(buf, 1, 4, f) !=4 ||
        (strncmp((char *)buf, "%!PS", 4) && strncmp((char *)buf, "%PDF", 4))) {
    fclose(f);
    g_free(buf);
    return NULL;
  }
  
  fclose(f);
  quotedfilename = g_shell_quote(filename);
  pipename = g_strdup_printf(GS_CMDLINE, (double)GS_BITMAP_DPI, quotedfilename);
  g_free(quotedfilename);
#ifdef WIN32
  gs_pipe = popen(pipename, "rb");
#else
  gs_pipe = popen(pipename, "r");
#endif
  g_free(pipename);
  
  bg_list = NULL;
  remnlen = 0;
  file_pageno = 0;
  loader = NULL;
  if (gs_pipe!=NULL)
  while (!feof(gs_pipe)) {
    if (!remnlen) { // new page: get a BMP header ?
      buflen = fread(buf, 1, 54, gs_pipe);
      if (buflen < 6) buflen += fread(buf, 1, 54-buflen, gs_pipe);
      if (buflen < 6 || buf[0]!='B' || buf[1]!='M') break; // fatal: abort
      remnlen = (int)(buf[5]<<24) + (buf[4]<<16) + (buf[3]<<8) + (buf[2]);
      loader = gdk_pixbuf_loader_new();
    }
    else buflen = fread(buf, 1, (remnlen < BUFSIZE)?remnlen:BUFSIZE, gs_pipe);
    remnlen -= buflen;
    if (buflen == 0) break;
    if (!gdk_pixbuf_loader_write(loader, buf, buflen, NULL)) break;
    if (remnlen == 0) { // make a new bg
      pix = gdk_pixbuf_loader_get_pixbuf(loader);
      if (pix == NULL) break;
      g_object_ref(pix);
      gdk_pixbuf_loader_close(loader, NULL);
      g_object_unref(loader);
      loader = NULL;
      bg = g_new(struct Background, 1);
      bg->canvas_item = NULL;
      bg->pixbuf = pix;
      bg->pixbuf_scale = (GS_BITMAP_DPI/72.0);
      bg->type = BG_PIXMAP;
      bg->filename = new_refstring(NULL);
      bg->file_domain = DOMAIN_ATTACH;
      file_pageno++;
      bg_list = g_list_append(bg_list, bg);
    }
  }
  if (loader != NULL) gdk_pixbuf_loader_close(loader, NULL);
  if (gs_pipe!=NULL) pclose(gs_pipe);
  g_free(buf);
  return bg_list;
}

struct Background *attempt_screenshot_bg(void)
{
#ifdef GDK_WINDOWING_X11
  struct Background *bg;
  GdkPixbuf *pix;
  XEvent x_event;
  GdkWindow *window;
  GdkColormap *cmap;
  int x,y,w,h;
  Window x_root, x_win;

  x_root = gdk_x11_get_default_root_xwindow();
  
  if (!XGrabButton(GDK_DISPLAY(), AnyButton, AnyModifier, x_root, 
      False, ButtonReleaseMask, GrabModeAsync, GrabModeSync, None, None))
    return NULL;

  XWindowEvent (GDK_DISPLAY(), x_root, ButtonReleaseMask, &x_event);
  XUngrabButton(GDK_DISPLAY(), AnyButton, AnyModifier, x_root);

  x_win = x_event.xbutton.subwindow;
  if (x_win == None) x_win = x_root;

  window = gdk_window_foreign_new_for_display(gdk_display_get_default(), x_win);
    
  gdk_window_get_geometry(window, &x, &y, &w, &h, NULL);
  cmap = gdk_drawable_get_colormap(window);
  if (cmap == NULL) cmap = gdk_colormap_get_system();
  
  pix = gdk_pixbuf_get_from_drawable(NULL, window,
     cmap, 0, 0, 0, 0, w, h);
    
  if (pix == NULL) return NULL;
  
  bg = g_new(struct Background, 1);
  bg->type = BG_PIXMAP;
  bg->canvas_item = NULL;
  bg->pixbuf = pix;
  bg->pixbuf_scale = DEFAULT_ZOOM;
  bg->filename = new_refstring(NULL);
  bg->file_domain = DOMAIN_ATTACH;
  return bg;
#else
  // not implemented on non-X11 backends
  return FALSE;
#endif
}

/************** pdf annotation ***************/

/* cancel a request */

void cancel_bgpdf_request(struct BgPdfRequest *req)
{
  GList *list_link;
  
  list_link = g_list_find(bgpdf.requests, req);
  if (list_link == NULL) return;
  // remove the request
  bgpdf.requests = g_list_delete_link(bgpdf.requests, list_link);
  g_free(req);
}

/* process a bg PDF request from the queue, and recurse */

gboolean bgpdf_scheduler_callback(gpointer data)
{
  struct BgPdfRequest *req;
  struct BgPdfPage *bgpg;
  GdkPixbuf *pixbuf;
  GtkWidget *dialog;
  PopplerPage *pdfpage;
  gdouble height, width;
  int scaled_height, scaled_width;
  GdkPixmap *pixmap;
  cairo_t *cr;

  // if all requests have been cancelled, remove ourselves from main loop
  if (bgpdf.requests == NULL) { bgpdf.pid = 0; return FALSE; }
  if (bgpdf.status == STATUS_NOT_INIT)
    { printf("DEBUG: BGPDF not initialized??\n"); bgpdf.pid = 0; return FALSE; }

  req = (struct BgPdfRequest *)bgpdf.requests->data;

  // use poppler to generate the page
  pixbuf = NULL;
  pdfpage = poppler_document_get_page(bgpdf.document, req->pageno-1);
  if (pdfpage) {
//    printf("DEBUG: Processing request for page %d at %f dpi\n", req->pageno, req->dpi);
    set_cursor_busy(TRUE);
    poppler_page_get_size(pdfpage, &width, &height);
    scaled_width = (int) (req->dpi * width/72);
    scaled_height = (int) (req->dpi * height/72);

    if (ui.poppler_force_cairo) { // poppler -> cairo -> pixmap -> pixbuf
      pixmap = gdk_pixmap_new(GTK_WIDGET(canvas)->window, scaled_width, scaled_height, -1);
      cr = gdk_cairo_create(pixmap);
      cairo_set_source_rgb(cr, 1., 1., 1.);
      cairo_paint(cr);
      cairo_scale(cr, scaled_width/width, scaled_height/height);
      poppler_page_render(pdfpage, cr);
      cairo_destroy(cr);
      pixbuf = gdk_pixbuf_get_from_drawable(NULL, GDK_DRAWABLE(pixmap),
        NULL, 0, 0, 0, 0, scaled_width, scaled_height);
      g_object_unref(pixmap);
    }
    else { // directly poppler -> pixbuf: faster, but bitmap font bug
      pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB,
                 FALSE, 8, scaled_width, scaled_height);
      wrapper_poppler_page_render_to_pixbuf(
                pdfpage, 0, 0, scaled_width, scaled_height,
                req->dpi/72, 0, pixbuf);
    }
    g_object_unref(pdfpage);
    set_cursor_busy(FALSE);
  }

  // process the generated pixbuf...
  if (pixbuf != NULL) { // success
    while (req->pageno > bgpdf.npages) {
      bgpg = g_new(struct BgPdfPage, 1);
      bgpg->pixbuf = NULL;
      bgpdf.pages = g_list_append(bgpdf.pages, bgpg);
      bgpdf.npages++;
    }
    bgpg = g_list_nth_data(bgpdf.pages, req->pageno-1);
    if (bgpg->pixbuf!=NULL) g_object_unref(bgpg->pixbuf);
    bgpg->pixbuf = pixbuf;
    bgpg->dpi = req->dpi;
    bgpg->pixel_height = scaled_height;
    bgpg->pixel_width = scaled_width;
    bgpdf_update_bg(req->pageno, bgpg); // update all pages that have this bg
  } else { // failure
    if (!bgpdf.has_failed) {
      dialog = gtk_message_dialog_new(GTK_WINDOW(winMain), GTK_DIALOG_MODAL,
        GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, _("Unable to render one or more PDF pages."));
      wrapper_gtk_dialog_run(GTK_DIALOG(dialog));
      gtk_widget_destroy(dialog);
    }
    bgpdf.has_failed = TRUE;
  }

  bgpdf.requests = g_list_delete_link(bgpdf.requests, bgpdf.requests);
  if (bgpdf.requests != NULL) return TRUE; // remain in the idle loop
  bgpdf.pid = 0;
  return FALSE; // we're done
}

/* make a request */

gboolean add_bgpdf_request(int pageno, double zoom)
{
  struct BgPdfRequest *req, *cmp_req;
  GList *list;

  if (bgpdf.status == STATUS_NOT_INIT)
    return FALSE; // don't accept requests
  req = g_new(struct BgPdfRequest, 1);
  req->pageno = pageno;
  req->dpi = 72*zoom;
//  printf("DEBUG: Enqueuing request for page %d at %f dpi\n", pageno, req->dpi);

  // cancel any request this may supersede
  for (list = bgpdf.requests; list != NULL; ) {
    cmp_req = (struct BgPdfRequest *)list->data;
    list = list->next;
    if (cmp_req->pageno == pageno) cancel_bgpdf_request(cmp_req);
  }

  // make the request
  bgpdf.requests = g_list_append(bgpdf.requests, req);
  if (!bgpdf.pid) bgpdf.pid = g_idle_add(bgpdf_scheduler_callback, NULL);
  return TRUE;
}

/* shutdown the PDF reader */

void shutdown_bgpdf(void)
{
  GList *list;
  struct BgPdfPage *pdfpg;
  struct BgPdfRequest *req;

  if (bgpdf.status == STATUS_NOT_INIT) return;
  
  // cancel all requests and free data structures
  refstring_unref(bgpdf.filename);
  for (list = bgpdf.pages; list != NULL; list = list->next) {
    pdfpg = (struct BgPdfPage *)list->data;
    if (pdfpg->pixbuf!=NULL) g_object_unref(pdfpg->pixbuf);
    g_free(pdfpg);
  }
  g_list_free(bgpdf.pages);
  for (list = bgpdf.requests; list != NULL; list = list->next) {
    req = (struct BgPdfRequest *)list->data;
    g_free(req);
  }
  g_list_free(bgpdf.requests);

  if (bgpdf.file_contents!=NULL) {
    g_free(bgpdf.file_contents); 
    bgpdf.file_contents = NULL;
  }
  if (bgpdf.document!=NULL) {
    g_object_unref(bgpdf.document);
    bgpdf.document = NULL;
  }

  bgpdf.status = STATUS_NOT_INIT;
}


// initialize PDF background rendering 

gboolean init_bgpdf(char *pdfname, gboolean create_pages, int file_domain)
{
  int i, n_pages;
  struct Background *bg;
  struct Page *pg;
  PopplerPage *pdfpage;
  gdouble width, height;
  gchar *uri;
  
  if (bgpdf.status != STATUS_NOT_INIT) return FALSE;
  
  // make a copy of the file in memory and check it's a PDF
  if (!g_file_get_contents(pdfname, &(bgpdf.file_contents), &(bgpdf.file_length), NULL))
    return FALSE;
  if (bgpdf.file_length < 4 || strncmp(bgpdf.file_contents, "%PDF", 4))
    { g_free(bgpdf.file_contents); bgpdf.file_contents = NULL; return FALSE; }

  // init bgpdf data structures and open poppler document
  bgpdf.status = STATUS_READY;
  bgpdf.filename = new_refstring((file_domain == DOMAIN_ATTACH) ? "bg.pdf" : pdfname);
  bgpdf.file_domain = file_domain;
  bgpdf.npages = 0;
  bgpdf.pages = NULL;
  bgpdf.requests = NULL;
  bgpdf.pid = 0;
  bgpdf.has_failed = FALSE;

/* poppler_document_new_from_data() starts at 0.6.1, but we want to
   be compatible with poppler 0.5.4 = latest in CentOS as of sept 2009 */
  uri = g_filename_to_uri(pdfname, NULL, NULL);
  if (!uri) uri = g_strdup_printf("file://%s", pdfname);
  bgpdf.document = poppler_document_new_from_file(uri, NULL, NULL);
  g_free(uri);
/*    with poppler 0.6.1 or later, can replace the above 4 lines by:
  bgpdf.document = poppler_document_new_from_data(bgpdf.file_contents, 
                          bgpdf.file_length, NULL, NULL);
*/
  if (bgpdf.document == NULL) { shutdown_bgpdf(); return FALSE; }
  
  if (pdfname[0]=='/' && ui.filename == NULL) {
    if (ui.default_path!=NULL) g_free(ui.default_path);
    ui.default_path = g_path_get_dirname(pdfname);
  }

  if (!create_pages) return TRUE; // we're done
  
  // create pages with correct sizes if requested
  n_pages = poppler_document_get_n_pages(bgpdf.document);
  for (i=1; i<=n_pages; i++) {
    pdfpage = poppler_document_get_page(bgpdf.document, i-1);
    if (!pdfpage) continue;
    if (journal.npages < i) {
      bg = g_new(struct Background, 1);
      bg->canvas_item = NULL;
      pg = NULL;
    } else {
      pg = (struct Page *)g_list_nth_data(journal.pages, i-1);
      bg = pg->bg;
    }
    bg->type = BG_PDF;
    bg->filename = refstring_ref(bgpdf.filename);
    bg->file_domain = bgpdf.file_domain;
    bg->file_page_seq = i;
    bg->pixbuf = NULL;
    bg->pixbuf_scale = 0;
    poppler_page_get_size(pdfpage, &width, &height);
    g_object_unref(pdfpage);
    if (pg == NULL) {
      pg = new_page_with_bg(bg, width, height);
      journal.pages = g_list_append(journal.pages, pg);
      journal.npages++;
    } else {
      pg->width = width; 
      pg->height = height;
      make_page_clipbox(pg);
      update_canvas_bg(pg);
    }
  }
  update_page_stuff();
  rescale_bg_pixmaps(); // this actually requests the pages !!
  return TRUE;
}


// look for all journal pages with given pdf bg, and update their bg pixmaps
void bgpdf_update_bg(int pageno, struct BgPdfPage *bgpg)
{
  GList *list;
  struct Page *pg;
  
  for (list = journal.pages; list!= NULL; list = list->next) {
    pg = (struct Page *)list->data;
    if (pg->bg->type == BG_PDF && pg->bg->file_page_seq == pageno) {
      if (pg->bg->pixbuf!=NULL) g_object_unref(pg->bg->pixbuf);
      pg->bg->pixbuf = g_object_ref(bgpg->pixbuf);
      pg->bg->pixel_width = bgpg->pixel_width;
      pg->bg->pixel_height = bgpg->pixel_height;
      update_canvas_bg(pg);
    }
  }
}

// initialize the recent files list
void init_mru(void)
{
  int i;
  gsize lfptr;
  char s[5];
  GIOChannel *f;
  gchar *str;
  GIOStatus status;
  
  g_strlcpy(s, "mru0", 5);
  for (s[3]='0', i=0; i<MRU_SIZE; s[3]++, i++) {
    ui.mrumenu[i] = GET_COMPONENT(s);
    ui.mru[i] = NULL;
  }
  f = g_io_channel_new_file(ui.mrufile, "r", NULL);
  if (f) status = G_IO_STATUS_NORMAL;
  else status = G_IO_STATUS_ERROR;
  i = 0;
  while (status == G_IO_STATUS_NORMAL && i<MRU_SIZE) {
    lfptr = 0;
    status = g_io_channel_read_line(f, &str, NULL, &lfptr, NULL);
    if (status == G_IO_STATUS_NORMAL && lfptr>0) {
      str[lfptr] = 0;
      ui.mru[i] = str;
      i++;
    }
  }
  if (f) {
    g_io_channel_shutdown(f, FALSE, NULL);
    g_io_channel_unref(f);
  }
  update_mru_menu();
}

void update_mru_menu(void)
{
  int i;
  gboolean anyone = FALSE;
  gchar *tmp;
  
  for (i=0; i<MRU_SIZE; i++) {
    if (ui.mru[i]!=NULL) {
      tmp = g_strdup_printf("_%d %s", i+1,
               g_strjoinv("__", g_strsplit_set(xo_basename(ui.mru[i], FALSE),"_",-1)));
      gtk_label_set_text_with_mnemonic(GTK_LABEL(gtk_bin_get_child(GTK_BIN(ui.mrumenu[i]))),
          tmp);
      g_free(tmp);
      gtk_widget_show(ui.mrumenu[i]);
      anyone = TRUE;
    }
    else gtk_widget_hide(ui.mrumenu[i]);
  }
  gtk_widget_set_sensitive(GET_COMPONENT("fileRecentFiles"), anyone);
}

void new_mru_entry(char *name)
{
  int i, j;
  
  for (i=0;i<MRU_SIZE;i++) 
    if (ui.mru[i]!=NULL && !strcmp(ui.mru[i], name)) {
      g_free(ui.mru[i]);
      for (j=i+1; j<MRU_SIZE; j++) ui.mru[j-1] = ui.mru[j];
      ui.mru[MRU_SIZE-1]=NULL;
    }
  if (ui.mru[MRU_SIZE-1]!=NULL) g_free(ui.mru[MRU_SIZE-1]);
  for (j=MRU_SIZE-1; j>=1; j--) ui.mru[j] = ui.mru[j-1];
  ui.mru[0] = g_strdup(name);
  update_mru_menu();
}

void delete_mru_entry(int which)
{
  int i;
  
  if (ui.mru[which]!=NULL) g_free(ui.mru[which]);
  for (i=which+1;i<MRU_SIZE;i++) 
    ui.mru[i-1] = ui.mru[i];
  ui.mru[MRU_SIZE-1] = NULL;
  update_mru_menu();
}

void save_mru_list(void)
{
  FILE *f;
  int i;
  
  f = g_fopen(ui.mrufile, "w");
  if (f==NULL) return;
  for (i=0; i<MRU_SIZE; i++)
    if (ui.mru[i]!=NULL) fprintf(f, "%s\n", ui.mru[i]);
  fclose(f);
}

void init_config_default(void)
{
  int i, j;

  DEFAULT_ZOOM = DISPLAY_DPI_DEFAULT/72.0;
  ui.zoom = ui.startup_zoom = 1.0*DEFAULT_ZOOM;
  ui.default_page.height = 792.0;
  ui.default_page.width = 612.0;
  ui.default_page.bg->type = BG_SOLID;
  ui.default_page.bg->color_no = COLOR_WHITE;
  ui.default_page.bg->color_rgba = predef_bgcolors_rgba[COLOR_WHITE];
  ui.default_page.bg->ruling = RULING_LINED;
  ui.view_continuous = VIEW_MODE_CONTINUOUS;
  ui.allow_xinput = TRUE;
  ui.discard_corepointer = FALSE;
  ui.ignore_other_devices = TRUE;
  ui.ignore_btn_reported_up = TRUE;
  ui.left_handed = FALSE;
  ui.shorten_menus = FALSE;
  ui.shorten_menu_items = g_strdup(DEFAULT_SHORTEN_MENUS);
  ui.auto_save_prefs = FALSE;
  ui.bg_apply_all_pages = FALSE;
  ui.new_page_bg_from_pdf = FALSE;
  ui.use_erasertip = FALSE;
  ui.window_default_width = 1000;
  ui.window_default_height = 700;
  ui.maximize_at_start = FALSE;
  ui.fullscreen = FALSE;
  ui.scrollbar_step_increment = 30;
  ui.zoom_step_increment = 1;
  ui.zoom_step_factor = 1.5;
  ui.progressive_bg = TRUE;
  ui.print_ruling = TRUE;
  ui.exportpdf_prefer_legacy = FALSE;
  ui.exportpdf_layers = FALSE;
  ui.default_unit = UNIT_CM;
  ui.default_path = NULL;
  ui.default_image = NULL;
  ui.default_font_name = g_strdup(DEFAULT_FONT);
  ui.default_font_size = DEFAULT_FONT_SIZE;
  ui.pressure_sensitivity = FALSE;
  ui.width_minimum_multiplier = 0.0;
  ui.width_maximum_multiplier = 1.25;
  ui.button_switch_mapping = FALSE;
  ui.autoload_pdf_xoj = FALSE;
  ui.autocreate_new_xoj = FALSE;
  ui.poppler_force_cairo = FALSE;
  ui.touch_as_handtool = FALSE;
  ui.pen_disables_touch = FALSE;
  ui.device_for_touch = g_strdup(DEFAULT_DEVICE_FOR_TOUCH);
  ui.autosave_enabled = FALSE;
  ui.autosave_filename_list = NULL;
  ui.autosave_delay = 5;
  ui.autosave_loop_running = FALSE;
  ui.autosave_need_catchup = FALSE;
  
  // the default UI vertical order
  ui.vertical_order[0][0] = 1; 
  ui.vertical_order[0][1] = 2; 
  ui.vertical_order[0][2] = 3; 
  ui.vertical_order[0][3] = 0; 
  ui.vertical_order[0][4] = 4;
  ui.vertical_order[1][0] = 2;
  ui.vertical_order[1][1] = 3;
  ui.vertical_order[1][2] = 0;
  ui.vertical_order[1][3] = ui.vertical_order[1][4] = -1;

  ui.toolno[0] = ui.startuptool = TOOL_PEN;
  for (i=1; i<=NUM_BUTTONS; i++) {
    ui.toolno[i] = TOOL_ERASER;
  }
  ui.toolno[NUM_BUTTONS+1] = TOOL_HAND; // special hand mapping
  for (i=0; i<=NUM_BUTTONS; i++)
    ui.linked_brush[i] = BRUSH_LINKED;
  ui.brushes[0][TOOL_PEN].color_no = COLOR_BLACK;
  ui.brushes[0][TOOL_ERASER].color_no = COLOR_WHITE;
  ui.brushes[0][TOOL_HIGHLIGHTER].color_no = COLOR_YELLOW;
  for (i=0; i < NUM_STROKE_TOOLS; i++) {
    ui.brushes[0][i].thickness_no = THICKNESS_MEDIUM;
    ui.brushes[0][i].tool_options = 0;
    ui.brushes[0][i].ruler = FALSE;
    ui.brushes[0][i].recognizer = FALSE;
    ui.brushes[0][i].variable_width = FALSE;
  }
  for (i=0; i< NUM_STROKE_TOOLS; i++)
    for (j=1; j<=NUM_BUTTONS; j++)
      g_memmove(&(ui.brushes[j][i]), &(ui.brushes[0][i]), sizeof(struct Brush));

  // predef_thickness is already initialized as a global variable
  GS_BITMAP_DPI = 144;
  PDFTOPPM_PRINTING_DPI = 150;
  
  ui.hiliter_opacity = 0.5;
  ui.pen_cursor = FALSE;
  
#if GTK_CHECK_VERSION(2,10,0)
  ui.print_settings = NULL;
#endif
  
}

#if GLIB_CHECK_VERSION(2,6,0)

void update_keyval(const gchar *group_name, const gchar *key,
                const gchar *comment, gchar *value)
{
  gboolean has_it = g_key_file_has_key(ui.config_data, group_name, key, NULL);
  g_key_file_set_value(ui.config_data, group_name, key, value);
  g_free(value);
  if (!has_it) g_key_file_set_comment(ui.config_data, group_name, key, comment, NULL);
}

#endif

const char *vorder_usernames[VBOX_MAIN_NITEMS+1] = 
  {"drawarea", "menu", "main_toolbar", "pen_toolbar", "statusbar", NULL};
  
gchar *verbose_vertical_order(int *order)
{
  gchar buf[80], *p; // longer than needed
  int i;

  p = buf;  
  for (i=0; i<VBOX_MAIN_NITEMS; i++) {
    if (order[i]<0 || order[i]>=VBOX_MAIN_NITEMS) continue;
    if (p!=buf) *(p++) = ' ';
    p = g_stpcpy(p, vorder_usernames[order[i]]);
  }
  return g_strdup(buf);
}

void save_config_to_file(void)
{
  gchar *buf;
  FILE *f;

#if GLIB_CHECK_VERSION(2,6,0)
  // no support for keyval files before Glib 2.6.0
  if (glib_minor_version<6) return; 

  // save some data...
  ui.maximize_at_start = (gdk_window_get_state(winMain->window) & GDK_WINDOW_STATE_MAXIMIZED);
  if (!ui.maximize_at_start && !ui.fullscreen)
    gdk_drawable_get_size(winMain->window, 
      &ui.window_default_width, &ui.window_default_height);

  setlocale(LC_NUMERIC, "C");

  update_keyval("general", "display_dpi",
    _(" the display resolution, in pixels per inch"),
    g_strdup_printf("%.2f", DEFAULT_ZOOM*72));
  update_keyval("general", "initial_zoom",
    _(" the initial zoom level, in percent"),
    g_strdup_printf("%.2f", 100*ui.zoom/DEFAULT_ZOOM));
  update_keyval("general", "window_maximize",
    _(" maximize the window at startup (true/false)"),
    g_strdup(ui.maximize_at_start?"true":"false"));
  update_keyval("general", "window_fullscreen",
    _(" start in full screen mode (true/false)"),
    g_strdup(ui.fullscreen?"true":"false"));
  update_keyval("general", "window_width",
    _(" the window width in pixels (when not maximized)"),
    g_strdup_printf("%d", ui.window_default_width));
  update_keyval("general", "window_height",
    _(" the window height in pixels"),
    g_strdup_printf("%d", ui.window_default_height));
  update_keyval("general", "scrollbar_speed",
    _(" scrollbar step increment (in pixels)"),
    g_strdup_printf("%d", ui.scrollbar_step_increment));
  update_keyval("general", "zoom_dialog_increment",
    _(" the step increment in the zoom dialog box"),
    g_strdup_printf("%d", ui.zoom_step_increment));
  update_keyval("general", "zoom_step_factor",
    _(" the multiplicative factor for zoom in/out"),
    g_strdup_printf("%.3f", ui.zoom_step_factor));
  update_keyval("general", "view_continuous",
    _(" continuous view (false = one page, true = continuous, horiz = horizontal)"),
    g_strdup(view_mode_names[ui.view_continuous]));
  update_keyval("general", "use_xinput",
    _(" use XInput extensions (true/false)"),
    g_strdup(ui.allow_xinput?"true":"false"));
  update_keyval("general", "discard_corepointer",
    _(" discard Core Pointer events in XInput mode (true/false)"),
    g_strdup(ui.discard_corepointer?"true":"false"));
  update_keyval("general", "ignore_other_devices",
    _(" ignore events from other devices while drawing (true/false)"),
    g_strdup(ui.ignore_other_devices?"true":"false"));
  update_keyval("general", "ignore_btn_reported_up",
    _(" do not worry if device reports button isn't pressed while drawing (true/false)"),
    g_strdup(ui.ignore_btn_reported_up?"true":"false"));
  update_keyval("general", "use_erasertip",
    _(" always map eraser tip to eraser (true/false)"),
    g_strdup(ui.use_erasertip?"true":"false"));
  update_keyval("general", "touchscreen_as_hand_tool",
    _(" always map touchscreen device to hand tool (true/false) (requires separate pen and touch devices)"),
    g_strdup(ui.touch_as_handtool?"true":"false"));
  update_keyval("general", "pen_disables_touch",
    _(" disable touchscreen device when pen is in proximity (true/false) (requires separate pen and touch devices)"),
    g_strdup(ui.pen_disables_touch?"true":"false"));
  update_keyval("general", "touchscreen_device_name",
    _(" name of touchscreen device for touchscreen_as_hand_tool"),
    g_strdup(ui.device_for_touch));
  update_keyval("general", "buttons_switch_mappings",
    _(" buttons 2 and 3 switch mappings instead of drawing (useful for some tablets) (true/false)"),
    g_strdup(ui.button_switch_mapping?"true":"false"));
  update_keyval("general", "autoload_pdf_xoj",
    _(" automatically load filename.pdf.xoj instead of filename.pdf (true/false)"),
    g_strdup(ui.autoload_pdf_xoj?"true":"false"));
  update_keyval("general", "autocreate_new_xoj",
    _(" when attempting to open a non-existent file, treat it as a new file (true/false)"),
    g_strdup(ui.autocreate_new_xoj?"true":"false"));
  update_keyval("general", "autosave_enabled",
    _(" enable periodic autosaves (true/false)"),
    g_strdup(ui.autosave_enabled?"true":"false"));
  update_keyval("general", "autosave_delay",
    _(" delay for periodic autosaves (in seconds)"),
    g_strdup_printf("%d", ui.autosave_delay));
  update_keyval("general", "default_path",
    _(" default path for open/save (leave blank for current directory)"),
    g_strdup((ui.default_path!=NULL)?ui.default_path:""));
  update_keyval("general", "pressure_sensitivity",
     _(" use pressure sensitivity to control pen stroke width (true/false)"),
     g_strdup(ui.pressure_sensitivity?"true":"false"));
  update_keyval("general", "width_minimum_multiplier",
     _(" minimum width multiplier"),
     g_strdup_printf("%.2f", ui.width_minimum_multiplier));
  update_keyval("general", "width_maximum_multiplier",
     _(" maximum width multiplier"),
     g_strdup_printf("%.2f", ui.width_maximum_multiplier));
  update_keyval("general", "interface_order",
    _(" interface components from top to bottom\n valid values: drawarea menu main_toolbar pen_toolbar statusbar"),
    verbose_vertical_order(ui.vertical_order[0]));
  update_keyval("general", "interface_fullscreen",
    _(" interface components in fullscreen mode, from top to bottom"),
    verbose_vertical_order(ui.vertical_order[1]));
  update_keyval("general", "interface_lefthanded",
    _(" interface has left-handed scrollbar (true/false)"),
    g_strdup(ui.left_handed?"true":"false"));
  update_keyval("general", "shorten_menus",
    _(" hide some unwanted menu or toolbar items (true/false)"),
    g_strdup(ui.shorten_menus?"true":"false"));
  update_keyval("general", "shorten_menu_items",
    _(" interface items to hide (customize at your own risk!)\n see source file xo-interface.c for a list of item names"),
    g_strdup(ui.shorten_menu_items));
  update_keyval("general", "highlighter_opacity",
    _(" highlighter opacity (0 to 1, default 0.5)\n warning: opacity level is not saved in xoj files!"),
    g_strdup_printf("%.2f", ui.hiliter_opacity));
  update_keyval("general", "autosave_prefs",
    _(" auto-save preferences on exit (true/false)"),
    g_strdup(ui.auto_save_prefs?"true":"false"));
  update_keyval("general", "poppler_force_cairo",
    _(" force PDF rendering through cairo (slower but nicer) (true/false)"),
    g_strdup(ui.poppler_force_cairo?"true":"false"));
  update_keyval("general", "exportpdf_prefer_legacy",
    _(" prefer xournal's own PDF code for exporting PDFs (true/false)"),
    g_strdup(ui.exportpdf_prefer_legacy?"true":"false"));
  update_keyval("general", "exportpdf_layers",
    _(" export successive layers on separate pages in PDFs (true/false)"),
    g_strdup(ui.exportpdf_layers?"true":"false"));

  update_keyval("paper", "width",
    _(" the default page width, in points (1/72 in)"),
    g_strdup_printf("%.2f", ui.default_page.width));
  update_keyval("paper", "height",
    _(" the default page height, in points (1/72 in)"),
    g_strdup_printf("%.2f", ui.default_page.height));
  update_keyval("paper", "color",
    _(" the default paper color"),
    (ui.default_page.bg->color_no>=0)?
    g_strdup(bgcolor_names[ui.default_page.bg->color_no]):
    g_strdup_printf("#%08x", ui.default_page.bg->color_rgba));
  update_keyval("paper", "style",
    _(" the default paper style (plain, lined, ruled, or graph)"),
    g_strdup(bgstyle_names[ui.default_page.bg->ruling]));
  update_keyval("paper", "apply_all",
    _(" apply paper style changes to all pages (true/false)"),
    g_strdup(ui.bg_apply_all_pages?"true":"false"));
  update_keyval("paper", "default_unit",
    _(" preferred unit (cm, in, px, pt)"),
    g_strdup(unit_names[ui.default_unit]));
  update_keyval("paper", "print_ruling",
    _(" include paper ruling when printing or exporting to PDF (true/false)"),
    g_strdup(ui.print_ruling?"true":"false"));
  update_keyval("paper", "new_page_duplicates_bg",
    _(" when creating a new page, duplicate a PDF or image background instead of using default paper (true/false)"),
    g_strdup(ui.new_page_bg_from_pdf?"true":"false"));
  update_keyval("paper", "progressive_bg",
    _(" just-in-time update of page backgrounds (true/false)"),
    g_strdup(ui.progressive_bg?"true":"false"));
  update_keyval("paper", "gs_bitmap_dpi",
    _(" bitmap resolution of PS/PDF backgrounds rendered using ghostscript (dpi)"),
    g_strdup_printf("%d", GS_BITMAP_DPI));
  update_keyval("paper", "pdftoppm_printing_dpi",
    _(" bitmap resolution of PDF backgrounds when printing with libgnomeprint (dpi)"),
    g_strdup_printf("%d", PDFTOPPM_PRINTING_DPI));

  update_keyval("tools", "startup_tool",
    _(" selected tool at startup (pen, eraser, highlighter, selectregion, selectrect, vertspace, hand, image)"),
    g_strdup(tool_names[ui.startuptool]));
  update_keyval("tools", "pen_cursor",
    _(" Use the pencil from cursor theme instead of a color dot (true/false)"),
    g_strdup(ui.pen_cursor?"true":"false"));
  update_keyval("tools", "pen_color",
    _(" default pen color"),
    (ui.default_brushes[TOOL_PEN].color_no>=0)?
    g_strdup(color_names[ui.default_brushes[TOOL_PEN].color_no]):
    g_strdup_printf("#%08x", ui.default_brushes[TOOL_PEN].color_rgba));
  update_keyval("tools", "pen_thickness",
    _(" default pen thickness (fine = 1, medium = 2, thick = 3)"),
    g_strdup_printf("%d", ui.default_brushes[TOOL_PEN].thickness_no));
  update_keyval("tools", "pen_ruler",
    _(" default pen is in ruler mode (true/false)"),
    g_strdup(ui.default_brushes[TOOL_PEN].ruler?"true":"false"));
  update_keyval("tools", "pen_recognizer",
    _(" default pen is in shape recognizer mode (true/false)"),
    g_strdup(ui.default_brushes[TOOL_PEN].recognizer?"true":"false"));
  update_keyval("tools", "eraser_thickness",
    _(" default eraser thickness (fine = 1, medium = 2, thick = 3)"),
    g_strdup_printf("%d", ui.default_brushes[TOOL_ERASER].thickness_no));
  update_keyval("tools", "eraser_mode",
    _(" default eraser mode (standard = 0, whiteout = 1, strokes = 2)"),
    g_strdup_printf("%d", ui.default_brushes[TOOL_ERASER].tool_options));
  update_keyval("tools", "highlighter_color",
    _(" default highlighter color"),
    (ui.default_brushes[TOOL_HIGHLIGHTER].color_no>=0)?
    g_strdup(color_names[ui.default_brushes[TOOL_HIGHLIGHTER].color_no]):
    g_strdup_printf("#%08x", ui.default_brushes[TOOL_HIGHLIGHTER].color_rgba));
  update_keyval("tools", "highlighter_thickness",
    _(" default highlighter thickness (fine = 1, medium = 2, thick = 3)"),
    g_strdup_printf("%d", ui.default_brushes[TOOL_HIGHLIGHTER].thickness_no));
  update_keyval("tools", "highlighter_ruler",
    _(" default highlighter is in ruler mode (true/false)"),
    g_strdup(ui.default_brushes[TOOL_HIGHLIGHTER].ruler?"true":"false"));
  update_keyval("tools", "highlighter_recognizer",
    _(" default highlighter is in shape recognizer mode (true/false)"),
    g_strdup(ui.default_brushes[TOOL_HIGHLIGHTER].recognizer?"true":"false"));
  update_keyval("tools", "btn2_tool",
    _(" button 2 tool (pen, eraser, highlighter, text, selectregion, selectrect, vertspace, hand, image)"),
    g_strdup(tool_names[ui.toolno[1]]));
  update_keyval("tools", "btn2_linked",
    _(" button 2 brush linked to primary brush (true/false) (overrides all other settings)"),
    g_strdup((ui.linked_brush[1]==BRUSH_LINKED)?"true":"false"));
  update_keyval("tools", "btn2_color",
    _(" button 2 brush color (for pen or highlighter only)"),
    (ui.toolno[1]<NUM_STROKE_TOOLS)?
      ((ui.brushes[1][ui.toolno[1]].color_no>=0)?
       g_strdup(color_names[ui.brushes[1][ui.toolno[1]].color_no]):
       g_strdup_printf("#%08x", ui.brushes[1][ui.toolno[1]].color_rgba)):
      g_strdup("white"));
  update_keyval("tools", "btn2_thickness",
    _(" button 2 brush thickness (pen, eraser, or highlighter only)"),
    g_strdup_printf("%d", (ui.toolno[1]<NUM_STROKE_TOOLS)?
                            ui.brushes[1][ui.toolno[1]].thickness_no:0));
  update_keyval("tools", "btn2_ruler",
    _(" button 2 ruler mode (true/false) (for pen or highlighter only)"),
    g_strdup(((ui.toolno[1]<NUM_STROKE_TOOLS)?
              ui.brushes[1][ui.toolno[1]].ruler:FALSE)?"true":"false"));
  update_keyval("tools", "btn2_recognizer",
    _(" button 2 shape recognizer mode (true/false) (pen or highlighter only)"),
    g_strdup(((ui.toolno[1]<NUM_STROKE_TOOLS)?
              ui.brushes[1][ui.toolno[1]].recognizer:FALSE)?"true":"false"));
  update_keyval("tools", "btn2_erasermode",
    _(" button 2 eraser mode (eraser only)"),
    g_strdup_printf("%d", ui.brushes[1][TOOL_ERASER].tool_options));
  update_keyval("tools", "btn3_tool",
    _(" button 3 tool (pen, eraser, highlighter, text, selectregion, selectrect, vertspace, hand, image)"),
    g_strdup(tool_names[ui.toolno[2]]));
  update_keyval("tools", "btn3_linked",
    _(" button 3 brush linked to primary brush (true/false) (overrides all other settings)"),
    g_strdup((ui.linked_brush[2]==BRUSH_LINKED)?"true":"false"));
  update_keyval("tools", "btn3_color",
    _(" button 3 brush color (for pen or highlighter only)"),
    (ui.toolno[2]<NUM_STROKE_TOOLS)?
      ((ui.brushes[2][ui.toolno[2]].color_no>=0)?
       g_strdup(color_names[ui.brushes[2][ui.toolno[2]].color_no]):
       g_strdup_printf("#%08x", ui.brushes[2][ui.toolno[2]].color_rgba)):
      g_strdup("white"));
  update_keyval("tools", "btn3_thickness",
    _(" button 3 brush thickness (pen, eraser, or highlighter only)"),
    g_strdup_printf("%d", (ui.toolno[2]<NUM_STROKE_TOOLS)?
                            ui.brushes[2][ui.toolno[2]].thickness_no:0));
  update_keyval("tools", "btn3_ruler",
    _(" button 3 ruler mode (true/false) (for pen or highlighter only)"),
    g_strdup(((ui.toolno[2]<NUM_STROKE_TOOLS)?
              ui.brushes[2][ui.toolno[2]].ruler:FALSE)?"true":"false"));
  update_keyval("tools", "btn3_recognizer",
    _(" button 3 shape recognizer mode (true/false) (pen or highlighter only)"),
    g_strdup(((ui.toolno[2]<NUM_STROKE_TOOLS)?
              ui.brushes[2][ui.toolno[2]].recognizer:FALSE)?"true":"false"));
  update_keyval("tools", "btn3_erasermode",
    _(" button 3 eraser mode (eraser only)"),
    g_strdup_printf("%d", ui.brushes[2][TOOL_ERASER].tool_options));

  update_keyval("tools", "pen_thicknesses",
    _(" thickness of the various pens (in points, 1 pt = 1/72 in)"),
    g_strdup_printf("%.2f;%.2f;%.2f;%.2f;%.2f", 
      predef_thickness[TOOL_PEN][0], predef_thickness[TOOL_PEN][1],
      predef_thickness[TOOL_PEN][2], predef_thickness[TOOL_PEN][3],
      predef_thickness[TOOL_PEN][4]));
  update_keyval("tools", "eraser_thicknesses",
    _(" thickness of the various erasers (in points, 1 pt = 1/72 in)"),
    g_strdup_printf("%.2f;%.2f;%.2f", 
      predef_thickness[TOOL_ERASER][1], predef_thickness[TOOL_ERASER][2],
      predef_thickness[TOOL_ERASER][3]));
  update_keyval("tools", "highlighter_thicknesses",
    _(" thickness of the various highlighters (in points, 1 pt = 1/72 in)"),
    g_strdup_printf("%.2f;%.2f;%.2f", 
      predef_thickness[TOOL_HIGHLIGHTER][1], predef_thickness[TOOL_HIGHLIGHTER][2],
      predef_thickness[TOOL_HIGHLIGHTER][3]));
  update_keyval("tools", "default_font",
    _(" name of the default font"),
    g_strdup(ui.default_font_name));
  update_keyval("tools", "default_font_size",
    _(" default font size"),
    g_strdup_printf("%.1f", ui.default_font_size));

  setlocale(LC_NUMERIC, "");

  buf = g_key_file_to_data(ui.config_data, NULL, NULL);
  if (buf == NULL) return;
  f = g_fopen(ui.configfile, "w");
  if (f==NULL) { g_free(buf); return; }
  fputs(buf, f);
  fclose(f);
  g_free(buf);
#endif
}

#if GLIB_CHECK_VERSION(2,6,0)
gboolean parse_keyval_float(const gchar *group, const gchar *key, double *val, double inf, double sup)
{
  gchar *ret, *end;
  double conv;
  
  ret = g_key_file_get_value(ui.config_data, group, key, NULL);
  if (ret==NULL) return FALSE;
  cleanup_numeric(ret);
  conv = g_ascii_strtod(ret, &end);
  if (*end!=0) { g_free(ret); return FALSE; }
  g_free(ret);
  if (conv < inf || conv > sup) return FALSE;
  *val = conv;
  return TRUE;
}

gboolean parse_keyval_floatlist(const gchar *group, const gchar *key, double *val, int n, double inf, double sup)
{
  gchar *ret, *end;
  double conv[5];
  int i;

  if (n>5) return FALSE;
  ret = g_key_file_get_value(ui.config_data, group, key, NULL);
  if (ret==NULL) return FALSE;
  cleanup_numeric(ret);
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

gboolean parse_keyval_int(const gchar *group, const gchar *key, int *val, int inf, int sup)
{
  gchar *ret, *end;
  int conv;
  
  ret = g_key_file_get_value(ui.config_data, group, key, NULL);
  if (ret==NULL) return FALSE;
  conv = strtol(ret, &end, 10);
  if (*end!=0) { g_free(ret); return FALSE; }
  g_free(ret);
  if (conv < inf || conv > sup) return FALSE;
  *val = conv;
  return TRUE;
}

gboolean parse_keyval_enum(const gchar *group, const gchar *key, int *val, const char **names, int n)
{
  gchar *ret;
  int i;
  
  ret = g_key_file_get_value(ui.config_data, group, key, NULL);
  if (ret==NULL) return FALSE;
  for (i=0; i<n; i++) {
    if (!names[i][0]) continue; // "" is for invalid values
    if (!g_ascii_strcasecmp(ret, names[i]))
      { *val = i; g_free(ret); return TRUE; }
  }
  return FALSE;
}

gboolean parse_keyval_enum_color(const gchar *group, const gchar *key, int *val, guint *val_rgba, 
                                 const char **names, const guint *predef_rgba, int n)
{
  gchar *ret;
  int i;
  
  ret = g_key_file_get_value(ui.config_data, group, key, NULL);
  if (ret==NULL) return FALSE;
  for (i=0; i<n; i++) {
    if (!names[i][0]) continue; // "" is for invalid values
    if (!g_ascii_strcasecmp(ret, names[i]))
      { *val = i; *val_rgba = predef_rgba[i]; g_free(ret); return TRUE; }
  }
  if (ret[0]=='#') {
    *val = COLOR_OTHER;
    *val_rgba = strtoul(ret+1, NULL, 16);
    g_free(ret);
    return TRUE;
  }
  return FALSE;
}

gboolean parse_keyval_boolean(const gchar *group, const gchar *key, gboolean *val)
{
  gchar *ret;
  
  ret = g_key_file_get_value(ui.config_data, group, key, NULL);
  if (ret==NULL) return FALSE;
  if (!g_ascii_strcasecmp(ret, "true")) 
    { *val = TRUE; g_free(ret); return TRUE; }
  if (!g_ascii_strcasecmp(ret, "false")) 
    { *val = FALSE; g_free(ret); return TRUE; }
  g_free(ret);
  return TRUE;
}

gboolean parse_keyval_string(const gchar *group, const gchar *key, gchar **val)
{
  gchar *ret;
  
  ret = g_key_file_get_value(ui.config_data, group, key, NULL);
  if (ret==NULL) return FALSE;
  if (strlen(ret) == 0) {
    *val = NULL;
    g_free(ret);
  } 
  else *val = ret;
  return TRUE;
}

gboolean parse_keyval_vorderlist(const gchar *group, const gchar *key, int *order)
{
  gchar *ret, *p;
  int tmp[VBOX_MAIN_NITEMS];
  int i, n, l;

  ret = g_key_file_get_value(ui.config_data, group, key, NULL);
  if (ret==NULL) return FALSE;
  
  for (i=0; i<VBOX_MAIN_NITEMS; i++) tmp[i] = -1;
  n = 0; p = ret;
  while (*p==' ') p++;
  while (*p!=0) {
    if (n>VBOX_MAIN_NITEMS) return FALSE; // too many items
    for (i=0; i<VBOX_MAIN_NITEMS; i++) {
      if (!g_str_has_prefix(p, vorder_usernames[i])) continue;
      l = strlen(vorder_usernames[i]);
      if (p[l]==' '||p[l]==0) { p+=l; break; }
    }
    if (i>=VBOX_MAIN_NITEMS) { g_free(ret); return FALSE; } // parse error
    // we found item #i
    tmp[n++] = i;
    while (*p==' ') p++;
  }
  
  for (n=0; n<VBOX_MAIN_NITEMS; n++) order[n] = tmp[n];
  g_free(ret);
  return TRUE;
}

#endif

void load_config_from_file(void)
{
  double f;
  gboolean b;
  int i, j;
  gchar *str;
  
#if GLIB_CHECK_VERSION(2,6,0)
  // no support for keyval files before Glib 2.6.0
  if (glib_minor_version<6) return; 
  ui.config_data = g_key_file_new();
  if (!g_key_file_load_from_file(ui.config_data, ui.configfile, 
         G_KEY_FILE_KEEP_COMMENTS, NULL)) {
    g_key_file_free(ui.config_data);
    ui.config_data = g_key_file_new();
    g_key_file_set_comment(ui.config_data, NULL, NULL, 
         _(" Xournal configuration file.\n"
           " This file is generated automatically upon saving preferences.\n"
           " Use caution when editing this file manually.\n"), NULL);
    return;
  }

  // parse keys from the keyfile to set defaults
  if (parse_keyval_float("general", "display_dpi", &f, 10., 500.))
    DEFAULT_ZOOM = f/72.0;
  if (parse_keyval_float("general", "initial_zoom", &f, 
              MIN_ZOOM*100/DEFAULT_ZOOM, MAX_ZOOM*100/DEFAULT_ZOOM))
    ui.zoom = ui.startup_zoom = DEFAULT_ZOOM*f/100.0;
  parse_keyval_boolean("general", "window_maximize", &ui.maximize_at_start);
  parse_keyval_boolean("general", "window_fullscreen", &ui.fullscreen);
  parse_keyval_int("general", "window_width", &ui.window_default_width, 10, 5000);
  parse_keyval_int("general", "window_height", &ui.window_default_height, 10, 5000);
  parse_keyval_int("general", "scrollbar_speed", &ui.scrollbar_step_increment, 1, 5000);
  parse_keyval_int("general", "zoom_dialog_increment", &ui.zoom_step_increment, 1, 500);
  parse_keyval_float("general", "zoom_step_factor", &ui.zoom_step_factor, 1., 5.);
  parse_keyval_enum("general", "view_continuous", &ui.view_continuous, view_mode_names, 3);
  parse_keyval_boolean("general", "use_xinput", &ui.allow_xinput);
  parse_keyval_boolean("general", "discard_corepointer", &ui.discard_corepointer);
  parse_keyval_boolean("general", "ignore_other_devices", &ui.ignore_other_devices);
  parse_keyval_boolean("general", "ignore_btn_reported_up", &ui.ignore_btn_reported_up);
  parse_keyval_boolean("general", "use_erasertip", &ui.use_erasertip);
  parse_keyval_boolean("general", "touchscreen_as_hand_tool", &ui.touch_as_handtool);
  parse_keyval_boolean("general", "pen_disables_touch", &ui.pen_disables_touch);
  if (parse_keyval_string("general", "touchscreen_device_name", &str))
    if (str!=NULL) ui.device_for_touch = str;
  parse_keyval_boolean("general", "buttons_switch_mappings", &ui.button_switch_mapping);
  parse_keyval_boolean("general", "autoload_pdf_xoj", &ui.autoload_pdf_xoj);
  parse_keyval_boolean("general", "autocreate_new_xoj", &ui.autocreate_new_xoj);
  parse_keyval_boolean("general", "autosave_enabled", &ui.autosave_enabled);
  parse_keyval_int("general", "autosave_delay", &ui.autosave_delay, 1, 3600);
  parse_keyval_string("general", "default_path", &ui.default_path);
  parse_keyval_boolean("general", "pressure_sensitivity", &ui.pressure_sensitivity);
  parse_keyval_float("general", "width_minimum_multiplier", &ui.width_minimum_multiplier, 0., 10.);
  parse_keyval_float("general", "width_maximum_multiplier", &ui.width_maximum_multiplier, 0., 10.);

  parse_keyval_vorderlist("general", "interface_order", ui.vertical_order[0]);
  parse_keyval_vorderlist("general", "interface_fullscreen", ui.vertical_order[1]);
  parse_keyval_boolean("general", "interface_lefthanded", &ui.left_handed);
  parse_keyval_boolean("general", "shorten_menus", &ui.shorten_menus);
  if (parse_keyval_string("general", "shorten_menu_items", &str))
    if (str!=NULL) { g_free(ui.shorten_menu_items); ui.shorten_menu_items = str; }
  parse_keyval_float("general", "highlighter_opacity", &ui.hiliter_opacity, 0., 1.);
  parse_keyval_boolean("general", "autosave_prefs", &ui.auto_save_prefs);
  parse_keyval_boolean("general", "poppler_force_cairo", &ui.poppler_force_cairo);
  parse_keyval_boolean("general", "exportpdf_prefer_legacy", &ui.exportpdf_prefer_legacy);
  parse_keyval_boolean("general", "exportpdf_layers", &ui.exportpdf_layers);
  
  parse_keyval_float("paper", "width", &ui.default_page.width, 1., 5000.);
  parse_keyval_float("paper", "height", &ui.default_page.height, 1., 5000.);
  parse_keyval_enum_color("paper", "color", 
     &(ui.default_page.bg->color_no), &(ui.default_page.bg->color_rgba), 
     bgcolor_names, predef_bgcolors_rgba, COLOR_MAX);
  parse_keyval_enum("paper", "style", &(ui.default_page.bg->ruling), bgstyle_names, 4);
  parse_keyval_boolean("paper", "apply_all", &ui.bg_apply_all_pages);
  parse_keyval_enum("paper", "default_unit", &ui.default_unit, unit_names, 4);
  parse_keyval_boolean("paper", "progressive_bg", &ui.progressive_bg);
  parse_keyval_boolean("paper", "print_ruling", &ui.print_ruling);
  parse_keyval_boolean("paper", "new_page_duplicates_bg", &ui.new_page_bg_from_pdf);
  parse_keyval_int("paper", "gs_bitmap_dpi", &GS_BITMAP_DPI, 1, 1200);
  parse_keyval_int("paper", "pdftoppm_printing_dpi", &PDFTOPPM_PRINTING_DPI, 1, 1200);

  parse_keyval_enum("tools", "startup_tool", &ui.startuptool, tool_names, NUM_TOOLS);
  ui.toolno[0] = ui.startuptool;
  parse_keyval_boolean("tools", "pen_cursor", &ui.pen_cursor);
  parse_keyval_enum_color("tools", "pen_color", 
     &(ui.brushes[0][TOOL_PEN].color_no), &(ui.brushes[0][TOOL_PEN].color_rgba),
     color_names, predef_colors_rgba, COLOR_MAX);
  parse_keyval_int("tools", "pen_thickness", &(ui.brushes[0][TOOL_PEN].thickness_no), 0, 4);
  parse_keyval_boolean("tools", "pen_ruler", &(ui.brushes[0][TOOL_PEN].ruler));
  parse_keyval_boolean("tools", "pen_recognizer", &(ui.brushes[0][TOOL_PEN].recognizer));
  parse_keyval_int("tools", "eraser_thickness", &(ui.brushes[0][TOOL_ERASER].thickness_no), 1, 3);
  parse_keyval_int("tools", "eraser_mode", &(ui.brushes[0][TOOL_ERASER].tool_options), 0, 2);
  parse_keyval_enum_color("tools", "highlighter_color", 
     &(ui.brushes[0][TOOL_HIGHLIGHTER].color_no), &(ui.brushes[0][TOOL_HIGHLIGHTER].color_rgba),
     color_names, predef_colors_rgba, COLOR_MAX);
  parse_keyval_int("tools", "highlighter_thickness", &(ui.brushes[0][TOOL_HIGHLIGHTER].thickness_no), 0, 4);
  parse_keyval_boolean("tools", "highlighter_ruler", &(ui.brushes[0][TOOL_HIGHLIGHTER].ruler));
  parse_keyval_boolean("tools", "highlighter_recognizer", &(ui.brushes[0][TOOL_HIGHLIGHTER].recognizer));
  ui.brushes[0][TOOL_PEN].variable_width = ui.pressure_sensitivity;
  for (i=0; i< NUM_STROKE_TOOLS; i++)
    for (j=1; j<=NUM_BUTTONS; j++)
      g_memmove(&(ui.brushes[j][i]), &(ui.brushes[0][i]), sizeof(struct Brush));

  parse_keyval_enum("tools", "btn2_tool", &(ui.toolno[1]), tool_names, NUM_TOOLS);
  if (parse_keyval_boolean("tools", "btn2_linked", &b))
    ui.linked_brush[1] = b?BRUSH_LINKED:BRUSH_STATIC;
  parse_keyval_enum("tools", "btn3_tool", &(ui.toolno[2]), tool_names, NUM_TOOLS);
  if (parse_keyval_boolean("tools", "btn3_linked", &b))
    ui.linked_brush[2] = b?BRUSH_LINKED:BRUSH_STATIC;
  if (ui.linked_brush[1]!=BRUSH_LINKED) {
    if (ui.toolno[1]==TOOL_PEN || ui.toolno[1]==TOOL_HIGHLIGHTER) {
      parse_keyval_boolean("tools", "btn2_ruler", &(ui.brushes[1][ui.toolno[1]].ruler));
      parse_keyval_boolean("tools", "btn2_recognizer", &(ui.brushes[1][ui.toolno[1]].recognizer));
      parse_keyval_enum_color("tools", "btn2_color", 
         &(ui.brushes[1][ui.toolno[1]].color_no), &(ui.brushes[1][ui.toolno[1]].color_rgba), 
         color_names, predef_colors_rgba, COLOR_MAX);
    }
    if (ui.toolno[1]<NUM_STROKE_TOOLS)
      parse_keyval_int("tools", "btn2_thickness", &(ui.brushes[1][ui.toolno[1]].thickness_no), 0, 4);
    if (ui.toolno[1]==TOOL_ERASER)
      parse_keyval_int("tools", "btn2_erasermode", &(ui.brushes[1][TOOL_ERASER].tool_options), 0, 2);
  }
  if (ui.linked_brush[2]!=BRUSH_LINKED) {
    if (ui.toolno[2]==TOOL_PEN || ui.toolno[2]==TOOL_HIGHLIGHTER) {
      parse_keyval_boolean("tools", "btn3_ruler", &(ui.brushes[2][ui.toolno[2]].ruler));
      parse_keyval_boolean("tools", "btn3_recognizer", &(ui.brushes[2][ui.toolno[2]].recognizer));
      parse_keyval_enum_color("tools", "btn3_color", 
         &(ui.brushes[2][ui.toolno[2]].color_no), &(ui.brushes[2][ui.toolno[2]].color_rgba), 
         color_names, predef_colors_rgba, COLOR_MAX);
    }
    if (ui.toolno[2]<NUM_STROKE_TOOLS)
      parse_keyval_int("tools", "btn3_thickness", &(ui.brushes[2][ui.toolno[2]].thickness_no), 0, 4);
    if (ui.toolno[2]==TOOL_ERASER)
      parse_keyval_int("tools", "btn3_erasermode", &(ui.brushes[2][TOOL_ERASER].tool_options), 0, 2);
  }
  parse_keyval_floatlist("tools", "pen_thicknesses", predef_thickness[TOOL_PEN], 5, 0.01, 1000.0);
  parse_keyval_floatlist("tools", "eraser_thicknesses", predef_thickness[TOOL_ERASER]+1, 3, 0.01, 1000.0);
  parse_keyval_floatlist("tools", "highlighter_thicknesses", predef_thickness[TOOL_HIGHLIGHTER]+1, 3, 0.01, 1000.0);
  if (parse_keyval_string("tools", "default_font", &str))
    if (str!=NULL) { g_free(ui.default_font_name); ui.default_font_name = str; }
  parse_keyval_float("tools", "default_font_size", &ui.default_font_size, 1., 200.);
#endif
}
