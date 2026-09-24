/* Copy ephemeral engine visuals, batching at input boundaries. No delays, RNG,
 * inferred paths or hidden tiles. Bounds also cover unusually busy turns. */
static cJSON *deluxe_projectiles, *deluxe_projectile_points;
static unsigned long deluxe_projectile_level;
static int deluxe_projectile_count, deluxe_projectile_groups;
static int deluxe_projectile_x, deluxe_projectile_y;
static const char *deluxe_projectile_kind;

static void deluxe_projectiles_publish(void)
{
 if (deluxe_projectiles) {
  cJSON *v = cJSON_CreateObject();
  counter(v, "level_id", deluxe_projectile_level);
  cJSON_AddItemToObject(v, "effects", deluxe_projectiles);
  event("projectile.feedback", v);
 }
 deluxe_projectiles = deluxe_projectile_points = NULL;
 deluxe_projectile_count = deluxe_projectile_groups = 0;
 deluxe_projectile_kind = NULL;
}

static cJSON *deluxe_projectile_begin(const char *kind, bool blast)
{
 cJSON *v;
 if (deluxe_projectile_groups >= 64) return NULL;
 if (!deluxe_projectiles) {
  deluxe_projectiles = cJSON_CreateArray();
  deluxe_projectile_level = deluxe_level;
 }
 v = cJSON_CreateObject();
 string(v, "element", kind); json_bool(v, "blast", blast);
 deluxe_projectile_points = cJSON_AddArrayToObject(v, "tiles");
 cJSON_AddItemToArray(deluxe_projectiles, v);
 deluxe_projectile_kind = kind;
 ++deluxe_projectile_groups;
 return deluxe_projectile_points;
}

static void deluxe_projectile_tile(cJSON *points, int x, int y, int distance)
{
 int tile[3] = { x, y, distance };
 if (!points || deluxe_projectile_count >= 2048) return;
 cJSON_AddItemToArray(points, cJSON_CreateIntArray(tile, 3));
 ++deluxe_projectile_count;
}

static void deluxe_projectile(game_event_type type, game_event_data *data, void *user)
{
 const char *kind;
 int x, y;
 bool seen;
 if (!data || !player || player->timed[TMD_IMAGE]) return;
 if (deluxe_projectiles && deluxe_projectile_level != deluxe_level)
  deluxe_projectiles_publish();
 if (type == EVENT_EXPLOSION) {
  int i, radius = 0;
  cJSON *points = NULL;
  for (i = 0; i < data->explosion.num_grids; ++i)
   radius = MAX(radius, data->explosion.distance_to_grid[i]);
  /* Beams have already supplied their path, with all distances zero. */
  if (radius || data->explosion.num_grids == 1) {
   for (i = 0; i < data->explosion.num_grids; ++i) {
    if (!data->explosion.player_sees_grid[i]) continue;
    if (!points) points = deluxe_projectile_begin(proj_idx_to_name(data->explosion.proj_type), true);
    deluxe_projectile_tile(points, data->explosion.blast_grid[i].x,
     data->explosion.blast_grid[i].y, data->explosion.distance_to_grid[i]);
   }
  }
  deluxe_projectile_points = NULL;
  return;
 }
 if (type == EVENT_BOLT) {
  kind = proj_idx_to_name(data->bolt.proj_type);
  x = data->bolt.x; y = data->bolt.y; seen = data->bolt.seen;
 } else {
  kind = "MISSILE";
  x = data->missile.x; y = data->missile.y; seen = data->missile.seen;
 }
 if (!seen) { deluxe_projectile_points = NULL; return; }
 if (!deluxe_projectile_points || !streq(kind, deluxe_projectile_kind) ||
     ABS(x-deluxe_projectile_x)>1 || ABS(y-deluxe_projectile_y)>1 ||
     (x==deluxe_projectile_x && y==deluxe_projectile_y))
  deluxe_projectile_points = deluxe_projectile_begin(kind, false);
 deluxe_projectile_tile(deluxe_projectile_points, x, y, 0);
 deluxe_projectile_x = x; deluxe_projectile_y = y;
}
