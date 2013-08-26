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

#ifndef __XO_RECENTLY_USED_H

#define __XO_RECENTLY_USED_H

void xo_recently_used_init(gchar *filename, GtkWidget *mainWindow);
void xo_recently_used_menu_update(void);
void xo_recently_used_new(gchar *filename);
void xo_recently_used_save(void);


#endif
