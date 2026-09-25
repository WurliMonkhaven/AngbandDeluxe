/* Copy ephemeral engine visuals, batching at input boundaries. No delays, RNG,
 * inferred paths or hidden tiles. Bounds also cover unusually busy turns. */
static cJSON *anybandui_projectiles, *anybandui_projectile_points;
static unsigned long anybandui_projectile_level;
static int anybandui_projectile_count, anybandui_projectile_groups;
static int anybandui_projectile_x, anybandui_projectile_y;
static const char *anybandui_projectile_kind;

static void anybandui_projectiles_publish(void)
{
 if (anybandui_projectiles) {
  cJSON *v = cJSON_CreateObject();
  counter(v, "level_id", anybandui_projectile_level);
  cJSON_AddItemToObject(v, "effects", anybandui_projectiles);
  event("projectile.feedback", v);
 }
 anybandui_projectiles = anybandui_projectile_points = NULL;
 anybandui_projectile_count = anybandui_projectile_groups = 0;
 anybandui_projectile_kind = NULL;
}

static cJSON *anybandui_projectile_begin(const char *kind, bool blast, bool arc)
{
 cJSON *v;
 if (anybandui_projectile_groups >= 64) return NULL;
 if (!anybandui_projectiles) {
  anybandui_projectiles = cJSON_CreateArray();
  anybandui_projectile_level = anybandui_level;
 }
 v = cJSON_CreateObject();
 string(v, "element", kind); json_bool(v, "blast", blast); json_bool(v,"arc",arc);
 anybandui_projectile_points = cJSON_AddArrayToObject(v, "tiles");
 cJSON_AddItemToArray(anybandui_projectiles, v);
 anybandui_projectile_kind = kind;
 ++anybandui_projectile_groups;
 return anybandui_projectile_points;
}

static void anybandui_projectile_tile(cJSON *points, int x, int y, int distance)
{
 int tile[3] = { x, y, distance };
 if (!points || anybandui_projectile_count >= 2048) return;
 cJSON_AddItemToArray(points, cJSON_CreateIntArray(tile, 3));
 ++anybandui_projectile_count;
}

static void anybandui_projectile(game_event_type type, game_event_data *data, void *user)
{
 const char *kind;
 int x, y;
 bool seen;
 if (!data || !player || player->timed[TMD_IMAGE]) return;
 if (anybandui_projectiles && anybandui_projectile_level != anybandui_level)
  anybandui_projectiles_publish();
 if (type == EVENT_EXPLOSION) {
  int i, radius = 0;
  cJSON *points = NULL;
  for (i = 0; i < data->explosion.num_grids; ++i)
   radius = MAX(radius, data->explosion.distance_to_grid[i]);
  /* Beams have already supplied their path, with all distances zero. */
  if (radius || data->explosion.num_grids == 1) {
   for (i = 0; i < data->explosion.num_grids; ++i) {
    if (!data->explosion.player_sees_grid[i]) continue;
    if (!points) points = anybandui_projectile_begin(proj_idx_to_name(data->explosion.proj_type), true, data->explosion.arc);
    anybandui_projectile_tile(points, data->explosion.blast_grid[i].x,
     data->explosion.blast_grid[i].y, data->explosion.distance_to_grid[i]);
   }
  }
  anybandui_projectile_points = NULL;
  return;
 }
 if (type == EVENT_BOLT) {
  kind = proj_idx_to_name(data->bolt.proj_type);
  x = data->bolt.x; y = data->bolt.y; seen = data->bolt.seen;
 } else {
  kind = "MISSILE";
  x = data->missile.x; y = data->missile.y; seen = data->missile.seen;
 }
 if (!seen) { anybandui_projectile_points = NULL; return; }
 if (!anybandui_projectile_points || !streq(kind, anybandui_projectile_kind) ||
     ABS(x-anybandui_projectile_x)>1 || ABS(y-anybandui_projectile_y)>1 ||
     (x==anybandui_projectile_x && y==anybandui_projectile_y))
  anybandui_projectile_points = anybandui_projectile_begin(kind, false, false);
 anybandui_projectile_tile(anybandui_projectile_points, x, y, 0);
 anybandui_projectile_x = x; anybandui_projectile_y = y;
}
