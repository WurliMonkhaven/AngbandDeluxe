/* Bounded, observational movement events; never infer walking from positions. */
static cJSON *deluxe_motion;
static unsigned long deluxe_motion_level;
static void deluxe_motion_event(game_event_type type, game_event_data *data, void *user)
{
 cJSON *v, *tiles;
 int x,y;
 if (!data || !data->motion.visible) return;
 if (deluxe_motion && deluxe_motion_level!=deluxe_level) { cJSON_Delete(deluxe_motion); deluxe_motion=NULL; }
 if (!deluxe_motion) { deluxe_motion=cJSON_CreateArray(); deluxe_motion_level=deluxe_level; }
 if (cJSON_GetArraySize(deluxe_motion)>=128) return;
 v=cJSON_CreateObject();
 number(v,"index",data->motion.index); json_bool(v,"blink",data->motion.blink);
 number(v,"x",data->motion.from.x); number(v,"y",data->motion.from.y);
 number(v,"tx",data->motion.to.x); number(v,"ty",data->motion.to.y);
 tiles=cJSON_AddArrayToObject(v,"tiles");
 if(data->motion.blink) for(y=-3;y<=3;++y) for(x=-3;x<=3;++x) {
  struct loc grid=loc(data->motion.from.x+x,data->motion.from.y+y);
  int tile[2]={grid.x,grid.y};
  if(x*x+y*y<=9 && square_in_bounds(cave,grid) && square_isseen(cave,grid))
   cJSON_AddItemToArray(tiles,cJSON_CreateIntArray(tile,2));
 }
 cJSON_AddItemToArray(deluxe_motion,v);
}
static void deluxe_motion_publish(void)
{
 if(deluxe_motion) {
  cJSON *v=cJSON_CreateObject(); counter(v,"level_id",deluxe_motion_level);
  cJSON_AddItemToObject(v,"effects",deluxe_motion); event("motion.feedback",v); deluxe_motion=NULL;
 }
}
