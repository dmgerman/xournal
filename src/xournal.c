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
#include <clutter/clutter.h>
#include <clutter-gtk/clutter-gtk.h>


gboolean xournal_ok_to_close(void)
{
  return TRUE;
}


void
on_fileQuit_activate                   (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
#ifdef dmg
  end_text();
#endif
  if (xournal_ok_to_close()) 
    gtk_main_quit ();
}


void xournal_init_stuff(int argc, char *argv[])
{
  GList *dev_list;
  GdkDevice *device;

  dev_list =  gdk_device_manager_list_devices (gdk_display_get_device_manager ( gdk_display_get_default()),
					       GDK_DEVICE_TYPE_MASTER); /// gdk_devices_list();


  while (dev_list != NULL) {
    device = (GdkDevice *)dev_list->data;
    printf("Device %s\n", gdk_device_get_name (device));
    dev_list = dev_list->next;
  }
  
}

void xournal_init_interface(void) 
{
  GError *err = NULL;
  GtkBuilder *builder;
  GtkWidget *winMain;
  GtkGrid   *mainGrid;

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

  // we need to add the main clutter widget. glade doesn't let us do it
  // it is attached to mainGrid

  // 1. create embed and attach
  mainGrid = (GtkGrid *)gtk_builder_get_object (builder, "mainGrid");
  assert(mainGrid != NULL);
  GtkWidget *embed = gtk_clutter_embed_new ();

  gtk_grid_attach (mainGrid, embed, 1, 0, 1, 1);
  gtk_widget_show (embed);

  // 2. init the stage
  ClutterActor *stage = gtk_clutter_embed_get_stage (GTK_CLUTTER_EMBED (embed));
  //  clutter_stage_set_color (CLUTTER_STAGE (stage), &stage_color);
  //  clutter_actor_set_size (stage, 640, 480);

  // 3. Create a viewport actor to be able to scroll actor. This is the main "canvas"

  ClutterActor *canvas =  gtk_clutter_actor_new ();
  clutter_container_add_actor (CLUTTER_CONTAINER (stage), canvas);

  /// * Load image from first command line argument and add it to viewport: */
  ClutterActor *texture = clutter_texture_new_from_file ("rip.jpg", NULL);
  clutter_container_add_actor (CLUTTER_CONTAINER (canvas), texture);
  clutter_actor_set_position (texture, 0, 0);
  clutter_actor_set_position (canvas, 0, 0);

  // 4. Now we need to connect the scrollbars

  g_object_unref (G_OBJECT (builder));


  gtk_widget_show (winMain);

}

void shutdown(void)
{
#ifdef dmg
  if (bgpdf.status != STATUS_NOT_INIT) shutdown_bgpdf();

  save_mru_list();
  if (ui.auto_save_prefs) save_config_to_file();
#endif
}
	     

int main (int argc, char *argv[])
{

#ifdef ENABLE_NLS
  bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);
#endif
  
  gtk_init (&argc, &argv);
  clutter_init(&argc, &argv);

  xournal_init_interface();

  xournal_init_stuff (argc, argv);
  
  gtk_main ();
  
  return 0;
}
