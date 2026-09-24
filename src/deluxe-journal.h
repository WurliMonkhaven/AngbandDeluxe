/* Read-only projection of the native save-persistent history. Queries do not
 * generate milestones, consume RNG, identify artifacts or advance the game. */
static cJSON *deluxe_journal(bool finished)
{
 struct history_info *history;
 size_t i, count = history_get_list(player, &history);
 bool levels[PY_MAX_LEVEL + 1] = { false };
 cJSON *out = cJSON_CreateObject(), *entries = cJSON_AddArrayToObject(out, "entries");
 for (i = 0; i < count; ++i) {
  const struct history_info *h = &history[i];
  const char *kind = NULL;
  cJSON *entry;
  if (hist_has(h->type, HIST_ARTIFACT_UNKNOWN)) continue;
  /* Post-mortem identification also reveals artifacts never discovered. */
  if (hist_has(h->type, HIST_ARTIFACT_LOST) && prefix(h->event, "Missed ")) continue;
  if (hist_has(h->type, HIST_PLAYER_BIRTH)) kind = "beginning";
  else if (hist_has(h->type, HIST_SLAY_UNIQUE)) kind = "unique";
  else if (hist_has(h->type, HIST_ARTIFACT_KNOWN)) kind = "artifact";
  else if (hist_has(h->type, HIST_REACH_DEPTH)) kind = "depth";
  else if (hist_has(h->type, HIST_GAIN_LEVEL)) {
   if (h->clev < 1 || h->clev > PY_MAX_LEVEL || levels[h->clev]) continue;
   levels[h->clev] = true; kind = "level";
  }
  if (!kind) continue;
  entry = cJSON_CreateObject(); string(entry, "kind", kind); string(entry, "text", h->event);
  number(entry, "turn", h->turn); number(entry, "depth", h->dlev); number(entry, "level", h->clev);
  json_bool(entry, "lost", hist_has(h->type, HIST_ARTIFACT_LOST));
  cJSON_AddItemToArray(entries, entry);
 }
 if (finished) {
  char text[160]; cJSON *entry = cJSON_CreateObject();
  if (player->total_winner) my_strcpy(text, "Victory. A legend remembered.", sizeof(text));
  else if (streq(player->died_from, "Retiring")) my_strcpy(text, "Retired from adventuring.", sizeof(text));
  else strnfmt(text, sizeof(text), "The journey ended: %s", player->died_from);
  string(entry, "kind", "ending"); string(entry, "text", text);
  number(entry, "turn", player->total_energy / 100); number(entry, "depth", player->depth); number(entry, "level", player->lev);
  cJSON_AddItemToArray(entries, entry);
 }
 return out;
}
