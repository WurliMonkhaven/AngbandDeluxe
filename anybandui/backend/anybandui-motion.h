/* Bounded, observational movement events; never infer walking from positions. */
static cJSON *anybandui_motion;
static unsigned long anybandui_motion_level;
static void anybandui_motion_event(game_event_type type, game_event_data *data, void *user)
{
 cJSON *v, *tiles;
 int x,y;
 if (!data || !data->motion.visible) return;
 if (anybandui_motion && anybandui_motion_level!=anybandui_level) { cJSON_Delete(anybandui_motion); anybandui_motion=NULL; }
 if (!anybandui_motion) { anybandui_motion=cJSON_CreateArray(); anybandui_motion_level=anybandui_level; }
 if (cJSON_GetArraySize(anybandui_motion)>=128) return;
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
 cJSON_AddItemToArray(anybandui_motion,v);
}
static void anybandui_motion_publish(void)
{
 if(anybandui_motion) {
  cJSON *v=cJSON_CreateObject(); counter(v,"level_id",anybandui_motion_level);
  cJSON_AddItemToObject(v,"effects",anybandui_motion); event("motion.feedback",v); anybandui_motion=NULL;
 }
}
