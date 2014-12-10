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
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <assert.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <ctype.h>

#include "xournal.h"
#include "xo-mru.h"
#include "xo-misc.h"

void mru_parse_entry(int index, char *entry)
{
  // parse entry of the form
  // filename;page with regular expression for page [0-9]+
  // it is backwards compatible: if page number does not exist then return 1 for it
  mru_item *mi;
  assert(index>=0 && index < MRU_SIZE);
  mi = ui.mru + index;

  int i;
  int len;
  assert(entry != NULL);

  len = strlen(entry);

  // parse backwards looking for separatror,
  // while the characters are numbers

  i = len -1 ;
  while (i > 0 && entry[i] != MRU_ITEM_SEPARATOR && isdigit(entry[i])) {
    i--;
  }
  mi->filename = g_strdup(entry);

  if (i > 0 && i < len - 1) {
    // there is at least one character in page number
    // i points to separator
    // just replace the null, the free would release the entire string anyways
    mi->filename[i] = 0;
    mi->currentPage = atoi(entry+i+1);
    if (mi->currentPage <= 0)
      mi->currentPage =1;
  } else {
    mi->currentPage =1;
  }
}


// initialize the recent files list
void mru_init(void)
{
  int i;
  gsize lfptr;
  char s[10];
  GIOChannel *f;
  gchar *str;
  GIOStatus status;

  for (i=0; i<MRU_SIZE; i++) {
    sprintf(s, "mru%d", i);
    ui.mrumenu[i] = GET_COMPONENT(s);
    bzero(ui.mru + i, sizeof(ui.mru[i]));
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
      mru_parse_entry(i, str);
      g_free(str);
      i++;
    }
  }
  if (f) {
    g_io_channel_shutdown(f, FALSE, NULL);
    g_io_channel_unref(f);
  }
  mru_update_menu();
}

void mru_update_menu(void)
{
  int i;
  gboolean anyone = FALSE;
  gchar *tmp;

  for (i=0; i<MRU_SIZE; i++) {
    if (!mru_item_is_empty(i)) {
      gchar *newName;

      // replace _ with __
      newName = g_strjoinv("__", g_strsplit_set(xo_basename(mru_filename(i), FALSE),"_",-1));

      tmp = g_strdup_printf("_%d %s", i+1, newName);

      gtk_label_set_text_with_mnemonic(GTK_LABEL(gtk_bin_get_child(GTK_BIN(ui.mrumenu[i]))), tmp);
      g_free(tmp);
      g_free(newName);

      gtk_widget_show(ui.mrumenu[i]);
      anyone = TRUE;
    }
    else {
        gtk_widget_hide(ui.mrumenu[i]);
    }
  }
  gtk_widget_set_sensitive(GET_COMPONENT("fileRecentFiles"), anyone);
}

void mru_item_move(int iDest, int iSource)
{
  // moves one item from one slot to another
  // clears source slot

  mru_item *source;
  mru_item *dest;
  assert(iDest>=0   && iDest < MRU_SIZE);
  assert(iSource>=0 && iSource < MRU_SIZE);

  source = ui.mru + iSource;
  dest = ui.mru + iDest;

  dest->filename = source ->filename;
  dest->currentPage = source ->currentPage;
  source->filename = NULL;
  source->currentPage = 0;
}

void mru_item_free(int i)
{
  mru_item *mi;
  assert(i>=0 && i < MRU_SIZE);
  mi = ui.mru + i;

  g_free(mi->filename);

  mi->filename = NULL;
  mi->currentPage = 0;
}

void mru_item_set(int i, char *filename, int page)
{
  // filename can be NULL

  mru_item *mi;
  assert(i>=0 && i < MRU_SIZE);
  assert(page>= 0);
  mi = ui.mru + i;

  assert(mi!= NULL);
  mi->filename = g_strdup(filename);
  mi->currentPage = page;
}

int mru_item_is_empty(int i)
{
  mru_item *mi;
  assert(i>=0 && i < MRU_SIZE);
  mi = ui.mru + i;
  return mi->filename == NULL;
}


int mru_item_equal_filename(int i, char *filename)
{
  // return true if filename is the same;
  mru_item *mi;
  assert(i>=0 && i < MRU_SIZE);
  mi = ui.mru + i;

  assert(mi != NULL);
  assert(filename != NULL);

  return g_strcmp0(mi->filename, filename) == 0;
}

void mru_shift(int from, int direction)
{
  // moves the MRU from given position (from)
  // if direction is 1
  // moves it pushes up
  // if direction is -1
  // moves it pushes down
  int i;
  if (direction == 1)
    for (i=from+1;i<MRU_SIZE;i++)
      mru_item_move( i -1, i);
  else if (direction == -1)
    for (i=MRU_SIZE-1; i>from; i--)
      mru_item_move( i, i -1 );
  else
    assert(1); //execution should never be here
}

void mru_new_entry(char *name, int page)
{
  int i, j;

  // if it exists remove it, and shift
  for (i=0;i<MRU_SIZE;i++)
    if (mru_item_equal_filename(i, name)) {
      mru_item_free(i);
      mru_shift(i, 1);
    }

  // get rid of the last one, if needed
  // it takes care of freeeing its memory
  mru_item_free(MRU_SIZE-1);

  // make space for new one
  mru_shift(0, -1);

  // set the first
  mru_item_set(0, name, page);
  mru_update_menu();
}

void mru_delete_entry(int which)
{

  mru_item_free(which);

  mru_shift(which, 1);

  mru_update_menu();
}

void mru_save_list(void)
{
  FILE *f;
  int i;

  f = g_fopen(ui.mrufile, "w");
  if (f==NULL) return;
  for (i=0; i<MRU_SIZE; i++)
    if (!mru_item_is_empty(i))
      fprintf(f, "%s;%d\n", mru_filename(i), mru_pagenumber(i));
  fclose(f);
}


char *mru_filename(int i)
{
  mru_item *mi;
  assert(i>=0 && i < MRU_SIZE);
  mi = ui.mru + i;
  return mi->filename;
}
int mru_pagenumber(int i)
{
  mru_item *mi;
  assert(i>=0 && i < MRU_SIZE);
  mi = ui.mru + i;
  assert(mi->currentPage > 0);
  return mi->currentPage;
}

int mru_set_pagenumber(int i, int page)
{
  mru_item *mi;
  assert(i>=0 && i < MRU_SIZE);
  mi = ui.mru + i;
  mi->currentPage = page;
}
