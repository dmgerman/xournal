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

#ifndef __XO_MRU_H
#define __XO_MRU_H

#define MRU_FILE "recent-files"
#define MRU_SIZE 20
#define MRU_ITEM_SEPARATOR ';'

typedef struct mru_item {
  char *filename;
  int currentPage;
} mru_item;

void mru_init(void);
void mru_update_menu(void);
void mru_new_entry(char *name, int page);
void mru_delete_entry(int which);
void mru_save_list(void);
int mru_item_is_empty(int i);
char *mru_filename(int i);
int mru_pagenumber(int i);
int mru_set_pagenumber(int i, int page);

#endif
