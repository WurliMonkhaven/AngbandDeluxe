/* Frontend travel-and-act intent. Movement, routing and pickup remain engine
 * commands; interruption discards the intent rather than retrying a route. */
enum deluxe_travel_stage { TRAVEL_IDLE, TRAVEL_START, TRAVEL_APPROACH, TRAVEL_STEP, TRAVEL_ARRIVED, TRAVEL_DIGGING };
static enum deluxe_travel_stage travel_stage;
static struct loc travel_grid, travel_waypoint;
static unsigned long travel_level;
static bool pickup_automatic;
static const char *travel_stop_reason;
static cmd_code travel_command=CMD_PICKUP;

static cmd_code deluxe_terrain_command(struct loc grid)
{
 if(!player || !player->cave || !square_in_bounds_fully(player->cave,grid)) return CMD_NULL;
 if(square_isdisarmabletrap(player->cave,grid)) return CMD_DISARM;
 if(square_iscloseddoor(player->cave,grid)) return CMD_OPEN;
 if(square_isopendoor(player->cave,grid)) return CMD_CLOSE;
 if(square_isupstairs(player->cave,grid)) return CMD_GO_UP;
 if(square_isdownstairs(player->cave,grid)) return CMD_GO_DOWN;
 if(square_isdiggable(player->cave,grid) && !square_isperm(player->cave,grid)) return CMD_TUNNEL;
 return CMD_NULL;
}

static const char *deluxe_terrain_action(cmd_code command)
{
 return command==CMD_GO_UP?"up":command==CMD_GO_DOWN?"down":command==CMD_TUNNEL?"tunnel":
  command==CMD_DISARM?"disarm":command==CMD_OPEN?"open":command==CMD_CLOSE?"close":"";
}

static bool deluxe_adjacent_action(cmd_code command)
{
 return command==CMD_TUNNEL || command==CMD_DISARM || command==CMD_OPEN || command==CMD_CLOSE;
}

static void deluxe_capture_terrain_actions(cJSON *state)
{
 cJSON *view=cJSON_GetObjectItem(state,"dungeon"), *actions=cJSON_CreateArray();
 int x,y;
 if(view) for(y=num(view,"y",0);y<num(view,"y",0)+num(view,"height",0);++y)
  for(x=num(view,"x",0);x<num(view,"x",0)+num(view,"width",0);++x) {
   cmd_code command=deluxe_terrain_command(loc(x,y));
   if(command!=CMD_NULL) {
    cJSON *entry=cJSON_CreateObject(); number(entry,"x",x); number(entry,"y",y);
    bool adjacent=distance(player->grid,loc(x,y))==1;
    const char *hint=command==CMD_DISARM?
     (adjacent && !player_is_trapsafe(player)?"Click to attempt disarming; right-click for Disarm.":"Right-click to approach and disarm."):
     command==CMD_OPEN?(adjacent?"Click to attempt opening; right-click for Open.":"Right-click to approach and open."):
     command==CMD_CLOSE?"Click to move through; right-click to close.":
     command==CMD_TUNNEL?"Right-click to approach and tunnel.":
     command==CMD_GO_UP?"Right-click to approach and go up.":"Right-click to approach and go down.";
    if(!OPT(player,mouse_movement)) hint="Mouse movement is disabled; terrain actions are available on right-click.";
    else if(player->timed[TMD_CONFUSED]) hint="Confused: clicks attempt a random step.";
    else if(adjacent && square_monster(cave,loc(x,y)) && monster_is_visible(square_monster(cave,loc(x,y))))
     hint="Click to attack; the creature may block terrain actions.";
    string(entry,"hint",hint);
    string(entry,"action",deluxe_terrain_action(command)); cJSON_AddItemToArray(actions,entry);
   }
  }
 cJSON_AddItemToObject(state,"terrain_actions",actions);
}

/* An attempt is based on the displayed observation, including remembered or
 * hallucinated objects. Do not consult the real pile until we arrive. */
static bool deluxe_pickup_observed(struct loc grid)
{
 size_t index;
 if(!square_in_bounds_fully(cave,grid) || grid.x>=deluxe_width || grid.y>=deluxe_height || !deluxe_cells) return false;
 index=grid.y*deluxe_width+grid.x;
 return deluxe_cells_valid[index] && deluxe_cells[index].object_char!=0;
}

static bool deluxe_pickup_possible(struct loc grid)
{
 struct object *o;
 if(!square_in_bounds_fully(cave,grid)) return false;
 for(o=square_object(cave,grid);o;o=o->next)
  if(!ignore_item_ok(player,o) && (tval_is_money(o) || inven_carry_okay(o))) return true;
 return false;
}

