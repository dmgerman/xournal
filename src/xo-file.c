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
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <locale.h>

#include "xournal.h"
#include "xo-interface.h"
#include "xo-support.h"
#include "xo-callbacks.h"
#include "xo-misc.h"
#include "xo-file.h"
#include "xo-paint.h"

const char *tool_names[NUM_TOOLS] = {"pen", "eraser", "highlighter", "text", "", "selectrect", "vertspace", "hand"};
const char *color_names[COLOR_MAX] = {"black", "blue", "red", "green",
   "gray", "lightblue", "lightgreen", "magenta", "orange", "yellow", "white"};
const char *bgtype_names[3] = {"solid", "pixmap", "pdf"};
const char *bgcolor_names[COLOR_MAX] = {"", "blue", "pink", "green",
   "", "", "", "", "orange", "yellow", "white"};
const char *bgstyle_names[4] = {"plain", "lined", "ruled", "graph"};
const char *file_domain_names[3] = {"absolute", "attach", "clone"};
const char *unit_names[4] = {"cm", "in", "px", "pt"};
int PDFTOPPM_PRINTING_DPI, GS_BITMAP_DPI;

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

// saves the journal to a file: returns true on success, false on error

gboolean save_journal(const char *filename)
{
  gzFile f;
  struct Page *pg, *tmppg;
  struct Layer *layer;
  struct Item *item;
  int i, is_clone;
  char *tmpfn, *tmpstr;
  gchar *pdfbuf;
  gsize pdflen;
  gboolean success;
  FILE *tmpf;
  GList *pagelist, *layerlist, *itemlist, *list;
  GtkWidget *dialog;
  
  f = gzopen(filename, "w");
  if (f==NULL) return FALSE;
  chk_attach_names();

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
          if (!gdk_pixbuf_save(pg->bg->pixbuf, tmpfn, "png", NULL, NULL)) {
            dialog = gtk_message_dialog_new(GTK_WINDOW(winMain), GTK_DIALOG_MODAL,
              GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, 
              "Could not write background '%s'. Continuing anyway.", tmpfn);
            gtk_dialog_run(GTK_DIALOG(dialog));
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
          if (bgpdf.status != STATUS_NOT_INIT &&
              g_file_get_contents(bgpdf.tmpfile_copy, &pdfbuf, &pdflen, NULL))
          {
            tmpf = fopen(tmpfn, "w");
            if (tmpf != NULL && fwrite(pdfbuf, 1, pdflen, tmpf) == pdflen)
              success = TRUE;
            g_free(pdfbuf);
            fclose(tmpf);
          }
          if (!success) {
            dialog = gtk_message_dialog_new(GTK_WINDOW(winMain), GTK_DIALOG_MODAL,
              GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, 
              "Could not write background '%s'. Continuing anyway.", tmpfn);
            gtk_dialog_run(GTK_DIALOG(dialog));
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
          gzprintf(f, "\" width=\"%.2f\">\n", item->brush.thickness);
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
          gzprintf(f, "\">%s</text>\n", tmpstr);
          g_free(tmpstr);
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

// closes a journal: returns true on success, false on abort

gboolean close_journal(void)
{
  if (!ok_to_close()) return FALSE;
  
  // free everything...
  reset_selection();
  clear_redo_stack();
  clear_undo_stack();

  shutdown_bgpdf();
  delete_journal(&journal);
  
  return TRUE;
  /* note: various members of ui and journal are now in invalid states,
     use new_journal() to reinitialize them */
}

// sanitize a string containing floats, in case it may have , instead of .

void cleanup_numeric(char *s)
{
  while (*s!=0) { if (*s==',') *s='.'; s++; }
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
  return g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT, "Invalid file contents");
}

void xoj_parser_start_element(GMarkupParseContext *context,
   const gchar *element_name, const gchar **attribute_names, 
   const gchar **attribute_values, gpointer user_data, GError **error)
{
  int has_attr, i;
  char *ptr;
  struct Background *tmpbg;
  char *tmpbg_filename;
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
          tmpPage->bg->color_rgba = strtol(*attribute_values + 1, &ptr, 16);
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
          if (tmpbg->pixbuf!=NULL) gdk_pixbuf_ref(tmpbg->pixbuf);
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
                "Could not open background '%s'. Setting background to white.",
                tmpbg_filename);
              gtk_dialog_run(GTK_DIALOG(dialog));
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
          tmpItem->brush.color_rgba = strtol(*attribute_values + 1, &ptr, 16);
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
          tmpItem->brush.color_rgba = strtol(*attribute_values + 1, &ptr, 16);
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
      n++;
    }
    if (n<4 || n&1) { *error = xoj_invalid(); return; }
    tmpItem->path = gnome_canvas_points_new(n/2);
    g_memmove(tmpItem->path->coords, ui.cur_path.coords, n*sizeof(double));
  }
  if (!strcmp(element_name, "text")) {
    tmpItem->text = g_malloc(text_len+1);
    g_memmove(tmpItem->text, text, text_len);
    tmpItem->text[text_len]=0;
  }
}

