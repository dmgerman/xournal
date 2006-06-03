#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gtk/gtk.h>
#include <libgnomecanvas/libgnomecanvas.h>
#include <libgnomeprint/gnome-print-job.h>

#include "xournal.h"
#include "xo-misc.h"
#include "xo-paint.h"
#include "xo-print.h"
#include "xo-file.h"

#define RGBA_RED(rgba) (((rgba>>24)&0xff)/255.0)
#define RGBA_GREEN(rgba) (((rgba>>16)&0xff)/255.0)
#define RGBA_BLUE(rgba) (((rgba>>8)&0xff)/255.0)
#define RGBA_ALPHA(rgba) (((rgba>>0)&0xff)/255.0)
#define RGBA_RGB(rgba) RGBA_RED(rgba), RGBA_GREEN(rgba), RGBA_BLUE(rgba)

// does the same job as update_canvas_bg(), but to a print context

void print_background(GnomePrintContext *gpc, struct Page *pg, gboolean *abort)
{
  double x, y;
  GdkPixbuf *pix;
  BgPdfPage *pgpdf;

  if (pg->bg->type == BG_SOLID) {
    gnome_print_setopacity(gpc, 1.0);
    gnome_print_setrgbcolor(gpc, RGBA_RGB(pg->bg->color_rgba));
    gnome_print_rect_filled(gpc, 0, 0, pg->width, pg->height);

    if (pg->bg->ruling == RULING_NONE) return;
    gnome_print_setrgbcolor(gpc, RGBA_RGB(RULING_COLOR));
    gnome_print_setlinewidth(gpc, RULING_THICKNESS);
    
    if (pg->bg->ruling == RULING_GRAPH) {
      for (x=RULING_GRAPHSPACING; x<pg->width-1; x+=RULING_GRAPHSPACING)
        gnome_print_line_stroked(gpc, x, 0, x, pg->height);
      for (y=RULING_GRAPHSPACING; y<pg->height-1; y+=RULING_GRAPHSPACING)
        gnome_print_line_stroked(gpc, 0, y, pg->width, y);
      return;
    }
    
    for (y=RULING_TOPMARGIN; y<pg->height-1; y+=RULING_SPACING)
      gnome_print_line_stroked(gpc, 0, y, pg->width, y);
    if (pg->bg->ruling == RULING_LINED) {
      gnome_print_setrgbcolor(gpc, RGBA_RGB(RULING_MARGIN_COLOR));
      gnome_print_line_stroked(gpc, RULING_LEFTMARGIN, 0, RULING_LEFTMARGIN, pg->height);
    }
    return;
  }
  else if (pg->bg->type == BG_PIXMAP || pg->bg->type == BG_PDF) {
    if (pg->bg->type == BG_PDF) {
      pgpdf = (struct BgPdfPage *)g_list_nth_data(bgpdf.pages, pg->bg->file_page_seq-1);
      if (pgpdf == NULL) return;
      if (pgpdf->dpi != PDFTOPPM_PRINTING_DPI) {
        add_bgpdf_request(pg->bg->file_page_seq, 0, TRUE);
        while (pgpdf->dpi != PDFTOPPM_PRINTING_DPI && bgpdf.status == STATUS_RUNNING) {
          gtk_main_iteration();
          if (*abort) return;
        }
      }
      pix = pgpdf->pixbuf;
    }
    else pix = pg->bg->pixbuf;
    if (gdk_pixbuf_get_bits_per_sample(pix) != 8) return;
    if (gdk_pixbuf_get_colorspace(pix) != GDK_COLORSPACE_RGB) return;
    gnome_print_gsave(gpc);
    gnome_print_scale(gpc, pg->width, -pg->height);
    gnome_print_translate(gpc, 0., -1.);
    if (gdk_pixbuf_get_n_channels(pix) == 3)
       gnome_print_rgbimage(gpc, gdk_pixbuf_get_pixels(pix),
         gdk_pixbuf_get_width(pix), gdk_pixbuf_get_height(pix), gdk_pixbuf_get_rowstride(pix));
    else if (gdk_pixbuf_get_n_channels(pix) == 4)
       gnome_print_rgbaimage(gpc, gdk_pixbuf_get_pixels(pix),
         gdk_pixbuf_get_width(pix), gdk_pixbuf_get_height(pix), gdk_pixbuf_get_rowstride(pix));
    gnome_print_grestore(gpc);
    return;
  }
}

