/* Semantic presentation adapter, included by main-anybandui.c after JSON helpers.
 * The classic viewport uses ordinary map drawing; the free camera uses the
 * engine's read-only known-map renderer for the full level.
 * No terminal-cell parsing, RNG use, pointer handles or client gameplay rules. */
static struct map_visual *anybandui_cells;
static bool *anybandui_cells_valid;
static int anybandui_width, anybandui_height;
static unsigned long anybandui_level;
static bool anybandui_full_map;
static int anybandui_view_width, anybandui_view_height, anybandui_view_x, anybandui_view_y;
static bool anybandui_view_initialized;

/* Looking may cycle over terrain/items as well as monsters. Confirm those as
 * locations using the engine's existing free-cursor controls. Kill targeting
 * retains its ordinary monster eligibility checks. */
static bool anybandui_look_location(void)
{
 return target_ui_current && (target_ui_current->mode&TARGET_LOOK) &&
  target_ui_current->interesting && !target_ui_current->can_confirm &&
  square_in_bounds_fully(cave,target_ui_current->grid);
}
static void anybandui_target_key(int key)
{
 if(!screen_save_depth && anybandui_look_location() &&
    (key=='t' || key=='5' || key=='0' || key=='.')) Term_keypress('o',0);
 Term_keypress(key,0);
}

