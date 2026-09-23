/* Semantic presentation adapter, included by main-deluxe.c after JSON helpers.
 * Cached values come from ordinary map drawing, never extra map_info() calls.
 * No terminal-cell parsing, RNG use, pointer handles or client gameplay rules. */
static struct map_visual *deluxe_cells;
static bool *deluxe_cells_valid;
static int deluxe_width, deluxe_height;
static unsigned long deluxe_level;

/* Looking may cycle over terrain/items as well as monsters. Confirm those as
 * locations using the engine's existing free-cursor controls. Kill targeting
 * retains its ordinary monster eligibility checks. */
static bool deluxe_look_location(void)
{
 return target_ui_current && (target_ui_current->mode&TARGET_LOOK) &&
  target_ui_current->interesting && !target_ui_current->can_confirm &&
  square_in_bounds_fully(cave,target_ui_current->grid);
}
static void deluxe_target_key(int key)
{
 if(!screen_save_depth && deluxe_look_location() &&
    (key=='t' || key=='5' || key=='0' || key=='.')) Term_keypress('o',0);
 Term_keypress(key,0);
}

static void deluxe_reset_view(void)
{
 mem_free(deluxe_cells); mem_free(deluxe_cells_valid);
 deluxe_cells = NULL; deluxe_cells_valid = NULL;
 deluxe_width = deluxe_height = 0; ++deluxe_level;
}
static void deluxe_observe_cell(struct loc grid, const struct map_visual *v)
{
 size_t index;
 if (!cave || !square_in_bounds(cave, grid)) return;
 if (deluxe_width != cave->width || deluxe_height != cave->height) {
  deluxe_reset_view(); deluxe_width = cave->width; deluxe_height = cave->height;
  deluxe_cells = mem_zalloc(deluxe_width * deluxe_height * sizeof(*deluxe_cells));
  deluxe_cells_valid = mem_zalloc(deluxe_width * deluxe_height * sizeof(*deluxe_cells_valid));
 }
 index = grid.y * deluxe_width + grid.x;
 deluxe_cells[index] = *v; deluxe_cells_valid[index] = true;
}
static void deluxe_capture_view(cJSON *state_record)
{
 int x, y, width, height;
 cJSON *view, *rows;
 /* Native targeting/aiming share the dungeon; nested recall screens still own
  * the terminal. Presentation mode never depends on parsing terminal text. */
 if ((!ready && !native_prompt && !textui_message_pending && !target_ui_current && !textui_aiming && !textui_direction && !item_choice_objects && !spell_selection) || (active_prompt && !native_prompt) || screen_save_depth || !streq(phase,"playing") || !deluxe_cells) return;
 width = MIN(SCREEN_WID, cave->width - terminal.offset_x);
 height = MIN(SCREEN_HGT, cave->height - terminal.offset_y);
 if (width < 1 || height < 1 || terminal.offset_x < 0 || terminal.offset_y < 0) return;
 for (y=0;y<height;++y) for (x=0;x<width;++x)
  if (!deluxe_cells_valid[(y+terminal.offset_y)*deluxe_width+x+terminal.offset_x]) return;
 view=cJSON_CreateObject(); rows=cJSON_CreateArray();
 json_bool(state_record,"message_pending",textui_message_pending);
 json_bool(state_record,"spell_selection",spell_selection);
 json_bool(state_record,"native_prompt",native_prompt);
 counter(view,"level_id",deluxe_level); number(view,"x",terminal.offset_x); number(view,"y",terminal.offset_y);
 number(view,"width",width); number(view,"height",height);
 for (y=0;y<height;++y) {
  cJSON *row=cJSON_CreateArray();
  for (x=0;x<width;++x) {
   const struct map_visual *v=&deluxe_cells[(y+terminal.offset_y)*deluxe_width+x+terminal.offset_x];
   int cell[13]={v->terrain_char,v->terrain_attr,v->trap_char,v->trap_attr,
    v->object_char,v->object_attr,v->actor_char,v->actor_attr,
    v->feature,v->lighting,v->seen,v->hallucinated,v->player};
   cJSON_AddItemToArray(row,ints(cell,13));
  }
  cJSON_AddItemToArray(rows,row);
 }
 cJSON_AddItemToObject(view,"cells",rows); cJSON_AddItemToObject(state_record,"dungeon",view);
 if (target_ui_current) {
  const struct target_ui_state *t=target_ui_current;
  cJSON *selection=cJSON_CreateObject(), *candidates=cJSON_CreateArray(), *path=cJSON_CreateArray();
  int i;
  string(selection,"mode",(t->mode&TARGET_KILL)?"target":"look");
  number(selection,"x",t->grid.x); number(selection,"y",t->grid.y);
  json_bool(selection,"interesting",t->interesting); json_bool(selection,"can_confirm",t->can_confirm || deluxe_look_location());
  for(i=0;i<t->candidates->n;++i) {
   int xy[2]={t->candidates->pts[i].x,t->candidates->pts[i].y};
   cJSON_AddItemToArray(candidates,ints(xy,2));
  }
  if(t->mode&TARGET_KILL) for(i=0;i<t->path_length;++i) {
   int xy[2]={t->path[i].x,t->path[i].y}; cJSON_AddItemToArray(path,ints(xy,2));
  }
  cJSON_AddItemToObject(selection,"candidates",candidates); cJSON_AddItemToObject(selection,"path",path);
  cJSON_AddItemToObject(state_record,"targeting",selection);
 }
 json_bool(state_record,"aiming",textui_aiming);
 json_bool(state_record,"direction_prompt",textui_direction);
 json_bool(state_record,"item_selection",item_choice_objects!=NULL);
}
static void deluxe_character_details(cJSON *p)
{
 const struct trap *trap = square_trap(cave,player->grid);
 const char *title = player->class->title[MIN((player->lev-1)/5,9)];
 if (player->wizard) title="Wizard";
 else if (player->total_winner) title="Winner";
 else if (player_is_shapechanged(player)) title=player->shape->name;
 string(p,"title",title); number(p,"max_level",player->max_lev);
 number(p,"experience",player->exp); number(p,"max_experience",player->max_exp);
 number(p,"level_start_experience",player->lev>1 ? (int)((int64_t)player_exp[player->lev-2]*player->expfact/100) : 0);
 number(p,"next_level_experience",player->lev<PY_MAX_LEVEL ? (int)((int64_t)player_exp[player->lev-1]*player->expfact/100) : 0);
 number(p,"light",square_light(cave,player->grid));
 string(p,"floor",trap && !square_isinvis(cave,player->grid) ? trap->kind->name : square_feat(cave,player->grid)->name);
 number(p,"recall",player->word_recall); number(p,"descent",player->deep_descent);
 number(p,"resting",player->upkeep->resting); number(p,"running",player->upkeep->running);
 number(p,"repeat",cmd_get_nrepeats()); number(p,"study",player->upkeep->new_spells);
 number(p,"extra_moves",player->state.num_moves); json_bool(p,"unignoring",player->unignoring);
 json_bool(p,"trap_detected",square_isdtrap(cave,player->grid));
 if (OPT(player,birth_feelings) && player->depth) {
  int objects=cave->feeling/10, monsters=cave->feeling%10;
  char feeling[40], treasure[8];
  if(cave->feeling_squares<z_info->feeling_need) my_strcpy(treasure,"?",sizeof(treasure));
  else if(objects<2) my_strcpy(treasure,objects?"$":"*",sizeof(treasure));
  else strnfmt(treasure,sizeof(treasure),"%d",11-objects);
  if(monsters) strnfmt(feeling,sizeof(feeling),"%d / %s",10-monsters,treasure);
  else strnfmt(feeling,sizeof(feeling),"? / %s",treasure);
  string(p,"feeling",feeling);
 }
 if(player->upkeep->health_who) {
  const struct monster *m=player->upkeep->health_who;
  cJSON *tracked=cJSON_CreateObject();
  string(tracked,"name",m->race?m->race->name:""); number(tracked,"hp",m->hp); number(tracked,"max_hp",m->maxhp);
  json_bool(tracked,"visible",monster_is_visible(m) && !player->timed[TMD_IMAGE]);
  cJSON_AddItemToObject(p,"tracked_creature",tracked);
 }
}