void print_page(GnomePrintContext *gpc, struct Page *pg, int pageno,
                double pgwidth, double pgheight, gboolean *abort)
{
  char tmp[10];
  gdouble scale;
  guint old_rgba;
  double old_thickness;
  GList *layerlist, *itemlist;
  struct Layer *l;
  struct Item *item;
  int i;
  double *pt;
  
  if (pg==NULL) return;
  
  g_snprintf(tmp, 10, "Page %d", pageno);
  gnome_print_beginpage(gpc, (guchar *)tmp);
  gnome_print_gsave(gpc);
  
  scale = MIN(pgwidth/pg->width, pgheight/pg->height)*0.95;
  gnome_print_translate(gpc,
     (pgwidth - scale*pg->width)/2, (pgheight + scale*pg->height)/2);
  gnome_print_scale(gpc, scale, -scale);
  gnome_print_setlinejoin(gpc, 1); // round
  gnome_print_setlinecap(gpc, 1); // round

  print_background(gpc, pg, abort);

  old_rgba = 0x12345678;    // not any values we use, so we'll reset them
  old_thickness = 0.0;

  for (layerlist = pg->layers; layerlist!=NULL; layerlist = layerlist->next) {
    if (*abort) break;
    l = (struct Layer *)layerlist->data;
    for (itemlist = l->items; itemlist!=NULL; itemlist = itemlist->next) {
      if (*abort) break;
      item = (struct Item *)itemlist->data;
      if (item->type == ITEM_STROKE) {
        if ((item->brush.color_rgba & ~0xff) != (old_rgba & ~0xff))
          gnome_print_setrgbcolor(gpc, RGBA_RGB(item->brush.color_rgba));
        if ((item->brush.color_rgba & 0xff) != (old_rgba & 0xff))
          gnome_print_setopacity(gpc, RGBA_ALPHA(item->brush.color_rgba));
        if (item->brush.thickness != old_thickness)
          gnome_print_setlinewidth(gpc, item->brush.thickness);
        old_rgba = item->brush.color_rgba;
        old_thickness = item->brush.thickness;
        gnome_print_newpath(gpc);
        pt = item->path->coords;
        gnome_print_moveto(gpc, pt[0], pt[1]);
        for (i=1, pt+=2; i<item->path->num_points; i++, pt+=2)
          gnome_print_lineto(gpc, pt[0], pt[1]);
        gnome_print_stroke(gpc);
      }
    }
  }
  
  gnome_print_grestore(gpc);
  gnome_print_showpage(gpc);
}

void cb_print_abort(GtkDialog *dialog, gint response, gboolean *abort)
{
  *abort = TRUE;
}

void print_job_render(GnomePrintJob *gpj, int fromPage, int toPage)
{
  GnomePrintConfig *config;
  GnomePrintContext *gpc;
  GtkWidget *wait_dialog;
  double pgwidth, pgheight;
  int i;
  gboolean abort;
  
  config = gnome_print_job_get_config(gpj);
  gnome_print_config_get_page_size(config, &pgwidth, &pgheight);
  g_object_unref(G_OBJECT(config));

  gpc = gnome_print_job_get_context(gpj);

  abort = FALSE;
  wait_dialog = gtk_message_dialog_new(GTK_WINDOW(winMain), GTK_DIALOG_MODAL,
     GTK_MESSAGE_INFO, GTK_BUTTONS_CANCEL, "Preparing print job");
  gtk_widget_show(wait_dialog);
  g_signal_connect(wait_dialog, "response", G_CALLBACK (cb_print_abort), &abort);
  
  for (i = fromPage; i <= toPage; i++) {
#if GTK_CHECK_VERSION(2,6,0)
    if (!gtk_check_version(2, 6, 0))
      gtk_message_dialog_format_secondary_text(
             GTK_MESSAGE_DIALOG(wait_dialog), "Page %d", i+1); 
#endif
    while (gtk_events_pending()) gtk_main_iteration();
    print_page(gpc, (struct Page *)g_list_nth_data(journal.pages, i), i+1,
                                             pgwidth, pgheight, &abort);
    if (abort) break;
  }
#if GTK_CHECK_VERSION(2,6,0)
  if (!gtk_check_version(2, 6, 0))
    gtk_message_dialog_format_secondary_text(
              GTK_MESSAGE_DIALOG(wait_dialog), "Finalizing...");
#endif
  while (gtk_events_pending()) gtk_main_iteration();

  gnome_print_context_close(gpc);  
  g_object_unref(G_OBJECT(gpc));  

  gnome_print_job_close(gpj);
  if (!abort) gnome_print_job_print(gpj);
  g_object_unref(G_OBJECT(gpj));

  gtk_widget_destroy(wait_dialog);
}