gboolean user_wants_second_chance(char **filename)
{
  GtkWidget *dialog;
  GtkFileFilter *filt_all, *filt_pdf;
  GtkResponseType response;

  dialog = gtk_message_dialog_new(GTK_WINDOW(winMain), GTK_DIALOG_MODAL,
    GTK_MESSAGE_ERROR, GTK_BUTTONS_YES_NO, 
    "Could not open background '%s'.\nSelect another file?",
    *filename);
  response = gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
  if (response != GTK_RESPONSE_YES) return FALSE;
  dialog = gtk_file_chooser_dialog_new("Open PDF", GTK_WINDOW (winMain),
     GTK_FILE_CHOOSER_ACTION_OPEN, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
     GTK_STOCK_OPEN, GTK_RESPONSE_OK, NULL);

  filt_all = gtk_file_filter_new();
  gtk_file_filter_set_name(filt_all, "All files");
  gtk_file_filter_add_pattern(filt_all, "*");
  filt_pdf = gtk_file_filter_new();
  gtk_file_filter_set_name(filt_pdf, "PDF files");
  gtk_file_filter_add_pattern(filt_pdf, "*.pdf");
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER (dialog), filt_pdf);
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER (dialog), filt_all);

  if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_OK) {
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
  gchar *tmpfn;
  gboolean maybe_pdf;
  
  f = gzopen(filename, "r");
  if (f==NULL) return FALSE;
  
  context = g_markup_parse_context_new(&parser, 0, NULL, NULL);
  valid = TRUE;
  tmpJournal.npages = 0;
  tmpJournal.pages = NULL;
  tmpJournal.last_attach_no = 0;
  tmpPage = NULL;
  tmpLayer = NULL;
  tmpItem = NULL;
  tmpFilename = filename;
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
      tmpfn = g_strdup_printf("%s.%s", filename, tmpBg_pdf->filename->s);
    else
      tmpfn = g_strdup(tmpBg_pdf->filename->s);
    valid = init_bgpdf(tmpfn, FALSE, tmpBg_pdf->file_domain);
    // in case the file name became invalid
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
        GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "Could not open background '%s'.",
        tmpfn);
      gtk_dialog_run(GTK_DIALOG(dialog));
      gtk_widget_destroy(dialog);
    }
    g_free(tmpfn);
  }
  
  ui.pageno = 0;
  ui.cur_page = (struct Page *)journal.pages->data;
  ui.layerno = ui.cur_page->nlayers-1;
  ui.cur_layer = (struct Layer *)(g_list_last(ui.cur_page->layers)->data);
  ui.saved = TRUE;
  ui.zoom = ui.startup_zoom;
  update_file_name(g_strdup(filename));
  gnome_canvas_set_pixels_per_unit(canvas, ui.zoom);
  make_canvas_items();
  update_page_stuff();
  gtk_adjustment_set_value(gtk_layout_get_vadjustment(GTK_LAYOUT(canvas)), 0);
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
  char *pipename;
  int buflen, remnlen, file_pageno;
  
  f = fopen(filename, "r");
  if (f == NULL) return NULL;
  buf = g_malloc(BUFSIZE); // a reasonable buffer size
  if (fread(buf, 1, 4, f) !=4 ||
        (strncmp((char *)buf, "%!PS", 4) && strncmp((char *)buf, "%PDF", 4))) {
    fclose(f);
    g_free(buf);
    return NULL;
  }
  
  fclose(f);
  pipename = g_strdup_printf(GS_CMDLINE, (double)GS_BITMAP_DPI, filename);
  gs_pipe = popen(pipename, "r");
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
      gdk_pixbuf_ref(pix);
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
  pclose(gs_pipe);
  g_free(buf);
  return bg_list;
}

struct Background *attempt_screenshot_bg(void)
{
  struct Background *bg;
  GdkPixbuf *pix;
  XEvent x_event;
  GError *error = NULL;
  GdkWindow *window;
  int x,y,w,h, status;
  unsigned int tmp;
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
  
  pix = gdk_pixbuf_get_from_drawable(NULL, window,
    gdk_colormap_get_system(), 0, 0, 0, 0, w, h);
    
  if (pix == NULL) return NULL;
  
  bg = g_new(struct Background, 1);
  bg->type = BG_PIXMAP;
  bg->canvas_item = NULL;
  bg->pixbuf = pix;
  bg->pixbuf_scale = DEFAULT_ZOOM;
  bg->filename = new_refstring(NULL);
  bg->file_domain = DOMAIN_ATTACH;
  return bg;
}

/************** pdf annotation ***************/

/* free tmp directory */

void end_bgpdf_shutdown(void)
{
  if (bgpdf.tmpdir!=NULL) {
    if (bgpdf.tmpfile_copy!=NULL) {
      g_unlink(bgpdf.tmpfile_copy);
      g_free(bgpdf.tmpfile_copy);
      bgpdf.tmpfile_copy = NULL;
    }
    g_rmdir(bgpdf.tmpdir);  
    g_free(bgpdf.tmpdir);
    bgpdf.tmpdir = NULL;
  }
  bgpdf.status = STATUS_NOT_INIT;
}

/* cancel a request */

void cancel_bgpdf_request(struct BgPdfRequest *req)
{
  GList *list_link;
  
  list_link = g_list_find(bgpdf.requests, req);
  if (list_link == NULL) return;
  if (list_link->prev == NULL && bgpdf.pid > 0) {
    // this is being processed: kill the child but don't remove the request yet
    if (bgpdf.status == STATUS_RUNNING) bgpdf.status = STATUS_ABORTED;
    kill(bgpdf.pid, SIGHUP);
//    printf("Cancelling a request - killing %d\n", bgpdf.pid);
  }
  else {
    // remove the request
    bgpdf.requests = g_list_delete_link(bgpdf.requests, list_link);
    g_free(req);
//    printf("Cancelling a request - no kill needed\n");
  }
}