static void deluxe_travel_event(game_event_type type, game_event_data *data, void *user)
{
 if(type==EVENT_AUTOPICKUP_BEGIN) pickup_automatic=true;
 else if(type==EVENT_AUTOPICKUP_END) pickup_automatic=false;
 else if(type==EVENT_INPUT_FLUSH) {
  /* Noticing the destination's pile is an ordinary part of arriving. Damage,
   * traps, sightings and manual interruption still cancel the intent. */
  if(!(pickup_automatic && loc_eq(player->grid,travel_grid))) travel_stage=TRAVEL_IDLE;
 } else travel_stage=TRAVEL_IDLE;
}

static bool deluxe_travel_continue(void)
{
 if(travel_stage==TRAVEL_IDLE) return false;
 bool adjacent=deluxe_adjacent_action(travel_command);
 if(travel_level!=deluxe_level || player->is_dead || (player->timed[TMD_CONFUSED] &&
    !(travel_stage==TRAVEL_START && adjacent && distance(player->grid,travel_grid)==1))) {
  travel_stage=TRAVEL_IDLE; return false;
 }
 if(travel_stage==TRAVEL_START && adjacent) {
  int best=-1,dir;
  if(distance(player->grid,travel_grid)==1) { best=0; travel_waypoint=player->grid; }
  for(dir=1;best!=0 && dir<=9;++dir) if(dir!=5) {
   struct loc adjacent=loc_sum(travel_grid,ddgrid[dir]);
   int16_t *steps=NULL; int count;
   if(!square_in_bounds_fully(player->cave,adjacent) || !square_ispassable(player->cave,adjacent)) continue;
   count=find_path(player,player->grid,adjacent,&steps); mem_free(steps);
   if(count>=0 && (best<0 || count<best)) { best=count; travel_waypoint=adjacent; }
  }
  if(best<0) { travel_stop_reason="No route to an adjacent position could be found."; travel_stage=TRAVEL_IDLE; return false; }
  if(best>0) {
   travel_stage=TRAVEL_APPROACH;
   cmdq_push(CMD_PATHFIND); cmd_set_arg_point(cmdq_peek(),"point",travel_waypoint); return true;
  }
  travel_stage=TRAVEL_ARRIVED;
 }
 if(travel_stage==TRAVEL_START) {
  if(loc_eq(player->grid,travel_grid)) travel_stage=TRAVEL_ARRIVED;
  else {
   int16_t *steps=NULL;
   int count=find_path(player,player->grid,travel_grid,&steps);
   if(count<=0) { mem_free(steps); travel_stop_reason="No walking route could be found."; travel_stage=TRAVEL_IDLE; return false; }
   /* Ordinary running stops before visible objects. Route to the previous
    * square, then take one normal step, without weakening running checks. */
   travel_waypoint=loc_diff(travel_grid,ddgrid[steps[0]]);
   mem_free(steps);
   if(count>1) {
    travel_stage=TRAVEL_APPROACH;
    cmdq_push(CMD_PATHFIND); cmd_set_arg_point(cmdq_peek(),"point",travel_waypoint);
    return true;
   }
   travel_stage=TRAVEL_STEP;
  }
 }
 if(travel_stage==TRAVEL_APPROACH) {
  if(!loc_eq(player->grid,travel_waypoint)) { travel_stage=TRAVEL_IDLE; return false; }
  travel_stage=adjacent?TRAVEL_ARRIVED:TRAVEL_STEP;
 }
 if(travel_stage==TRAVEL_STEP) {
  if(square_monster(cave,travel_grid)) {
   travel_stop_reason="Travel stopped: destination occupied.";
   travel_stage=TRAVEL_IDLE; return false;
  }
  travel_stage=TRAVEL_ARRIVED;
  cmdq_push(CMD_WALK); cmd_set_arg_direction(cmdq_peek(),"direction",motion_dir(player->grid,travel_grid));
  return true;
 }
 travel_stage=TRAVEL_IDLE;
 if(adjacent) {
  if(!loc_eq(player->grid,travel_waypoint) || deluxe_terrain_command(travel_grid)!=travel_command) return false;
  if(travel_command==CMD_TUNNEL && square_monster(cave,travel_grid)) return false;
  /* Continue after the engine's repeat batch expires. Success, futile digging,
   * danger and manual cancellation call disturb() and discard this intent. */
  if(travel_command==CMD_TUNNEL) travel_stage=TRAVEL_DIGGING;
  cmdq_push(travel_command); cmd_set_arg_direction(cmdq_peek(),"direction",motion_dir(player->grid,travel_grid)); return true;
 }
 if(travel_command==CMD_GO_UP || travel_command==CMD_GO_DOWN) {
  if(!loc_eq(player->grid,travel_grid)) return false;
  cmdq_push(travel_command); return true;
 }
 if(!loc_eq(player->grid,travel_grid) || !deluxe_pickup_possible(travel_grid)) return false;
 /* The normal pickup command owns capacity checks and item-selection prompts.
  * Queue it directly: arrival's input flush must not eat synthetic keystrokes. */
 cmdq_push(CMD_PICKUP);
 return true;
}


