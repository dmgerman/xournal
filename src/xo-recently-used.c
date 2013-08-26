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
#include <goocanvas.h>

#include "xo-gtk.h"
#include "xo-recently-used.h"

#define XO_RU_SIZE 8 


char      *recentlyUsedFilenames[XO_RU_SIZE]; 
GtkWidget *recentlyUsedMenuItems[XO_RU_SIZE];
char *recentlyUsedFilename;
GtkWidget *recentlyUsedMainWindow;

// initialize the recent files list
void xo_recently_used_init(gchar *filename, GtkWidget *mainWindow)
{
  int i;
  gsize lfptr;
  char s[5];
  GIOChannel *f;
  gchar *str;
  GIOStatus status;
  
  recentlyUsedFilename = filename;
  recentlyUsedMainWindow = mainWindow;
  g_strlcpy(s, "mru0", 5);
  // the name of the widgets are mru0, mru1, mru2...
  for (s[3]='0', i=0; i<XO_RU_SIZE; s[3]++, i++) {
    recentlyUsedMenuItems[i] = XO_GET_UI_COMPONENT(recentlyUsedMainWindow,s);
    recentlyUsedFilenames[i] = NULL;
  }
  // read them
  f = g_io_channel_new_file(filename, "r", NULL);
  if (f) status = G_IO_STATUS_NORMAL;
  else status = G_IO_STATUS_ERROR;
  i = 0;
  while (status == G_IO_STATUS_NORMAL && i<XO_RU_SIZE) {
    lfptr = 0;
    status = g_io_channel_read_line(f, &str, NULL, &lfptr, NULL);
    if (status == G_IO_STATUS_NORMAL && lfptr>0) {
      str[lfptr] = 0;
      recentlyUsedFilenames[i] = str;
      i++;
    }
  }
  if (f) {
    g_io_channel_shutdown(f, FALSE, NULL);
    g_io_channel_unref(f);
  }
  xo_recently_used_menu_update();
}


void xo_recently_used_menu_update(void)
{
  int i;
  gboolean anyone = FALSE;
  gchar *tmp;
  
  for (i=0; i<XO_RU_SIZE; i++) {
    if (recentlyUsedFilenames[i]!=NULL) {
      tmp = g_strdup_printf("_%d %s", i+1,
               g_strjoinv("__", g_strsplit_set(g_path_get_basename(recentlyUsedFilenames[i]),"_",-1)));
      gtk_label_set_text_with_mnemonic(GTK_LABEL(gtk_bin_get_child(GTK_BIN(recentlyUsedMenuItems[i]))), tmp);
      g_free(tmp);
      gtk_widget_show(recentlyUsedMenuItems[i]);
      anyone = TRUE;
    }  else {
      gtk_widget_hide(recentlyUsedMenuItems[i]);
    }
  }
  gtk_widget_set_sensitive(XO_GET_UI_COMPONENT(recentlyUsedMainWindow,"fileRecentFiles"), anyone);
}


void xo_recently_used_new(gchar *filename)
{
  assert(0);
}


void xo_recently_used_save(void)
{
  FILE *f;
  int i;

  f = fopen(recentlyUsedFilename, "w");
  if (f==NULL) return;
  for (i=0; i<XO_RU_SIZE; i++)
    if (recentlyUsedFilenames[i]!=NULL) 
      fprintf(f, "%s\n", recentlyUsedFilenames[i]);
  fclose(f);
}