/* sigchld callback */

void bgpdf_child_handler(GPid pid, gint status, gpointer data)
{
  struct BgPdfRequest *req;
  struct BgPdfPage *bgpg;
  gchar *ppm_name;
  GdkPixbuf *pixbuf;
  int npad, ret;
  
  if (bgpdf.requests == NULL) return;
  req = (struct BgPdfRequest *)bgpdf.requests->data;
  
  pixbuf = NULL;
  // pdftoppm used to generate p-nnnnnn.ppm (6 digits); new versions produce variable width
  for (npad = 6; npad>0; npad--) {
     ppm_name = g_strdup_printf("%s/p-%0*d.ppm", bgpdf.tmpdir, npad, req->pageno);
     if (bgpdf.status != STATUS_ABORTED && bgpdf.status != STATUS_SHUTDOWN)
       pixbuf = gdk_pixbuf_new_from_file(ppm_name, NULL);
     ret = unlink(ppm_name);
     g_free(ppm_name);
     if (pixbuf != NULL || ret == 0) break;
  }

  if (pixbuf != NULL) { // success
//    printf("success\n");
    while (req->pageno > bgpdf.npages) {
      bgpg = g_new(struct BgPdfPage, 1);
      bgpg->pixbuf = NULL;
      bgpdf.pages = g_list_append(bgpdf.pages, bgpg);
      bgpdf.npages++;
    }
    bgpg = g_list_nth_data(bgpdf.pages, req->pageno-1);
    if (bgpg->pixbuf!=NULL) gdk_pixbuf_unref(bgpg->pixbuf);
    bgpg->pixbuf = pixbuf;
    bgpg->dpi = req->dpi;
    if (req->initial_request && bgpdf.create_pages) {
      bgpdf_create_page_with_bg(req->pageno, bgpg);
      // create page n, resize it, set its bg - all without any undo effect
    } else {
      if (!req->is_printing) bgpdf_update_bg(req->pageno, bgpg);
      // look for all pages with this bg, and update their bg pixmaps
    }
  }
  else {
//    printf("failed or aborted\n");
    bgpdf.create_pages = FALSE;
    req->initial_request = FALSE;
  }

  bgpdf.pid = 0;
  g_spawn_close_pid(pid);
  
  if (req->initial_request)
    req->pageno++; // try for next page
  else
    bgpdf.requests = g_list_delete_link(bgpdf.requests, bgpdf.requests);
  
  if (bgpdf.status == STATUS_SHUTDOWN) {
    end_bgpdf_shutdown();
    return;
  }
  
  bgpdf.status = STATUS_IDLE;
  if (bgpdf.requests != NULL) bgpdf_spawn_child();
}

/* spawn a child to process the head request */

void bgpdf_spawn_child(void)
{
  struct BgPdfRequest *req;
  GPid pid;
  gchar pageno_str[10], dpi_str[10];
  gchar *pdf_filename = bgpdf.tmpfile_copy;
  gchar *ppm_root = g_strdup_printf("%s/p", bgpdf.tmpdir);
  gchar *argv[]= PDFTOPPM_ARGV;
  GtkWidget *dialog;

  if (bgpdf.requests == NULL) return;
  req = (struct BgPdfRequest *)bgpdf.requests->data;
  if (req->pageno > bgpdf.npages+1 || 
      (!req->initial_request && req->pageno <= bgpdf.npages && 
       req->dpi == ((struct BgPdfPage *)g_list_nth_data(bgpdf.pages, req->pageno-1))->dpi))
  { // ignore this request - it's redundant, or in outer space
    bgpdf.pid = 0;
    bgpdf.status = STATUS_IDLE;
    bgpdf.requests = g_list_delete_link(bgpdf.requests, bgpdf.requests);
    g_free(ppm_root);
    if (bgpdf.requests != NULL) bgpdf_spawn_child();
    return;
  }
  g_snprintf(pageno_str, 10, "%d", req->pageno);
  g_snprintf(dpi_str, 10, "%d", req->dpi);
/*  printf("Processing request for page %d at %d dpi -- in %s\n", 
    req->pageno, req->dpi, ppm_root); */
  if (!g_spawn_async(NULL, argv, NULL,
                     G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH, 
                     NULL, NULL, &pid, NULL))
  {
    // couldn't spawn... abort this request, try next one maybe ?
//    printf("Couldn't spawn\n");
    bgpdf.pid = 0;
    bgpdf.status = STATUS_IDLE;
    bgpdf.requests = g_list_delete_link(bgpdf.requests, bgpdf.requests);
    g_free(ppm_root);
    if (!bgpdf.has_failed) {
      dialog = gtk_message_dialog_new(GTK_WINDOW(winMain), GTK_DIALOG_MODAL,
        GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "Unable to start PDF loader %s.", argv[0]);
      gtk_dialog_run(GTK_DIALOG(dialog));
      gtk_widget_destroy(dialog);
    }
    bgpdf.has_failed = TRUE;
    if (bgpdf.requests != NULL) bgpdf_spawn_child();
    return;
  }  

//  printf("Spawned process %d\n", pid);
  bgpdf.pid = pid;
  bgpdf.status = STATUS_RUNNING;
  g_child_watch_add(pid, bgpdf_child_handler, NULL);
  g_free(ppm_root);
}

/* make a request */

