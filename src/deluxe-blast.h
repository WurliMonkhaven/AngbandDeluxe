/* Known-effect metadata only; never roll dice or execute an effect to preview. */
static int deluxe_blast_radius(void)
{
 const struct effect *e;
 int radius = 0;
 if (!aim_effect || !player || player->timed[TMD_CONFUSED] ||
     player->timed[TMD_IMAGE] || player->timed[TMD_BLIND]) return 0;
 for (e = aim_effect; e; e = e->next) {
  if (e->index == EF_RANDOM || e->index == EF_SELECT) return 0;
  if (e->index == EF_BALL) {
   int r = e->radius ? e->radius : 2;
   if (e->other > 0) r += player->lev / e->other;
   radius = MAX(radius, r);
  }
 }
 return MIN(radius, z_info->max_range);
}

static void deluxe_blast_preview(const char *id, const cJSON *p)
{
 const cJSON *jx = cJSON_GetObjectItem(p, "x"), *jy = cJSON_GetObjectItem(p, "y");
 const int radius = deluxe_blast_radius();
 struct loc grid = loc(num(p, "x", -1), num(p, "y", -1)), centre, cells[255];
 cJSON *out, *tiles;
 int i, n;
 if (!streq(str(p, "context"), context_text)) {
  error(id, "stale_revision", "Aiming context changed."); return;
 }
 if (!radius || (!textui_aiming && (!target_ui_current || !(target_ui_current->mode & TARGET_KILL)))) {
  error(id, "busy", "No known ball spell is being aimed."); return;
 }
 if (!cJSON_IsNumber(jx) || !cJSON_IsNumber(jy) || jx->valuedouble != jx->valueint ||
     jy->valuedouble != jy->valueint || !square_in_bounds_fully(player->cave, grid)) {
  error(id, "invalid_argument", "Invalid target tile."); return;
 }
 out = cJSON_CreateObject(); tiles = cJSON_AddArrayToObject(out, "tiles");
 string(out, "context", context_text); number(out, "x", grid.x); number(out, "y", grid.y);
 number(out, "radius", radius);
 /* All geometry reads the remembered chunk, including paths and LOS. */
 n = project_ball_area(player->cave, player->grid, grid, radius, &centre, cells);
 for (i = 0; i < n; ++i) {
  int xy[2] = { cells[i].x, cells[i].y };
  if (square_isknown(player->cave, cells[i])) cJSON_AddItemToArray(tiles, ints(xy, 2));
 }
 response(id, out);
}

static void deluxe_debug_blast(int radius)
{
 struct effect effect = { 0 };
 const struct effect *previous = aim_effect;
 bool ident = false, old_target = OPT(player, use_old_target), aimed;
 int dir = DIR_UNKNOWN;
 effect.index = EF_BALL; effect.subtype = PROJ_FIRE; effect.radius = radius;
 effect.dice = dice_new(); dice_parse_string(effect.dice, "25");
 /* A testing cast must always open aiming, even with Use old target enabled. */
 OPT(player, use_old_target) = false;
 aim_effect = &effect;
 aimed = get_aim_dir(&dir);
 aim_effect = previous;
 OPT(player, use_old_target) = old_target;
 if (aimed) effect_do(&effect, source_player(), NULL, &ident, true, dir, 0, 0, NULL);
 dice_free(effect.dice);
}