/* Presentation-only tracking; never changes the movement or disturbance rules. */
static bool travel_display_active, travel_display_attack;
static struct loc travel_display_grid;
static unsigned long travel_display_level;
static cmd_code travel_display_command;
static void deluxe_travel_feedback(const char *label,bool active,bool interrupted)
{
 cJSON *v=cJSON_CreateObject();
 string(v,"label",label); json_bool(v,"active",active); json_bool(v,"interrupted",interrupted);
 number(v,"x",travel_display_grid.x); number(v,"y",travel_display_grid.y); counter(v,"level_id",travel_display_level);
 event("travel.changed",v); travel_display_active=active;
}
static void deluxe_travel_begin(struct loc grid,cmd_code command)
{
 travel_stop_reason=NULL;
 travel_display_attack=command==CMD_WALK && distance(player->grid,grid)<=1 && square_monster(cave,grid)!=NULL;
 travel_display_grid=grid; travel_display_level=deluxe_level; travel_display_command=command;
 deluxe_travel_feedback(travel_display_attack?"Engaging adjacent creature":command==CMD_PICKUP?"Walking to pick up":command==CMD_TUNNEL?"Approaching wall to tunnel":command==CMD_GO_UP?"Walking to ascend":command==CMD_GO_DOWN?"Walking to descend":"Moving to destination",true,false);
}
static void deluxe_travel_finish(void)
{
 bool arrived;
 if(!travel_display_active) return;
 arrived=loc_eq(player->grid,travel_display_grid);
 if((travel_display_command==CMD_GO_UP || travel_display_command==CMD_GO_DOWN) && travel_display_level!=deluxe_level)
  deluxe_travel_feedback("Stairs used",false,false);
 else if(travel_display_level!=deluxe_level)
  deluxe_travel_feedback("Travel stopped: level changed",false,true);
 else if(travel_display_command==CMD_TUNNEL && deluxe_terrain_command(travel_display_grid)!=CMD_TUNNEL)
  deluxe_travel_feedback("Passage opened",false,false);
 else if(travel_display_attack)
  deluxe_travel_feedback("Interaction finished",false,false);
 else if(arrived)
  /* Ordinary arrivals clear the destination marker without a notification. */
  deluxe_travel_feedback(travel_display_command==CMD_PICKUP?"Arrived: pickup attempted":"",false,false);
 else
  deluxe_travel_feedback(travel_stop_reason?travel_stop_reason:"",false,true);
}
static void deluxe_route_preview(const char *id,const cJSON *p)
{
 struct loc dest=loc(num(p,"x",-1),num(p,"y",-1)),at;
 int16_t *steps=NULL; int count,i;
 cJSON *out,*points;
 const cJSON *x=cJSON_GetObjectItem(p,"x"),*y=cJSON_GetObjectItem(p,"y");
 if(!ready || active_prompt || !character_generated || !streq(phase,"playing")) { error(id,"busy","Route previews require normal play."); return; }
 if(!streq(str(p,"context"),context_text)) { error(id,"stale_revision","View changed."); return; }
 if(!cJSON_IsNumber(x) || !cJSON_IsNumber(y) || x->valuedouble!=x->valueint || y->valuedouble!=y->valueint || !square_in_bounds_fully(player->cave,dest)) {
  error(id,"invalid_argument","Invalid route destination."); return;
 }
 /* Use the same knowledge, door and trap costs as ordinary mouse pathfinding. */
 count=find_path(player,player->grid,dest,&steps);
 out=cJSON_CreateObject(); points=cJSON_CreateArray(); at=player->grid;
 for(i=count-1;i>=0;--i) {
  int xy[2]; at=loc_sum(at,ddgrid[steps[i]]); xy[0]=at.x; xy[1]=at.y;
  cJSON_AddItemToArray(points,ints(xy,2));
 }
 mem_free(steps);
 string(out,"context",context_text); number(out,"x",dest.x); number(out,"y",dest.y);
 json_bool(out,"reachable",count>=0); cJSON_AddItemToObject(out,"path",points); response(id,out);
}