void add_bgpdf_request(int pageno, double zoom, gboolean printing)
{
  struct BgPdfRequest *req, *cmp_req;
  GList *list;
  
  if (bgpdf.status == STATUS_NOT_INIT || bgpdf.status == STATUS_SHUTDOWN)
    return; // don't accept requests in those modes...
  req = g_new(struct BgPdfRequest, 1);
  req->is_printing = printing;
  if (printing) req->dpi = PDFTOPPM_PRINTING_DPI;
  else req->dpi = (int)floor(72*zoom+0.5);
//  printf("Enqueuing request for page %d at %d dpi\n", pageno, req->dpi);
  if (pageno >= 1) {
    // cancel any request this may supersede
    for (list = bgpdf.requests; list != NULL; ) {
      cmp_req = (struct BgPdfRequest *)list->data;
      list = list->next;
      if (!cmp_req->initial_request && cmp_req->pageno == pageno &&
             cmp_req->is_printing == printing)
        cancel_bgpdf_request(cmp_req);
    }
    req->pageno = pageno;
    req->initial_request = FALSE;
  } else {
    req->pageno = 1;
    req->initial_request = TRUE;
  }
  bgpdf.requests = g_list_append(bgpdf.requests, req);
  if (!bgpdf.pid) bgpdf_spawn_child();
}

/* shutdown the PDF reader */

void shutdown_bgpdf(void)
{
  GList *list;
  struct BgPdfPage *pdfpg;
  struct BgPdfRequest *req;
  
  if (bgpdf.status == STATUS_NOT_INIT || bgpdf.status == STATUS_SHUTDOWN) return;
  refstring_unref(bgpdf.filename);
  for (list = bgpdf.pages; list != NULL; list = list->next) {
    pdfpg = (struct BgPdfPage *)list->data;
    if (pdfpg->pixbuf!=NULL) gdk_pixbuf_unref(pdfpg->pixbuf);
  }
  g_list_free(bgpdf.pages);
  bgpdf.status = STATUS_SHUTDOWN;
  for (list = g_list_last(bgpdf.requests); list != NULL; ) {
    req = (struct BgPdfRequest *)list->data;
    list = list->prev;
    cancel_bgpdf_request(req);
  }
  if (!bgpdf.pid) end_bgpdf_shutdown();
  /* The above will ultimately remove all requests and kill the child if needed.
     The child will set status to STATUS_NOT_INIT, clear the requests list,
     empty tmpdir, ... except if there's no child! */
  /* note: it could look like there's a race condition here - if a child
     terminates and a new request is enqueued while we are destroying the
     queue - but actually the child handler callback is NOT a signal
     callback, so execution of this function is atomic */
}

gboolean init_bgpdf(char *pdfname, gboolean create_pages, int file_domain)
{
  FILE *f;
  gchar *filebuf;
  gsize filelen;
  
  if (bgpdf.status != STATUS_NOT_INIT) return FALSE;
  bgpdf.tmpfile_copy = NULL;
  bgpdf.tmpdir = mkdtemp(g_strdup(TMPDIR_TEMPLATE));
  if (!bgpdf.tmpdir) return FALSE;
  // make a local copy and check if it's a PDF
  if (!g_file_get_contents(pdfname, &filebuf, &filelen, NULL))
    { end_bgpdf_shutdown(); return FALSE; }
  if (filelen < 4 || strncmp(filebuf, "%PDF", 4))
    { g_free(filebuf); end_bgpdf_shutdown(); return FALSE; }
  bgpdf.tmpfile_copy = g_strdup_printf("%s/bg.pdf", bgpdf.tmpdir);
  f = fopen(bgpdf.tmpfile_copy, "w");
  if (f == NULL || fwrite(filebuf, 1, filelen, f) != filelen) 
    { g_free(filebuf); end_bgpdf_shutdown(); return FALSE; }
  fclose(f);
  g_free(filebuf);
  bgpdf.status = STATUS_IDLE;
  bgpdf.pid = 0;
  bgpdf.filename = new_refstring((file_domain == DOMAIN_ATTACH) ? "bg.pdf" : pdfname);
  bgpdf.file_domain = file_domain;
  bgpdf.npages = 0;
  bgpdf.pages = NULL;
  bgpdf.requests = NULL;
  bgpdf.create_pages = create_pages;
  bgpdf.has_failed = FALSE;
  add_bgpdf_request(-1, ui.startup_zoom, FALSE); // request all pages
  return TRUE;
}

// create page n, resize it, set its bg
void bgpdf_create_page_with_bg(int pageno, struct BgPdfPage *bgpg)
{
  struct Page *pg;
  struct Background *bg;

  if (journal.npages < pageno) {
    bg = g_new(struct Background, 1);
    bg->canvas_item = NULL;
  } else {
    pg = (struct Page *)g_list_nth_data(journal.pages, pageno-1);
    bg = pg->bg;
    if (bg->type != BG_SOLID) return;
      // don't mess with a page the user has modified significantly...
  }
  
  bg->type = BG_PDF;
  bg->pixbuf = gdk_pixbuf_ref(bgpg->pixbuf);
  bg->filename = refstring_ref(bgpdf.filename);
  bg->file_domain = bgpdf.file_domain;
  bg->file_page_seq = pageno;
  bg->pixbuf_scale = ui.startup_zoom;
  bg->pixbuf_dpi = bgpg->dpi;

  if (journal.npages < pageno) {
    pg = new_page_with_bg(bg, 
            gdk_pixbuf_get_width(bg->pixbuf)*72.0/bg->pixbuf_dpi,
            gdk_pixbuf_get_height(bg->pixbuf)*72.0/bg->pixbuf_dpi);
    journal.pages = g_list_append(journal.pages, pg);
    journal.npages++;
  } else {
    pg->width = gdk_pixbuf_get_width(bgpg->pixbuf)*72.0/bg->pixbuf_dpi;
    pg->height = gdk_pixbuf_get_height(bgpg->pixbuf)*72.0/bg->pixbuf_dpi;
    make_page_clipbox(pg);
    update_canvas_bg(pg);
  }
  update_page_stuff();
}