static void anybandui_reset_view(void)
{
 mem_free(anybandui_cells); mem_free(anybandui_cells_valid);
 anybandui_view_initialized = false;
 anybandui_cells = NULL; anybandui_cells_valid = NULL;
 anybandui_width = anybandui_height = 0; ++anybandui_level;
}
static void anybandui_observe_cell(struct loc grid, const struct map_visual *v)
{
 size_t index;
 if (!cave || !square_in_bounds(cave, grid)) return;
 if (anybandui_width != cave->width || anybandui_height != cave->height) {
  anybandui_reset_view(); anybandui_width = cave->width; anybandui_height = cave->height;
  anybandui_cells = mem_zalloc(anybandui_width * anybandui_height * sizeof(*anybandui_cells));
  anybandui_cells_valid = mem_zalloc(anybandui_width * anybandui_height * sizeof(*anybandui_cells_valid));
 }
 index = grid.y * anybandui_width + grid.x;
 anybandui_cells[index] = *v; anybandui_cells_valid[index] = true;
}
static void anybandui_capture_view(cJSON *state_record)
{
 int x, y, width, height, ox, oy;
 cJSON *view, *rows, *observed_items;
 /* Leaving the world adds one screen depth before flushing post-fatal spell
  * messages. There is no overlay yet: retain the dungeon and native ribbon
  * until these are acknowledged. Nested screens still own the terminal. */
 const bool final_messages = player && player->is_dead &&
  textui_message_pending && screen_save_depth == 1 && streq(phase,"playing");
 /* Native targeting/aiming share the dungeon; nested recall screens still own
  * the terminal. Presentation mode never depends on parsing terminal text. */
 if ((!ready && !native_prompt && !textui_message_pending && !target_ui_current && !textui_aiming && !textui_direction && !item_choice_objects && !spell_selection) || (active_prompt && !native_prompt) || (screen_save_depth && !final_messages) || !streq(phase,"playing") || !anybandui_cells) return;
 ox = anybandui_full_map ? 0 : terminal.offset_x;
 oy = anybandui_full_map ? 0 : terminal.offset_y;
 width = anybandui_full_map ? cave->width : MIN(SCREEN_WID, cave->width - ox);
 height = anybandui_full_map ? cave->height : MIN(SCREEN_HGT, cave->height - oy);
 if (width < 1 || height < 1 || terminal.offset_x < 0 || terminal.offset_y < 0) return;
 if (!anybandui_full_map && anybandui_view_width>0) {
  const struct loc focus=target_ui_current?target_ui_current->grid:player->grid;
  width=MIN(anybandui_view_width,cave->width); height=MIN(anybandui_view_height,cave->height);
  const bool centered=OPT(player,center_player) && !player->upkeep->running && !target_ui_current;
  if(!anybandui_view_initialized || centered || focus.x<anybandui_view_x+3 || focus.x>=anybandui_view_x+width-3) anybandui_view_x=focus.x-width/2;
  if(!anybandui_view_initialized || centered || focus.y<anybandui_view_y+2 || focus.y>=anybandui_view_y+height-2) anybandui_view_y=focus.y-height/2;
  anybandui_view_x=MAX(0,MIN(anybandui_view_x,cave->width-width));
  anybandui_view_y=MAX(0,MIN(anybandui_view_y,cave->height-height));
  ox=anybandui_view_x; oy=anybandui_view_y; anybandui_view_initialized=true;
 }
 if (!anybandui_full_map && !anybandui_view_width) for (y=0;y<height;++y) for (x=0;x<width;++x)
  if (!anybandui_cells_valid[(y+terminal.offset_y)*anybandui_width+x+terminal.offset_x]) return;
 view=cJSON_CreateObject(); rows=cJSON_CreateArray(); observed_items=cJSON_CreateArray();
 json_bool(state_record,"message_pending",textui_message_pending);
 json_bool(state_record,"spell_selection",spell_selection);
 json_bool(state_record,"native_prompt",native_prompt);
 counter(view,"level_id",anybandui_level); number(view,"x",ox); number(view,"y",oy);
 json_bool(view,"full_level",anybandui_full_map);
 number(view,"width",width); number(view,"height",height);
 for (y=0;y<height;++y) {
  cJSON *row=cJSON_CreateArray();
  for (x=0;x<width;++x) {
   struct map_visual whole;
   const struct map_visual *v;
   if (anybandui_full_map || anybandui_view_width>0) { map_visual_readonly(loc(x+ox,y+oy), &whole); v=&whole; }
   else v=&anybandui_cells[(y+oy)*anybandui_width+x+ox];
   int cell[13]={v->terrain_char,v->terrain_attr,v->trap_char,v->trap_attr,
    v->object_char,v->object_attr,v->actor_char,v->actor_attr,
    v->feature,v->lighting,v->seen,v->hallucinated,v->player};
   cJSON_AddItemToArray(row,ints(cell,13));
   /* Describe remembered piles, not live-world objects at these coordinates.
    * Copies keep object_desc's everseen bookkeeping out of read-only capture. */
   if(v->object_char && !v->hallucinated && player->cave) {
    struct loc grid=loc(x+ox,y+oy);
    const struct object *o;
    for(o=square_object(player->cave,grid);o;o=o->next) {
     struct object copy=*o; char label[512]; cJSON *entry;
     if(o->kind!=unknown_item_kind && o->kind!=unknown_gold_kind && ignore_known_item_ok(player,o)) continue;
     if(o->kind==unknown_item_kind) my_strcpy(label,"An unknown item",sizeof(label));
     else if(o->kind==unknown_gold_kind) my_strcpy(label,"Unknown treasure",sizeof(label));
     else { copy.known=&copy; describe(&copy,false,label,sizeof(label),0); }
     entry=cJSON_CreateObject(); number(entry,"x",grid.x); number(entry,"y",grid.y);
     string(entry,"label",label); number(entry,"quantity",o->number);
     number(entry,"color",o->kind->base ? o->kind->base->attr : COLOUR_WHITE);
     /* The remembered object contains only discovered properties. Never use
      * the live pile to decorate an unidentified or hallucinated item. */
     const char *aura="";
     if(o->kind!=unknown_item_kind && o->kind!=unknown_gold_kind) {
      if(cursed(o)) aura="cursed";
      else if(o->artifact) aura="artifact";
      else for(int rune=0;rune<max_runes();++rune) if(object_has_rune(o,rune)) { aura="rune"; break; }
     }
     string(entry,"aura",aura);
     cJSON_AddItemToArray(observed_items,entry);
    }
   }
  }
  cJSON_AddItemToArray(rows,row);
 }
 anybandui_capture_tiles(view,ox,oy,width,height);
 cJSON_AddItemToObject(view,"items",observed_items);
 cJSON_AddItemToObject(view,"cells",rows); cJSON_AddItemToObject(state_record,"dungeon",view);
 if (target_ui_current) {
  const struct target_ui_state *t=target_ui_current;
  cJSON *selection=cJSON_CreateObject(), *candidates=cJSON_CreateArray(), *path=cJSON_CreateArray();
  int i;
  string(selection,"mode",(t->mode&TARGET_KILL)?"target":"look");
  number(selection,"x",t->grid.x); number(selection,"y",t->grid.y);
  json_bool(selection,"interesting",t->interesting); json_bool(selection,"can_confirm",t->can_confirm || anybandui_look_location());
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
 if (textui_aiming || (target_ui_current && (target_ui_current->mode&TARGET_KILL)))
  number(state_record,"blast_radius",anybandui_blast_radius());
 json_bool(state_record,"aiming",textui_aiming);
 json_bool(state_record,"direction_prompt",textui_direction);
 json_bool(state_record,"item_selection",item_choice_objects!=NULL);
}
static void anybandui_character_details(cJSON *p)
{
 anybandui_character_sheet(p);
 char feeling_description[256];
 const struct trap *trap = square_trap(cave,player->grid);
 const char *title = player->class->title[MIN((player->lev-1)/5,9)];
 if (player->wizard) title="Wizard";
 else if (player->total_winner) title="Winner";
 else if (player_is_shapechanged(player)) title=player->shape->name;
 string(p,"title",title); number(p,"max_level",player->max_lev);
 number(p,"experience",player->exp); number(p,"max_experience",player->max_exp);
 number(p,"level_start_experience",player->lev>1 ? (int)((int64_t)player_exp[player->lev-2]*player->expfact/100) : 0);
 number(p,"next_level_experience",player->lev<PY_MAX_LEVEL ? (int)((int64_t)player_exp[player->lev-1]*player->expfact/100) : 0);
 format_level_feeling(feeling_description,sizeof(feeling_description));
 string(p,"feeling_description",feeling_description);
 number(p,"depth_feet",player->depth*50);
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
