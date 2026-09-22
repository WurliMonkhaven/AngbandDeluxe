/* Frontend walk-and-pickup intent. Movement, routing and pickup remain engine
 * commands; interruption discards the intent rather than retrying a route. */
enum deluxe_pickup_stage { PICKUP_IDLE, PICKUP_START, PICKUP_APPROACH, PICKUP_STEP, PICKUP_ARRIVED };
static enum deluxe_pickup_stage pickup_stage;
static struct loc pickup_grid, pickup_waypoint;
static unsigned long pickup_level;
static bool pickup_automatic;

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

static void deluxe_pickup_event(game_event_type type, game_event_data *data, void *user)
{
 if(type==EVENT_AUTOPICKUP_BEGIN) pickup_automatic=true;
 else if(type==EVENT_AUTOPICKUP_END) pickup_automatic=false;
 else if(type==EVENT_INPUT_FLUSH) {
  /* Noticing the destination's pile is an ordinary part of arriving. Damage,
   * traps, sightings and manual interruption still cancel the intent. */
  if(!(pickup_automatic && loc_eq(player->grid,pickup_grid))) pickup_stage=PICKUP_IDLE;
 } else pickup_stage=PICKUP_IDLE;
}

static bool deluxe_pickup_continue(void)
{
 if(pickup_stage==PICKUP_IDLE) return false;
 if(pickup_level!=deluxe_level || player->is_dead || player->timed[TMD_CONFUSED]) {
  pickup_stage=PICKUP_IDLE; return false;
 }
 if(pickup_stage==PICKUP_START) {
  if(loc_eq(player->grid,pickup_grid)) pickup_stage=PICKUP_ARRIVED;
  else {
   int16_t *steps=NULL;
   int count=find_path(player,player->grid,pickup_grid,&steps);
   if(count<=0) { mem_free(steps); pickup_stage=PICKUP_IDLE; return false; }
   /* Ordinary running stops before visible objects. Route to the previous
    * square, then take one normal step, without weakening running checks. */
   pickup_waypoint=loc_diff(pickup_grid,ddgrid[steps[0]]);
   mem_free(steps);
   if(count>1) {
    pickup_stage=PICKUP_APPROACH;
    cmdq_push(CMD_PATHFIND); cmd_set_arg_point(cmdq_peek(),"point",pickup_waypoint);
    return true;
   }
   pickup_stage=PICKUP_STEP;
  }
 }
 if(pickup_stage==PICKUP_APPROACH) {
  if(!loc_eq(player->grid,pickup_waypoint)) { pickup_stage=PICKUP_IDLE; return false; }
  pickup_stage=PICKUP_STEP;
 }
 if(pickup_stage==PICKUP_STEP) {
  if(square_monster(cave,pickup_grid)) {
   pickup_stage=PICKUP_IDLE; return false;
  }
  pickup_stage=PICKUP_ARRIVED;
  cmdq_push(CMD_WALK); cmd_set_arg_direction(cmdq_peek(),"direction",motion_dir(player->grid,pickup_grid));
  return true;
 }
 pickup_stage=PICKUP_IDLE;
 if(!loc_eq(player->grid,pickup_grid) || !deluxe_pickup_possible(pickup_grid)) return false;
 /* The normal pickup command owns capacity checks and item-selection prompts.
  * Queue it directly: arrival's input flush must not eat synthetic keystrokes. */
 cmdq_push(CMD_PICKUP);
 return true;
}
