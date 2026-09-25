/**
   \file ui-map.h
   \brief Writing level map info to the screen
 *
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */

extern void grid_data_as_text(struct grid_data *g, int *ap, wchar_t *cp,
							  int *tap, wchar_t *tcp);
extern void move_cursor_relative(int y, int x);
extern void print_rel(wchar_t c, uint8_t a, int y, int x);
extern void prt_map(void);
extern void display_map(int *cy, int *cx);
extern void do_cmd_view_map(void);

/* Optional presentation observer. Called only by an existing map draw; never
 * performs a second map query or exposes engine pointers to a client. */
struct map_visual {
 int terrain_attr, trap_attr, object_attr, actor_attr;
 wchar_t terrain_char, trap_char, object_char, actor_char;
 int feature, lighting;
 bool seen, hallucinated, player;
};
extern void (*map_visual_hook)(struct loc grid, const struct map_visual *visual);
extern void (*map_visual_reset_hook)(void);
extern void map_visual_readonly(struct loc grid, struct map_visual *visual);
extern void map_info_as_text(struct loc grid, struct grid_data *g, int *a,
 wchar_t *c, int *ta, wchar_t *tc);
