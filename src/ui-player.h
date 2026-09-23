/**
 * \file ui-player.h
 * \brief character info
 */

#ifndef UI_PLAYER_H
#define UI_PLAYER_H

/* Read-only semantic rows, shared with the original character screen. */
void character_sheet_rows(void (*emit)(void *, const char *, const char *, const char *, int), void *user);
void display_player_stat_info(void);
void display_player_xtra_info(void);
void display_player(int mode);
void write_character_dump(ang_file *fff);
bool dump_save(const char *path);
void do_cmd_change_name(void);

#endif /* !UI_PLAYER_H */
