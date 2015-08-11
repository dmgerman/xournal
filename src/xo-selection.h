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


void start_selectrect(GdkEvent *event);
void finalize_selectrect(void);
void start_selectregion(GdkEvent *event);
void finalize_selectregion(void);
void continue_selectregion(GdkEvent *event);

void make_dashed(GnomeCanvasItem *item);

gboolean start_movesel(GdkEvent *event);
void start_vertspace(GdkEvent *event);
void continue_movesel(GdkEvent *event);
void finalize_movesel(void);

gboolean start_resizesel(GdkEvent *event);
void continue_resizesel(GdkEvent *event);
void finalize_resizesel(void);

void selection_delete(void);

void recolor_selection(int color_no, guint color_rgba);
void rethicken_selection(int val);
