void set_cursor_busy(gboolean busy);
void update_cursor(void);

void create_new_stroke(GdkEvent *event);
void continue_stroke(GdkEvent *event);
void finalize_stroke(void);

void do_eraser(GdkEvent *event, double radius, gboolean whole_strokes);

void start_selectrect(GdkEvent *event);
void finalize_selectrect(void);
gboolean start_movesel(GdkEvent *event);
void start_vertspace(GdkEvent *event);
void continue_movesel(GdkEvent *event);
void finalize_movesel(void);

void selection_delete(void);
void selection_to_clip(void);
void clipboard_paste(void);

void recolor_selection(int color);
void rethicken_selection(int val);