// look for all journal pages with given pdf bg, and update their bg pixmaps
void bgpdf_update_bg(int pageno, struct BgPdfPage *bgpg)
{
  GList *list;
  struct Page *pg;
  
  for (list = journal.pages; list!= NULL; list = list->next) {
    pg = (struct Page *)list->data;
    if (pg->bg->type == BG_PDF && pg->bg->file_page_seq == pageno) {
      if (pg->bg->pixbuf!=NULL) gdk_pixbuf_unref(pg->bg->pixbuf);
      pg->bg->pixbuf = gdk_pixbuf_ref(bgpg->pixbuf);
      pg->bg->pixbuf_dpi = bgpg->dpi;
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
      tmp = g_strdup_printf("_%d %s", i+1, g_basename(ui.mru[i]));
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
  
  f = fopen(ui.mrufile, "w");
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
  ui.view_continuous = TRUE;
  ui.allow_xinput = TRUE;
  ui.discard_corepointer = FALSE;
  ui.left_handed = FALSE;
  ui.shorten_menus = FALSE;
  ui.shorten_menu_items = g_strdup(DEFAULT_SHORTEN_MENUS);
  ui.auto_save_prefs = FALSE;
  ui.bg_apply_all_pages = FALSE;
  ui.use_erasertip = FALSE;
  ui.window_default_width = 720;
  ui.window_default_height = 480;
  ui.maximize_at_start = FALSE;
  ui.fullscreen = FALSE;
  ui.scrollbar_step_increment = 30;
  ui.zoom_step_increment = 1;
  ui.zoom_step_factor = 1.5;
  ui.antialias_bg = TRUE;
  ui.progressive_bg = TRUE;
  ui.print_ruling = TRUE;
  ui.default_unit = UNIT_CM;
  ui.default_path = NULL;
  ui.default_font_name = g_strdup(DEFAULT_FONT);
  ui.default_font_size = DEFAULT_FONT_SIZE;
  
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
  ui.ruler[0] = ui.startupruler = FALSE;
  for (i=1; i<=NUM_BUTTONS; i++) {
    ui.toolno[i] = TOOL_ERASER;
    ui.ruler[i] = FALSE;
  }
  for (i=0; i<=NUM_BUTTONS; i++)
    ui.linked_brush[i] = BRUSH_LINKED;
  ui.brushes[0][TOOL_PEN].color_no = COLOR_BLACK;
  ui.brushes[0][TOOL_ERASER].color_no = COLOR_WHITE;
  ui.brushes[0][TOOL_HIGHLIGHTER].color_no = COLOR_YELLOW;
  for (i=0; i < NUM_STROKE_TOOLS; i++) {
    ui.brushes[0][i].thickness_no = THICKNESS_MEDIUM;
    ui.brushes[0][i].tool_options = 0;
  }
  for (i=0; i< NUM_STROKE_TOOLS; i++)
    for (j=1; j<=NUM_BUTTONS; j++)
      g_memmove(&(ui.brushes[j][i]), &(ui.brushes[0][i]), sizeof(struct Brush));

  // predef_thickness is already initialized as a global variable
  GS_BITMAP_DPI = 144;
  PDFTOPPM_PRINTING_DPI = 150;
  
  ui.hiliter_opacity = 0.5;
}

#if GLIB_CHECK_VERSION(2,6,0)

void update_keyval(const gchar *group_name, const gchar *key,
                const gchar *comment, gchar *value)
{
  gboolean has_it = g_key_file_has_key(ui.config_data, group_name, key, NULL);
  cleanup_numeric(value);
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

  update_keyval("general", "display_dpi",
    " the display resolution, in pixels per inch",
    g_strdup_printf("%.2f", DEFAULT_ZOOM*72));
  update_keyval("general", "initial_zoom",
    " the initial zoom level, in percent",
    g_strdup_printf("%.2f", 100*ui.zoom/DEFAULT_ZOOM));
  update_keyval("general", "window_maximize",
    " maximize the window at startup (true/false)",
    g_strdup(ui.maximize_at_start?"true":"false"));
  update_keyval("general", "window_fullscreen",
    " start in full screen mode (true/false)",
    g_strdup(ui.fullscreen?"true":"false"));
  update_keyval("general", "window_width",
    " the window width in pixels (when not maximized)",
    g_strdup_printf("%d", ui.window_default_width));
  update_keyval("general", "window_height",
    " the window height in pixels",
    g_strdup_printf("%d", ui.window_default_height));
  update_keyval("general", "scrollbar_speed",
    " scrollbar step increment (in pixels)",
    g_strdup_printf("%d", ui.scrollbar_step_increment));
  update_keyval("general", "zoom_dialog_increment",
    " the step increment in the zoom dialog box",
    g_strdup_printf("%d", ui.zoom_step_increment));
  update_keyval("general", "zoom_step_factor",
    " the multiplicative factor for zoom in/out",
    g_strdup_printf("%.3f", ui.zoom_step_factor));
  update_keyval("general", "view_continuous",
    " document view (true = continuous, false = single page)",
    g_strdup(ui.view_continuous?"true":"false"));
  update_keyval("general", "use_xinput",
    " use XInput extensions (true/false)",
    g_strdup(ui.allow_xinput?"true":"false"));
  update_keyval("general", "discard_corepointer",
    " discard Core Pointer events in XInput mode (true/false)",
    g_strdup(ui.discard_corepointer?"true":"false"));
  update_keyval("general", "use_erasertip",
    " always map eraser tip to eraser (true/false)",
    g_strdup(ui.use_erasertip?"true":"false"));
  update_keyval("general", "default_path",
    " default path for open/save (leave blank for current directory)",
    g_strdup((ui.default_path!=NULL)?ui.default_path:""));
  update_keyval("general", "interface_order",
    " interface components from top to bottom\n valid values: drawarea menu main_toolbar pen_toolbar statusbar",
    verbose_vertical_order(ui.vertical_order[0]));
  update_keyval("general", "interface_fullscreen",
    " interface components in fullscreen mode, from top to bottom",
    verbose_vertical_order(ui.vertical_order[1]));
  update_keyval("general", "interface_lefthanded",
    " interface has left-handed scrollbar (true/false)",
    g_strdup(ui.left_handed?"true":"false"));
  update_keyval("general", "shorten_menus",
    " hide some unwanted menu or toolbar items (true/false)",
    g_strdup(ui.shorten_menus?"true":"false"));
  update_keyval("general", "shorten_menu_items",
    " interface items to hide (customize at your own risk!)\n see source file xo-interface.c for a list of item names",
    g_strdup(ui.shorten_menu_items));
  update_keyval("general", "highlighter_opacity",
    " highlighter opacity (0 to 1, default 0.5)\n warning: opacity level is not saved in xoj files!",
    g_strdup_printf("%.2f", ui.hiliter_opacity));
  update_keyval("general", "autosave_prefs",
    " auto-save preferences on exit (true/false)",
    g_strdup(ui.auto_save_prefs?"true":"false"));

  update_keyval("paper", "width",
    " the default page width, in points (1/72 in)",
    g_strdup_printf("%.2f", ui.default_page.width));
  update_keyval("paper", "height",
    " the default page height, in points (1/72 in)",
    g_strdup_printf("%.2f", ui.default_page.height));
  update_keyval("paper", "color",
    " the default paper color",
    g_strdup(bgcolor_names[ui.default_page.bg->color_no]));
  update_keyval("paper", "style",
    " the default paper style (plain, lined, ruled, or graph)",
    g_strdup(bgstyle_names[ui.default_page.bg->ruling]));
  update_keyval("paper", "apply_all",
    " apply paper style changes to all pages (true/false)",
    g_strdup(ui.bg_apply_all_pages?"true":"false"));
  update_keyval("paper", "default_unit",
    " preferred unit (cm, in, px, pt)",
    g_strdup(unit_names[ui.default_unit]));
  update_keyval("paper", "print_ruling",
    " include paper ruling when printing or exporting to PDF (true/false)",
    g_strdup(ui.print_ruling?"true":"false"));
  update_keyval("paper", "antialias_bg",
    " antialiased bitmap backgrounds (true/false)",
    g_strdup(ui.antialias_bg?"true":"false"));
  update_keyval("paper", "progressive_bg",
    " progressive scaling of bitmap backgrounds (true/false)",
    g_strdup(ui.progressive_bg?"true":"false"));
  update_keyval("paper", "gs_bitmap_dpi",
    " bitmap resolution of PS/PDF backgrounds rendered using ghostscript (dpi)",
    g_strdup_printf("%d", GS_BITMAP_DPI));
  update_keyval("paper", "pdftoppm_printing_dpi",
    " bitmap resolution of PDF backgrounds when printing with libgnomeprint (dpi)",
    g_strdup_printf("%d", PDFTOPPM_PRINTING_DPI));

  update_keyval("tools", "startup_tool",
    " selected tool at startup (pen, eraser, highlighter, selectrect, vertspace, hand)",
    g_strdup(tool_names[ui.startuptool]));
  update_keyval("tools", "startup_ruler",
    " ruler mode at startup (true/false) (for pen or highlighter only)",
    g_strdup(ui.startupruler?"true":"false"));
  update_keyval("tools", "pen_color",
    " default pen color",
    g_strdup(color_names[ui.default_brushes[TOOL_PEN].color_no]));
  update_keyval("tools", "pen_thickness",
    " default pen thickness (fine = 1, medium = 2, thick = 3)",
    g_strdup_printf("%d", ui.default_brushes[TOOL_PEN].thickness_no));
  update_keyval("tools", "eraser_thickness",
    " default eraser thickness (fine = 1, medium = 2, thick = 3)",
    g_strdup_printf("%d", ui.default_brushes[TOOL_ERASER].thickness_no));
  update_keyval("tools", "eraser_mode",
    " default eraser mode (standard = 0, whiteout = 1, strokes = 2)",
    g_strdup_printf("%d", ui.default_brushes[TOOL_ERASER].tool_options));
  update_keyval("tools", "highlighter_color",
    " default highlighter color",
    g_strdup(color_names[ui.default_brushes[TOOL_HIGHLIGHTER].color_no]));
  update_keyval("tools", "highlighter_thickness",
    " default highlighter thickness (fine = 1, medium = 2, thick = 3)",
    g_strdup_printf("%d", ui.default_brushes[TOOL_HIGHLIGHTER].thickness_no));
  update_keyval("tools", "btn2_tool",
    " button 2 tool (pen, eraser, highlighter, text, selectrect, vertspace, hand)",
    g_strdup(tool_names[ui.toolno[1]]));
  update_keyval("tools", "btn2_linked",
    " button 2 brush linked to primary brush (true/false) (overrides all other settings)",
    g_strdup((ui.linked_brush[1]==BRUSH_LINKED)?"true":"false"));
  update_keyval("tools", "btn2_ruler",
    " button 2 ruler mode (true/false) (for pen or highlighter only)",
    g_strdup(ui.ruler[1]?"true":"false"));
  update_keyval("tools", "btn2_color",
    " button 2 brush color (for pen or highlighter only)",
    g_strdup((ui.toolno[1]<NUM_STROKE_TOOLS)?
               color_names[ui.brushes[1][ui.toolno[1]].color_no]:"white"));
  update_keyval("tools", "btn2_thickness",
    " button 2 brush thickness (pen, eraser, or highlighter only)",
    g_strdup_printf("%d", (ui.toolno[1]<NUM_STROKE_TOOLS)?
                            ui.brushes[1][ui.toolno[1]].thickness_no:0));
  update_keyval("tools", "btn2_erasermode",
    " button 2 eraser mode (eraser only)",
    g_strdup_printf("%d", ui.brushes[1][TOOL_ERASER].tool_options));
  update_keyval("tools", "btn3_tool",
    " button 3 tool (pen, eraser, highlighter, text, selectrect, vertspace, hand)",
    g_strdup(tool_names[ui.toolno[2]]));
  update_keyval("tools", "btn3_linked",
    " button 3 brush linked to primary brush (true/false) (overrides all other settings)",
    g_strdup((ui.linked_brush[2]==BRUSH_LINKED)?"true":"false"));
  update_keyval("tools", "btn3_ruler",
    " button 3 ruler mode (true/false) (for pen or highlighter only)",
    g_strdup(ui.ruler[2]?"true":"false"));
  update_keyval("tools", "btn3_color",
    " button 3 brush color (for pen or highlighter only)",
    g_strdup((ui.toolno[2]<NUM_STROKE_TOOLS)?
               color_names[ui.brushes[2][ui.toolno[2]].color_no]:"white"));
  update_keyval("tools", "btn3_thickness",
    " button 3 brush thickness (pen, eraser, or highlighter only)",
    g_strdup_printf("%d", (ui.toolno[2]<NUM_STROKE_TOOLS)?
                            ui.brushes[2][ui.toolno[2]].thickness_no:0));
  update_keyval("tools", "btn3_erasermode",
    " button 3 eraser mode (eraser only)",
    g_strdup_printf("%d", ui.brushes[2][TOOL_ERASER].tool_options));

  update_keyval("tools", "pen_thicknesses",
    " thickness of the various pens (in points, 1 pt = 1/72 in)",
    g_strdup_printf("%.2f;%.2f;%.2f;%.2f;%.2f", 
      predef_thickness[TOOL_PEN][0], predef_thickness[TOOL_PEN][1],
      predef_thickness[TOOL_PEN][2], predef_thickness[TOOL_PEN][3],
      predef_thickness[TOOL_PEN][4]));
  update_keyval("tools", "eraser_thicknesses",
    " thickness of the various erasers (in points, 1 pt = 1/72 in)",
    g_strdup_printf("%.2f;%.2f;%.2f", 
      predef_thickness[TOOL_ERASER][1], predef_thickness[TOOL_ERASER][2],
      predef_thickness[TOOL_ERASER][3]));
  update_keyval("tools", "highlighter_thicknesses",
    " thickness of the various highlighters (in points, 1 pt = 1/72 in)",
    g_strdup_printf("%.2f;%.2f;%.2f", 
      predef_thickness[TOOL_HIGHLIGHTER][1], predef_thickness[TOOL_HIGHLIGHTER][2],
      predef_thickness[TOOL_HIGHLIGHTER][3]));
  update_keyval("tools", "default_font",
    " name of the default font",
    g_strdup(ui.default_font_name));
  update_keyval("tools", "default_font_size",
    " default font size",
    g_strdup_printf("%.1f", ui.default_font_size));

  buf = g_key_file_to_data(ui.config_data, NULL, NULL);
  if (buf == NULL) return;
  f = fopen(ui.configfile, "w");
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
  return FALSE;
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
  int i, n, found, l;

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
           " Xournal configuration file.\n"
           " This file is generated automatically upon saving preferences.\n"
           " Use caution when editing this file manually.\n", NULL);
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
  parse_keyval_boolean("general", "view_continuous", &ui.view_continuous);
  parse_keyval_boolean("general", "use_xinput", &ui.allow_xinput);
  parse_keyval_boolean("general", "discard_corepointer", &ui.discard_corepointer);
  parse_keyval_boolean("general", "use_erasertip", &ui.use_erasertip);
  parse_keyval_string("general", "default_path", &ui.default_path);
  parse_keyval_vorderlist("general", "interface_order", ui.vertical_order[0]);
  parse_keyval_vorderlist("general", "interface_fullscreen", ui.vertical_order[1]);
  parse_keyval_boolean("general", "interface_lefthanded", &ui.left_handed);
  parse_keyval_boolean("general", "shorten_menus", &ui.shorten_menus);
  if (parse_keyval_string("general", "shorten_menu_items", &str))
    if (str!=NULL) { g_free(ui.shorten_menu_items); ui.shorten_menu_items = str; }
  parse_keyval_float("general", "highlighter_opacity", &ui.hiliter_opacity, 0., 1.);
  parse_keyval_boolean("general", "autosave_prefs", &ui.auto_save_prefs);
  
  parse_keyval_float("paper", "width", &ui.default_page.width, 1., 5000.);
  parse_keyval_float("paper", "height", &ui.default_page.height, 1., 5000.);
  parse_keyval_enum("paper", "color", &(ui.default_page.bg->color_no), bgcolor_names, COLOR_MAX);
  ui.default_page.bg->color_rgba = predef_bgcolors_rgba[ui.default_page.bg->color_no];
  parse_keyval_enum("paper", "style", &(ui.default_page.bg->ruling), bgstyle_names, 4);
  parse_keyval_boolean("paper", "apply_all", &ui.bg_apply_all_pages);
  parse_keyval_enum("paper", "default_unit", &ui.default_unit, unit_names, 4);
  parse_keyval_boolean("paper", "antialias_bg", &ui.antialias_bg);
  parse_keyval_boolean("paper", "progressive_bg", &ui.progressive_bg);
  parse_keyval_boolean("paper", "print_ruling", &ui.print_ruling);
  parse_keyval_int("paper", "gs_bitmap_dpi", &GS_BITMAP_DPI, 1, 1200);
  parse_keyval_int("paper", "pdftoppm_printing_dpi", &PDFTOPPM_PRINTING_DPI, 1, 1200);

  parse_keyval_enum("tools", "startup_tool", &ui.startuptool, tool_names, NUM_TOOLS);
  ui.toolno[0] = ui.startuptool;
  if (ui.startuptool == TOOL_PEN || ui.startuptool == TOOL_HIGHLIGHTER) {
    parse_keyval_boolean("tools", "startup_ruler", &ui.startupruler);
    ui.ruler[0] = ui.startupruler;
  }
  parse_keyval_enum("tools", "pen_color", &(ui.brushes[0][TOOL_PEN].color_no), color_names, COLOR_MAX);
  parse_keyval_int("tools", "pen_thickness", &(ui.brushes[0][TOOL_PEN].thickness_no), 0, 4);
  parse_keyval_int("tools", "eraser_thickness", &(ui.brushes[0][TOOL_ERASER].thickness_no), 1, 3);
  parse_keyval_int("tools", "eraser_mode", &(ui.brushes[0][TOOL_ERASER].tool_options), 0, 2);
  parse_keyval_enum("tools", "highlighter_color", &(ui.brushes[0][TOOL_HIGHLIGHTER].color_no), color_names, COLOR_MAX);
  parse_keyval_int("tools", "highlighter_thickness", &(ui.brushes[0][TOOL_HIGHLIGHTER].thickness_no), 0, 4);
  for (i=0; i< NUM_STROKE_TOOLS; i++)
    for (j=1; j<=NUM_BUTTONS; j++)
      g_memmove(&(ui.brushes[j][i]), &(ui.brushes[0][i]), sizeof(struct Brush));

  parse_keyval_enum("tools", "btn2_tool", &(ui.toolno[1]), tool_names, NUM_TOOLS);
  if (parse_keyval_boolean("tools", "btn2_linked", &b))
    ui.linked_brush[1] = b?BRUSH_LINKED:BRUSH_STATIC;
  parse_keyval_enum("tools", "btn3_tool", &(ui.toolno[2]), tool_names, NUM_TOOLS);
  if (parse_keyval_boolean("tools", "btn3_linked", &b))
    ui.linked_brush[2] = b?BRUSH_LINKED:BRUSH_STATIC;
  for (i=1; i<=NUM_BUTTONS; i++)
    if (ui.toolno[i]==TOOL_PEN || ui.toolno[i]==TOOL_HIGHLIGHTER)
      ui.ruler[i] = ui.ruler[0];
  if (ui.linked_brush[1]!=BRUSH_LINKED) {
    if (ui.toolno[1]==TOOL_PEN || ui.toolno[1]==TOOL_HIGHLIGHTER) {
      parse_keyval_boolean("tools", "btn2_ruler", &(ui.ruler[1]));
      parse_keyval_enum("tools", "btn2_color", &(ui.brushes[1][ui.toolno[1]].color_no), color_names, COLOR_MAX);
    }
    if (ui.toolno[1]<NUM_STROKE_TOOLS)
      parse_keyval_int("tools", "btn2_thickness", &(ui.brushes[1][ui.toolno[1]].thickness_no), 0, 4);
    if (ui.toolno[1]==TOOL_ERASER)
      parse_keyval_int("tools", "btn2_erasermode", &(ui.brushes[1][TOOL_ERASER].tool_options), 0, 2);
  }
  if (ui.linked_brush[2]!=BRUSH_LINKED) {
    if (ui.toolno[2]==TOOL_PEN || ui.toolno[2]==TOOL_HIGHLIGHTER) {
      parse_keyval_boolean("tools", "btn3_ruler", &(ui.ruler[2]));
      parse_keyval_enum("tools", "btn3_color", &(ui.brushes[2][ui.toolno[2]].color_no), color_names, COLOR_MAX);
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
