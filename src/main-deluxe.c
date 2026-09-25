/* Angband Deluxe local semantic adapter. GPLv2, as for the engine.
 * All engine access runs on its main thread at input boundaries. */
#include "angband.h"
#include "cJSON.h"
#include "cave.h"
#include "cmd-core.h"
#include "game-input.h"
#include "game-world.h"
#include "init.h"
#include "message.h"
#include "deluxe-messages.h"
#include "monster.h"
#include "mon-util.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-ignore.h"
#include "obj-info.h"
#include "obj-knowledge.h"
#include "obj-pile.h"
#include "obj-make.h"
#include "obj-curse.h"
#include "option.h"
#include "obj-util.h"
#include "obj-tval.h"
#include "player.h"
#include "player-history.h"
#include "project.h"
#include "effects.h"
#include "z-dice.h"
#include "player-birth.h"
#include "ui-birth.h"
#include "player-properties.h"
#include "player-calcs.h"
#include "player-path.h"
#include "player-spell.h"
#include "player-util.h"
#include "player-timed.h"
#include "store.h"
#include "score.h"
#include "savefile.h"
#include "ui-store.h"
#include "ui-command.h"
#include "ui-context.h"
#include "ui-display.h"
#include "ui-game.h"
#include "ui-init.h"
#include "ui-input.h"
#include "ui-object.h"
#include "ui-options.h"
#include "ui-keymap.h"
#include "ui-knowledge.h"
#include "ui-map.h"
#include "ui-menu.h"
#include "ui-spell.h"
#include "ui-target.h"
#include "target.h"
#include "ui-output.h"
#include "ui-player.h"
#include "obj-properties.h"
#include "obj-slays.h"
#include "trap.h"
#include "ui-term.h"
#include "z-quark.h"
#include <locale.h>
#include <sys/stat.h>
#ifdef WINDOWS
#include <windows.h>



#else
#include <sys/select.h>
#include <unistd.h>
#endif

#define FRAME_LIMIT (1024 * 1024)
#define SCREEN_W 100
#define SCREEN_H 34
static term terminal;
static bool connected = true, negotiated, initialized, ready, closing;
static bool native_inventory, native_equipment;
static unsigned long revision, sequence, context_id;
static char revision_text[32], context_text[32];
static const char *phase = "launcher";
static bool debug_blink;
static bool debug_glow_items;
static int debug_glow_kind;
static struct loc debug_glow_grid;
static int debug_breath_element = -1;
static int debug_damage, debug_experience, debug_blast_radius;
static int debug_status=-1,debug_status_amount;
static int native_target_mode;
static bool native_target_immediate;
static struct loc native_target_grid;
static bool click_after_look;
static struct loc look_click_grid;
static int look_click_mods;
static char action[80];
static cJSON *snapshot, *reply_value, *active_prompt;
static bool native_prompt;
static cJSON *next_choices;
static struct object **item_choice_objects;
static int item_choice_count;
static struct object *item_handles[8192], *pending_item;
static bool pickup_single;
static size_t item_handle_count;
static bool (*original_get_item)(struct object **, const char *, const char *, cmd_code, item_tester, int);
static char *seen_ids[32768];
static size_t seen_count;
static int launch_mode = -1;
static bool use_native_birth;
static char birth_race[80],birth_class[80];
static cJSON *run_report;
static bool run_finishing;

struct command_entry { const char *id, *label; char key; };
static const struct command_entry commands[] = {
 {"core.walk", "Walk", ';'}, {"core.run", "Run", '.'},
 {"core.hold", "Wait / stay", ','}, {"core.rest", "Rest", 'R'},
 {"core.up", "Ascend stairs", '<'}, {"core.down", "Descend stairs", '>'},
 {"core.pickup", "Pick up", 'g'}, {"core.wield", "Wield / wear", 'w'},
 {"core.takeoff", "Take off", 't'}, {"core.drop", "Drop", 'd'},
 {"core.use", "Use item", 'U'}, {"core.quaff", "Drink potion", 'q'},
 {"core.read", "Read scroll", 'r'}, {"core.eat", "Eat", 'E'},
 {"core.cast", "Cast spell", 'm'}, {"core.study", "Study spell", 'G'},
 {"core.browse", "Browse spells", 'b'},
 {"core.fire", "Fire ammunition", 'f'}, {"core.throw", "Throw", 'v'},
 {"core.open", "Open", 'o'}, {"core.close", "Close door", 'c'},
 {"core.disarm", "Disarm", 'D'}, {"core.tunnel", "Tunnel", 'T'},
 {"core.inscribe", "Inscribe", '{'}, {"core.ignore", "Ignore", 'k'},
 {"core.look", "Look", 'l'}, {"core.target", "Target", '*'},
 {"core.knowledge", "Knowledge", '~'}, {"core.character", "Character", 'C'},
 {"core.inventory", "Inventory", 'i'}, {"core.equipment", "Equipment", 'e'},
 {"core.options", "Engine options", '='}, {"core.help", "Help", '?'}
};

static const char *str(cJSON *j, const char *key)
{
 cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
 return cJSON_IsString(v) ? v->valuestring : "";
}
static int num(cJSON *j, const char *key, int fallback)
{
 cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
 return cJSON_IsNumber(v) ? v->valueint : fallback;
}
static void string(cJSON *j, const char *key, const char *value)
{ cJSON_AddStringToObject(j, key, value ? value : ""); }
static void number(cJSON *j, const char *key, int value)
{ cJSON_AddNumberToObject(j, key, value); }
static void json_bool(cJSON *j, const char *key, bool value)
{ cJSON_AddBoolToObject(j, key, value); }
static void counter(cJSON *j, const char *key, unsigned long value)
{ char b[32]; strnfmt(b, sizeof(b), "%lu", value); string(j, key, b); }
static void send_json(cJSON *j)
{
 char *s = cJSON_PrintUnformatted(j);
 if (!s || strlen(s) >= FRAME_LIMIT) { fprintf(stderr, "Deluxe frame exceeds limit\n"); exit(2); }
 if (puts(s) < 0 || fflush(stdout)) exit(2);
 cJSON_free(s); cJSON_Delete(j);
}
static void response(const char *id, cJSON *result)
{
 cJSON *r = cJSON_CreateObject();
 string(r, "kind", "response"); string(r, "id", id);
 cJSON_AddItemToObject(r, "result", result); send_json(r);
}
static void error(const char *id, const char *code, const char *message)
{
 cJSON *r = cJSON_CreateObject(), *e = cJSON_CreateObject();
 string(r, "kind", "response"); string(r, "id", id);
 string(e, "code", code); string(e, "message", message);
 cJSON_AddItemToObject(r, "error", e); send_json(r);
}
static void event(const char *name, cJSON *data)
{
 cJSON *r = cJSON_CreateObject();
 string(r, "kind", "event"); counter(r, "seq", ++sequence);
 string(r, "session_id", "session-1"); string(r, "event", name);
 cJSON_AddItemToObject(r, "data", data); send_json(r);
}
/* Sound cues are transient events, never replayed from message history. */
static void deluxe_sound_event(int type)
{
 const char *name=message_sound_name(type);
 if(name && *name) {
  cJSON *j=cJSON_CreateObject(); string(j,"name",name); event("sound.play",j);
 }
}
static void deluxe_target_selected(void)
{
 cJSON *j=cJSON_CreateObject(); string(j,"name","target_confirmed"); event("sound.play",j);
}
static cJSON *command_list(void)
{
 size_t i; cJSON *a = cJSON_CreateArray();
 for (i = 0; i < N_ELEMENTS(commands); ++i) {
  cJSON *j = cJSON_CreateObject();
  string(j, "id", commands[i].id); string(j, "label", commands[i].label);
  number(j, "key", commands[i].key); cJSON_AddItemToArray(a, j);
 }
 return a;
}
static cJSON *ints(const int *values, int count)
{
 /* These presentation arrays are immutable and only serialized, never queried
  * through cJSON. Encode integer tokens directly to avoid a heap node and
  * floating-point formatting for every cell component. The wire schema stays
  * an ordinary JSON array. Only engine integers enter this raw JSON buffer. */
 char *buffer=mem_alloc((size_t)count*12+3), *out=buffer;
 cJSON *result;
 int i;
 *out++='[';
 for(i=0;i<count;++i) {
  char digits[10]; int used=0;
  uint32_t value=(uint32_t)values[i];
  if(i) *out++=',';
  if(values[i]<0) { *out++='-'; value=0u-value; }
  do { digits[used++]=(char)('0'+value%10); value/=10; } while(value);
  while(used) *out++=digits[--used];
 }
 *out++=']'; *out=0;
 result=cJSON_CreateRaw(buffer); mem_free(buffer);
 return result;
}
static bool cursed(const struct object *o)
{
 int i; if (!o || !o->curses) return false;
 for (i = 1; i < z_info->curse_max; ++i) if (o->curses[i].power) return true;
 return false;
}
/* object_desc marks kinds/egos as seen. Use local metadata copies for queries. */
static void describe(const struct object *obj, bool actual, char *buf, size_t n, uint32_t extra)
{
 /* The engine's money-name path requires a player for its ignore check,
  * even in spoiler mode. Omniscient labels have no ignore annotation. */
 if (actual && tval_is_money(obj)) {
  strnfmt(buf, n, "%d gold pieces worth of %s", obj->pval, obj->kind->name);
  return;
 }
 struct object copy = *obj, known;
 struct object_kind kind = *obj->kind;
 struct ego_item ego;
 copy.kind = &kind;
 if (obj->ego) { ego = *obj->ego; copy.ego = &ego; }
 if (actual) { known = copy; copy.known = &known; }
 else if (obj->known) {
  known = *obj->known;
  if (known.kind == obj->kind) known.kind = &kind;
  if (known.ego == obj->ego && obj->ego) known.ego = &ego;
  copy.known = &known;
 } else { my_strcpy(buf, "Unobserved item", n); return; }
 object_desc(buf, n, &copy, ODESC_PREFIX | ODESC_FULL |
  (actual ? ODESC_SPOIL : extra), actual ? NULL : player);
}
/* Use the same prose as classic inspection. Its hypothetical equipment/state
 * swaps are restored synchronously, and calc_bonuses uses update=false. */
static void inspection_section(void *user, const char *id, const char *title,
 const wchar_t *wide, size_t length)
{
 size_t i, used=0;
 char *text;
 cJSON *row;
 while(length && (*wide==' ' || *wide=='\n' || *wide=='\r' || *wide=='\t')) { ++wide; --length; }
 while(length && (wide[length-1]==' ' || wide[length-1]=='\n' || wide[length-1]=='\r')) --length;
 if(!length) return;
 text=mem_alloc(length*4+1);
 for(i=0;i<length;++i) {
  uint32_t code=(uint32_t)wide[i];
  if(code>=0xd800 && code<=0xdbff && i+1<length && wide[i+1]>=0xdc00 && wide[i+1]<=0xdfff)
   code=0x10000+((code-0xd800)<<10)+wide[++i]-0xdc00;
  used+=utf32_to_utf8(text+used,length*4+1-used,&code,1,NULL);
 }
 text[used]=0; row=cJSON_CreateObject();
 string(row,"id",id); string(row,"title",title); string(row,"text",text);
 cJSON_AddItemToArray(cJSON_GetObjectItem((cJSON *)user,"description_sections"),row); mem_free(text);
}
static void inspection_combat(void *user, const char *kind, const char *label,
 int value, int str_plus, int dex_plus)
{
 cJSON *row=cJSON_CreateObject();
 string(row,"kind",kind); string(row,"label",label); number(row,"value",value);
 number(row,"str",str_plus); number(row,"dex",dex_plus);
 cJSON_AddItemToArray(cJSON_GetObjectItem((cJSON *)user,"combat_details"),row);
}
static void inspection_description(cJSON *record, const struct object *obj)
{
 textblock *tb;
 const wchar_t *wide;
 size_t length, i, used = 0;
 char *text;
 uint32_t rng_state[RAND_DEG], rng_index = state_i, rng_value = Rand_value;
 bool rng_quick = Rand_quick;
#ifndef NDEBUG
 struct player_state before = player->state;
 struct object **slots = mem_alloc(player->body.count * sizeof(*slots));
 for (i = 0; i < (size_t)player->body.count; ++i) slots[i] = player->body.slots[i].obj;
#endif
 if (!obj->known) {
  string(record, "description", "You do not know what this is.");
#ifndef NDEBUG
  mem_free(slots);
#endif
  return;
 }
 /* effect_describe rolls dice to obtain their components. Those incidental
  * rolls must not consume gameplay entropy when building a read-only view. */
 memcpy(rng_state, STATE, sizeof(rng_state));
 {
  cJSON *sections=cJSON_CreateArray();
  cJSON_AddItemToObject(record,"description_sections",sections);
  cJSON_AddItemToObject(record,"combat_details",cJSON_CreateArray());
  tb = object_info_sections(obj, OINFO_NONE, inspection_section, inspection_combat, record);
 }
 memcpy(STATE, rng_state, sizeof(rng_state));
 state_i = rng_index; Rand_value = rng_value; Rand_quick = rng_quick;
 wide = textblock_text(tb); length = wcslen(wide);
 text = mem_alloc(length * 4 + 1);
 for (i = 0; i < length; ++i) {
  uint32_t code = (uint32_t)wide[i];
  if (code >= 0xd800 && code <= 0xdbff && i + 1 < length &&
      wide[i + 1] >= 0xdc00 && wide[i + 1] <= 0xdfff) {
   code = 0x10000 + ((code - 0xd800) << 10) + wide[++i] - 0xdc00;
  }
  used += utf32_to_utf8(text + used, length * 4 + 1 - used, &code, 1, NULL);
 }
 text[used] = 0; string(record, "description", text);
 mem_free(text); textblock_free(tb);
#ifndef NDEBUG
 assert(memcmp(&before, &player->state, sizeof(before)) == 0);
 for (i = 0; i < (size_t)player->body.count; ++i) assert(slots[i] == player->body.slots[i].obj);
 mem_free(slots);
#endif
}
#include "deluxe-spells.h"
#include "deluxe-item-rules.h"
static cJSON *item_record(const struct object *o, const char *location, int index)
{
 char name[512], id[80]; int i;
 cJSON *j = cJSON_CreateObject(), *a = cJSON_CreateObject(), *k = cJSON_CreateObject();
 strnfmt(id, sizeof(id), "item-%lu-%d", revision, index);
 if (index > 0 && index < (int)N_ELEMENTS(item_handles)) {
  item_handles[index] = (struct object *)o; item_handle_count = index + 1;
 }
 string(j, "id", id); string(j, "location", location);
 {
  char binding[256]; int used;
  used=strnfmt(binding,sizeof(binding),"angband-kind-%d-ego-%d-art-%d",o->kind->kidx,o->ego?o->ego->eidx:0,o->artifact?o->artifact->aidx:0);
  string(j,"kind_key",binding);
  if(tval_is_wearable(o)) {
   used+=strnfmt(binding+used,sizeof(binding)-used,"-%d-%d-%d",o->to_h,o->to_d,o->to_a);
   for(i=0;i<OBJ_MOD_MAX && used<(int)sizeof(binding)-16;++i)
    used+=strnfmt(binding+used,sizeof(binding)-used,"-%d",o->modifiers[i]);
  }
  string(j,"binding_key",binding); string(j,"category",tval_find_name(o->tval));
 }

 json_bool(j,"comparison_available",tval_is_wearable(o) && wield_slot(o)>=0 && !object_is_equipped(player->body,o));
 json_bool(j,"on_player_tile",streq(location,"Floor") && !tval_is_money(o) && loc_eq(player->grid,o->grid) && o->known && !ignore_item_ok(player,o) && !player->timed[TMD_IMAGE]);
 json_bool(j,"can_pickup",streq(location,"Floor") && (loc_eq(player->grid,o->grid) || square_isseen(cave,o->grid)) && !ignore_item_ok(player,o) && (tval_is_money(o) || inven_carry_okay(o)));
 {
  cJSON *actions = cJSON_CreateArray();
  bool carried = object_is_carried(player, o);
  bool equipped = object_is_equipped(player->body, o);
  if (item_is_available((struct object *)o)) {
   if (!equipped && obj_can_wear(o)) cJSON_AddItemToArray(actions, cJSON_CreateString("core.wield"));
   if (obj_is_useable(o) && (!obj_is_activatable(o) || equipped)) {
    const char *use=tval_is_potion(o)?"core.quaff":tval_is_scroll(o)?"core.read":tval_is_edible(o)?"core.eat":"core.use";
    cJSON_AddItemToArray(actions,cJSON_CreateString(use));
   }
   if(equipped && obj_can_takeoff(o)) cJSON_AddItemToArray(actions,cJSON_CreateString("core.takeoff"));
   if(obj_can_fire(o)) cJSON_AddItemToArray(actions,cJSON_CreateString("core.fire"));
   if(obj_can_throw(o)) cJSON_AddItemToArray(actions,cJSON_CreateString("core.throw"));
   if (carried && (!equipped || obj_can_takeoff(o))) cJSON_AddItemToArray(actions, cJSON_CreateString("core.drop"));
   cJSON_AddItemToArray(actions, cJSON_CreateString("core.inscribe"));
   if(obj_can_browse(o)) cJSON_AddItemToArray(actions,cJSON_CreateString("core.browse"));
  }
  cJSON_AddItemToObject(j, "actions", actions);
 }
 describe(o, false, name, sizeof(name), streq(location,"Store") ? ODESC_STORE : 0); string(j, "label", name);
 describe(o, true, name, sizeof(name), 0); string(a, "label", name);
 string(a, "kind", o->kind->name); json_bool(a, "cursed", cursed(o));
 json_bool(a, "artifact", o->artifact != NULL); json_bool(a, "ego", o->ego != NULL);
 number(a, "to_hit", o->to_h); number(a, "to_damage", o->to_d);
 number(a, "armour", o->ac); number(a, "to_armour", o->to_a);
 number(a, "dice", o->dd); number(a, "sides", o->ds);
 number(a, "weight", o->weight); number(a, "timeout", o->timeout);
 if(tval_can_have_charges(o)) number(j,"charges",o->pval);
 if(tval_is_rod(o)) number(j,"charging",number_charging(o));
 else if(obj_is_activatable(o)) number(j,"charging",o->timeout>0?1:0);
 cJSON_AddItemToObject(a, "modifiers", cJSON_CreateArray());
 for (i = 0; i < OBJ_MOD_MAX; ++i)
  cJSON_AddItemToArray(cJSON_GetObjectItem(a, "modifiers"), cJSON_CreateNumber(o->modifiers[i]));
 json_bool(k, "cursed", cursed(o->known));
 json_bool(k, "identified", o->known && object_fully_known(o));
 cJSON_AddItemToObject(j, "actual", a); cJSON_AddItemToObject(j, "player_known", k);
 number(j, "quantity", o->number); number(j, "x", o->grid.x); number(j, "y", o->grid.y);
 number(j, "glyph", o->kind->d_char); number(j, "color", o->kind->d_attr);
 /* List text uses the engine's item-type colour, not its dungeon glyph colour. */
 number(j, "name_color", !streq(location,"Store") && !streq(location,"Home") &&
  tval_is_book_k(o->kind) && !player_object_to_book(player,o) ? COLOUR_SLATE : o->kind->base->attr);
 string(j, "inscription", quark_str(o->note));
 inspection_description(j, o);
 if(item_is_available((struct object *)o)) deluxe_item_preferences(j,o);
 deluxe_book_record(j,o);
 if (object_is_carried(player, o)) {
  char label[2] = { gear_to_label(player, (struct object *)o), 0 };
  string(j, "selection_key", label);
 }
 return j;
}
/* Explicit developer action: create real, identified samples on separate tiles. */
static void deluxe_spawn_glow_items(void)
{
 struct loc spots[3]; int count=0;
 struct object *samples[3]={NULL,NULL,NULL};
 const int first=debug_glow_kind?debug_glow_kind-1:0,last=debug_glow_kind?debug_glow_kind:3;
 if(debug_glow_kind) { spots[first]=debug_glow_grid; count=3; }
 for(int radius=1;radius<=5 && count<3;++radius)
  for(int dy=-radius;dy<=radius && count<3;++dy)
   for(int dx=-radius;dx<=radius && count<3;++dx) {
    struct loc grid=loc(player->grid.x+dx,player->grid.y+dy);
    if(MAX(abs(dx),abs(dy))!=radius || !square_in_bounds_fully(cave,grid) ||
       !square_isseen(cave,grid) || !square_isfloor(cave,grid) ||
       square_object(cave,grid) || square_monster(cave,grid) || square_isplayer(cave,grid)) continue;
    spots[count++]=grid;
   }
 if(count<3) { msg("Find three clear, visible floor tiles nearby to spawn glow test items."); return; }
 struct object_kind *kind=lookup_kind(TV_SWORD,lookup_sval(TV_SWORD,"Dagger"));
 if(!kind) { msg("No test weapon kind is available."); return; }
 if(first==0) for(int i=1;i<z_info->a_max;++i) if(a_info[i].name && !is_artifact_created(&a_info[i])) {
  struct object *obj=object_new(); make_fake_artifact(obj,&a_info[i]);
  if(obj->kind && !cursed(obj)) { samples[0]=obj; break; }
  object_delete(NULL,NULL,&obj);
 }
 if(first==0 && !samples[0]) { msg("No unused artifact is available for the glow test."); return; }
 for(int i=MAX(1,first);i<last;++i) { samples[i]=object_new(); object_prep(samples[i],kind,0,MINIMISE); }
 if(samples[1]) samples[1]->to_d=3;
 bool has_curse=last<3;
 if(last==3) for(int i=1;i<z_info->curse_max;++i) if(curses[i].name && curses[i].poss[kind->tval] && append_object_curse(samples[2],i,20)) { has_curse=true; break; }
 if(!has_curse) {
  for(int i=0;i<3;++i) object_delete(NULL,NULL,&samples[i]);
  msg("No suitable curse is available for the glow test."); return;
 }
 for(int i=first;i<last;++i) {
  struct object *obj=samples[i]; obj->origin=ORIGIN_CHEAT;
  obj->origin_depth=convert_depth_to_origin(player->depth);
  if(!floor_carry(cave,spots[i],obj,false)) { object_delete(NULL,NULL,&obj); continue; }
  if(obj->artifact) mark_artifact_created(obj->artifact,true);
  object_touch(player,obj);
  for(int rune=0;rune<max_runes() && !object_runes_known(obj);++rune) object_learn_unknown_rune(player,obj);
  square_note_spot(cave,spots[i]); square_light_spot(cave,spots[i]);
 }
 player->upkeep->notice|=PN_IGNORE;
 player->upkeep->redraw|=PR_MAP|PR_ITEMLIST;
 msg(debug_glow_kind?"Glow test item placed.":"Glow test items placed nearby: artifact, runed weapon and cursed weapon.");
}
#include "deluxe-character.h"
#include "deluxe-options.h"
#include "deluxe-keybindings.h"
#include "deluxe-journal.h"
#include "deluxe-run.h"
#include "deluxe-save-summary.h"
#include "deluxe-status.h"
#include "deluxe-knowledge.h"
static int deluxe_blast_radius(void);
#include "deluxe-view.h"
#include "deluxe-blast.h"
#include "deluxe-projectiles.h"
#include "deluxe-motion.h"
#include "deluxe-compare.h"
#include "deluxe-travel.h"
#include "deluxe-store.h"

static void publish(void);
static void pump(void);
#include "deluxe-birth.h"
static cJSON *capture(void)
{
#ifndef NDEBUG
 uint32_t rng_state[RAND_DEG], rng_index = state_i, rng_value = Rand_value;
 bool rng_quick = Rand_quick;
 memcpy(rng_state, STATE, sizeof(rng_state));
#endif
 cJSON *s = cJSON_CreateObject(), *screen = cJSON_CreateArray();
 cJSON *items = cJSON_CreateArray(), *monsters = cJSON_CreateArray();
 cJSON *messages = cJSON_CreateArray(); int y, x, i, index = 0;
 string(s, "session_id", "session-1"); string(s, "revision", revision_text);
 string(s, "phase", phase); string(s, "context", context_text);
 string(s, "readiness", ready ? "ready" : "awaiting_prompt");
 number(s, "turn", (int)turn);
 for (y = 0; y < terminal.hgt; ++y) {
  cJSON *row = cJSON_CreateArray();
  for (x = 0; x < terminal.wid; ++x) {
   int cell[2] = { (int)terminal.scr->c[y][x], terminal.scr->a[y][x] };
   cJSON_AddItemToArray(row, ints(cell, 2));
  }
  cJSON_AddItemToArray(screen, row);
 }
 cJSON_AddItemToObject(s, "terminal", screen);
 {
  cJSON *cursor = cJSON_CreateObject();
  number(cursor, "x", terminal.scr->cx); number(cursor, "y", terminal.scr->cy);
  json_bool(cursor, "visible", terminal.scr->cv && !terminal.scr->cu);
  cJSON_AddItemToObject(s, "cursor", cursor);
 }
 if(birth_active) cJSON_AddItemToObject(s,"birth",deluxe_birth_record());
 if (character_generated && player && cave) {
  struct object *o; cJSON *p = cJSON_CreateObject(), *slots = cJSON_CreateArray();
  cJSON *map = cJSON_CreateObject(), *terrain = cJSON_CreateArray(), *known = cJSON_CreateArray();
  cJSON *visible = cJSON_CreateArray();
  string(p, "name", player->full_name); string(p, "race", player->race->name);
  string(p, "class", player->class->name); number(p, "hp", player->chp); number(p, "max_hp", player->mhp);
  number(p, "hp_warning", player->mhp * player->opts.hitpoint_warn / 10);
  /* The fatal message is flushed for acknowledgement before is_dead is set.
   * Negative HP alone is insufficient: bloodlust can keep the player alive. */
  json_bool(p, "death_pending", player->is_dead ||
   (player->chp < 0 && messages_num() > 0 && message_type(0) == MSG_DEATH));
  number(p, "sp", player->csp); number(p, "max_sp", player->msp); number(p, "level", player->lev);
  json_bool(p,"spellcasting",player->class->magic.total_spells>0);
  number(p,"new_spells",player->upkeep->new_spells);
  number(p, "food", player->timed[TMD_FOOD]); number(p, "food_max", PY_FOOD_MAX);
  number(p, "depth", player->depth); number(p, "gold", player->au);
  deluxe_character_details(p);
  number(p, "speed", player->state.speed - 110); number(p, "armour", player->known_state.ac + player->known_state.to_a);
  number(p, "x", player->grid.x); number(p, "y", player->grid.y);
  if(target_is_set()) {
   struct loc grid; cJSON *selection=cJSON_CreateObject(); target_get(&grid);
   number(selection,"x",grid.x); number(selection,"y",grid.y);
   struct monster *tracked=target_get_monster();
   json_bool(selection,"monster",tracked && tracked->race);
   cJSON_AddItemToObject(s,"selected_target",selection);
  }
  cJSON_AddItemToObject(p, "stats", ints(player->known_state.stat_use, STAT_MAX));
  cJSON_AddItemToObject(p, "actual_stats", ints(player->state.stat_use, STAT_MAX));
  cJSON_AddItemToObject(p,"statuses",deluxe_statuses());
  cJSON_AddItemToObject(s, "player", p);
  deluxe_capture_view(s);
  deluxe_capture_terrain_actions(s);
  for (o = player->gear; o; o = o->next) {
   int slot = object_slot(player->body, o);
   const char *place = slot >= 0 && slot < player->body.count ? player->body.slots[slot].name :
    (object_is_in_quiver(player, o) ? "Quiver" : "Pack");
   cJSON_AddItemToArray(items, item_record(o, place, ++index));
  }
  for (i = 0; i < player->body.count; ++i) {
   cJSON *slot = cJSON_CreateObject(); string(slot, "label", player->body.slots[i].name);
   json_bool(slot, "occupied", player->body.slots[i].obj != NULL); cJSON_AddItemToArray(slots, slot);
  }
  cJSON_AddItemToObject(s, "slots", slots);
  for (y = 0; y < cave->height; ++y) {
   int actual_row[512], known_row[512]; char seen[512];
   for (x = 0; x < cave->width; ++x) {
    struct loc g = loc(x, y);
    actual_row[x] = square(cave, g)->feat;
    known_row[x] = player->cave ? square(player->cave, g)->feat : 0;
    seen[x] = square_isseen(cave, g) ? '1' : '0';
    for (o = square_object(cave, g); o; o = o->next)
     cJSON_AddItemToArray(items, item_record(o, "Floor", ++index));
   }
   seen[cave->width] = 0;
   cJSON_AddItemToArray(terrain, ints(actual_row,cave->width));
   cJSON_AddItemToArray(known, ints(known_row,cave->width));
   cJSON_AddItemToArray(visible, cJSON_CreateString(seen));
  }
  counter(map,"level_id",deluxe_level);
  cJSON_AddItemToObject(map, "actual", terrain); cJSON_AddItemToObject(map, "known", known);
  cJSON_AddItemToObject(map, "visible", visible); cJSON_AddItemToObject(s, "map", map);
  for (i = 1; i < cave->mon_max; ++i) {
   struct monster *m = cave_monster(cave, i); cJSON *j; char id[80];
   if (!m || !m->race) continue;
   j = cJSON_CreateObject(); strnfmt(id, sizeof(id), "monster-%lu-%d", revision, i);
   string(j, "id", id); string(j, "name", m->race->name); number(j,"race_id",m->race->ridx);
   number(j, "x", m->grid.x); number(j, "y", m->grid.y);
   number(j, "hp", m->hp); number(j, "max_hp", m->maxhp);
   number(j, "glyph", m->race->d_char); number(j, "color", m->race->d_attr);
   json_bool(j, "visible", monster_is_visible(m)); json_bool(j, "asleep", m->m_timed[MON_TMD_SLEEP] > 0);
   json_bool(j,"afraid",m->m_timed[MON_TMD_FEAR]>0); number(j,"index",i);
   { char description[160]=""; look_mon_desc(description,sizeof(description),i); string(j,"condition",description); }
   cJSON_AddItemToArray(monsters, j);
  }
 }
 if (active_store) deluxe_capture_store(s, items, &index);
 for (i = 0; i < messages_num() && i < 200; ++i) {
  cJSON *m = cJSON_CreateObject(); string(m, "text", message_str(i));
  number(m, "count", message_count(i)); number(m, "category", message_type(i));
  string(m,"group",deluxe_message_group(message_type(i)));
  cJSON_AddItemToArray(messages, m);
 }
 cJSON_AddItemToObject(s, "items", items); cJSON_AddItemToObject(s, "monsters", monsters);
 cJSON_AddItemToObject(s, "messages", messages);
 if(streq(phase,"dead") && !run_report) run_report=deluxe_run_record(s);
 if(run_report) cJSON_AddItemToObject(s,"run",cJSON_Duplicate(run_report,true));
#ifndef NDEBUG
 assert(state_i == rng_index && Rand_value == rng_value && Rand_quick == rng_quick);
 assert(memcmp(rng_state, STATE, sizeof(rng_state)) == 0);
#endif
 return s;
}
static void publish(void)
{
 deluxe_projectiles_publish();
 ++revision; ++context_id;
 item_handle_count = 0;
 strnfmt(revision_text, sizeof(revision_text), "%lu", revision);
 strnfmt(context_text, sizeof(context_text), "%lu", context_id);
 cJSON_Delete(snapshot); snapshot = capture();
 /* send_json serializes synchronously. Borrow the immutable snapshot instead
  * of allocating and freeing a second copy of every dungeon cell. */
 event("state.changed", cJSON_CreateObjectReference(snapshot->child));
 deluxe_motion_publish();
 deluxe_knowledge_publish();
}
static bool input_available(void)
{
#ifdef WINDOWS
 DWORD n = 0; HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
 return PeekNamedPipe(h, NULL, 0, NULL, &n, NULL) && n > 0;
#else
 fd_set fds; struct timeval t = {0, 0};
 FD_ZERO(&fds); FD_SET(0, &fds); return select(1, &fds, NULL, NULL, &t) > 0;
#endif
}
static void complete(void)
{
 pending_item = NULL;
 pending_spell = -1;
 if (action[0]) {
  cJSON *j = cJSON_CreateObject(); string(j, "action_id", action);
  string(j, "outcome", "resolved"); string(j, "revision", revision_text);
  event("action.completed", j); action[0] = 0;
 }
}
static void pump(void);
static cJSON *prompt(const char *type, const char *text, int maximum, const char *initial)
{
 cJSON *p = cJSON_CreateObject(), *v;
 deluxe_travel_finish();
 native_prompt = true;
 ready = false; publish();
 string(p, "prompt_id", context_text); string(p, "type", type); string(p, "text", text);
 number(p, "maximum", maximum); string(p, "initial", initial);
 if(streq(type,"quantity") && quantity_item) {
  char label[512];
  cJSON *item=cJSON_CreateObject();
  describe(quantity_item,false,label,sizeof(label),0);
  string(item,"label",label); number(item,"name_color",quantity_item->kind->base->attr);
  number(item,"quantity",quantity_item->number); cJSON_AddItemToObject(p,"item",item);
  if(active_store && store_operation==STORE_BUY && active_store->feat!=FEAT_HOME) {
   cJSON *prices=cJSON_CreateArray(); int n;
   /* Quote the exact split stack, including its share of device charges. */
   for(n=1;n<=maximum;++n) {
    struct object *part=object_new();
    object_copy_amt(part,quantity_item,n);
    cJSON_AddItemToArray(prices,cJSON_CreateNumber(price_item(active_store,part,false,n)));
    object_delete(NULL,NULL,&part);
   }
   cJSON_AddItemToObject(p,"purchase_totals",prices); number(p,"gold",player->au);
  }
 }

 if(textui_rest_prompt) string(p,"selection_kind","rest");
 if(spell_selection) {
  string(p,"selection_kind","spell"); json_bool(p,"browse",spell_browsing);
 }
 if(item_choice_objects) {
  int i, mapped=0; cJSON *records=cJSON_GetObjectItem(snapshot,"items");
  for(i=0;i<item_choice_count;++i) {
   size_t index;
   cJSON *entry=cJSON_GetArrayItem(next_choices,i);
   for(index=1;index<item_handle_count;++index) if(item_handles[index]==item_choice_objects[i]) {
    cJSON *record=cJSON_GetArrayItem(records,(int)index-1);
    string(entry,"item_id",str(record,"id")); ++mapped; break;
   }
  }
  if(mapped==item_choice_count) string(p,"selection_kind","item");
 }
 if (next_choices) { cJSON_AddItemToObject(p, "choices", next_choices); next_choices = NULL; }
 active_prompt = p; event("prompt.requested", cJSON_Duplicate(p, true));
 while (!reply_value && connected) pump();
 v = reply_value; reply_value = NULL; active_prompt = NULL; cJSON_Delete(p);
 native_prompt = false;
 return v;
}
static bool check_hook(const char *text)
{ cJSON *v = prompt("confirmation", text, 0, ""); bool b = cJSON_IsTrue(v); cJSON_Delete(v); return b; }
static bool string_hook(const char *text, char *buf, size_t len)
{
 cJSON *v = prompt("text", text, (int)len - 1, buf); bool ok = cJSON_IsString(v);
 if (ok) my_strcpy(buf, v->valuestring, len); cJSON_Delete(v); return ok;
}
static int quantity_hook(const char *text, int max)
{ cJSON *v = prompt("quantity", text, max, "1"); int n = cJSON_IsNumber(v) ? v->valueint : 0; cJSON_Delete(v); return n; }

static bool item_hook(struct object **choice, const char *text, const char *reject,
 cmd_code cmd, item_tester tester, int mode)
{
 size_t cap = z_info->pack_size + z_info->quiver_size + z_info->floor_size + player->body.count;
 struct object **objects; int count, i, chosen = -1; cJSON *v;
 int keymode = OPT(player, rogue_like_commands) ? KEYMAP_MODE_ROGUE : KEYMAP_MODE_ORIG;
 if (!reject) return original_get_item(choice, text, reject, cmd, tester, mode);
 objects = mem_zalloc(cap * sizeof(*objects));
 count = scan_items(objects, cap, player, mode, tester);
 if (!count) { if (reject) msg("%s", reject); mem_free(objects); pending_item = NULL; return false; }
 if (pending_item) {
  for (i = 0; i < count; ++i) if (objects[i] == pending_item) chosen = i;
  pending_item = NULL;
 }
 if (chosen < 0) {
  next_choices = cJSON_CreateArray();
  for (i = 0; i < count; ++i) {
   char label[512], id[32]; cJSON *j = cJSON_CreateObject();
   describe(objects[i], false, label, sizeof(label), 0); strnfmt(id, sizeof(id), "%d", i);
   string(j, "id", id); string(j, "label", label); cJSON_AddItemToArray(next_choices, j);
   if(object_is_carried(player,objects[i])) {
    char shortcut[2]={gear_to_label(player,objects[i]),0}; string(j,"shortcut",shortcut);
   }
  }
  item_choice_objects=objects; item_choice_count=count;
  v = prompt("choice", text, count, "");
  item_choice_objects=NULL; item_choice_count=0;
  if (cJSON_IsString(v)) chosen = atoi(v->valuestring);
  cJSON_Delete(v);
 }
 if (chosen >= 0 && chosen < count && get_item_allow(objects[chosen], cmd_lookup_key(cmd,keymode), cmd, (mode & IS_HARMLESS) != 0)) {
  *choice = objects[chosen]; mem_free(objects); return true;
 }
 mem_free(objects); return false;
}

static void pump(void)
{
 static char line[FRAME_LIMIT + 2]; cJSON *r, *p; const char *id, *method; size_t n, i;
 if (!fgets(line, sizeof(line), stdin)) {
  connected = false;
  if (character_generated && player && !player->is_dead) {
   terms_disconnecting = 1; save_game_checked();
  }
  exit(0);
 }
 n = strlen(line);
 if (n >= FRAME_LIMIT || !strchr(line, '\n')) { fprintf(stderr, "Invalid frame size\n"); exit(2); }
 { const char *end = NULL;
  r = cJSON_ParseWithOpts(line, &end, true);
 }
 if (!r) { error("", "invalid_request", "Expected one JSON object."); return; }
 id = str(r, "id"); method = str(r, "method"); p = cJSON_GetObjectItem(r, "params");
 if (!*id || strlen(id) > 64 || !streq(str(r, "kind"), "request") || !cJSON_IsObject(p)) {
  error(id, "invalid_request", "Request needs kind, id, method and params."); goto done;
 }
 for (i = 0; i < seen_count; ++i) if (streq(id, seen_ids[i])) {
  error(id, "duplicate_id", "A request ID cannot execute twice."); goto done;
 }
 if (seen_count == N_ELEMENTS(seen_ids)) { error(id, "busy", "Connection request limit reached."); goto done; }
 seen_ids[seen_count++] = string_make(id);
 if (streq(method, "hello")) {
  bool match = false; cJSON *v, *out;
  cJSON_ArrayForEach(v, cJSON_GetObjectItem(p, "protocols"))
   if (num(v, "major", -1) == 0 && num(v, "minor", -1) == 1) match = true;
  if (!match) { error(id, "unsupported_protocol", "This development backend speaks 0.1, not stable v1."); goto done; }
  negotiated = true;
  native_inventory=cJSON_IsTrue(cJSON_GetObjectItem(p,"native_inventory"));
  native_equipment=cJSON_IsTrue(cJSON_GetObjectItem(p,"native_equipment"));
  out = cJSON_Parse("{\"protocol\":{\"major\":0,\"minor\":1},\"engine\":{\"id\":\"org.angband.angband\",\"version\":\"4.2.6-deluxe-dev\",\"save_compatibility\":\"angband-4.2.6\"},\"capabilities\":{\"state.player\":1,\"state.items\":1,\"state.map\":1,\"state.monsters\":1,\"state.messages\":1,\"commands\":1,\"prompts.basic\":1,\"prompts.items\":1,\"spells\":1,\"audio.events\":1,\"session.replay\":1,\"run.summary\":1,\"journal\":1,\"keybindings\":1,\"options\":1,\"knowledge.watch\":1,\"knowledge\":1,\"item.rules\":1,\"item.compare\":1,\"interaction.birth\":1,\"interaction.store\":1,\"interaction.inventory\":1,\"debug.glow_items\":1,\"debug.experience\":1,\"debug.blast\":1,\"debug.breath\":1,\"debug.blink\":1,\"targeting.blast\":1,\"debug.status\":1,\"debug.quit\":1,\"terminal.fallback\":1,\"presentation.dungeon\":1,\"interaction.targeting\":1,\"interaction.route\":1,\"interaction.mouse\":1,\"interaction.pickup\":1,\"interaction.terrain\":1},\"max_frame_bytes\":1048576}");
  response(id, out); goto done;
 }
 if (!negotiated) { error(id, "unsupported_protocol", "Negotiate first."); goto done; }
 if (streq(method, "commands.list")) response(id, command_list());
 else if (streq(method, "state.get")) response(id, snapshot ? cJSON_CreateObjectReference(snapshot->child) : cJSON_CreateObject());
 else if(streq(method,"knowledge.unwatch")) { deluxe_knowledge_unwatch(); response(id,cJSON_CreateObject()); }
 else if(streq(method,"knowledge.list") || streq(method,"knowledge.get")) deluxe_knowledge_request(id,method,p);
 else if(streq(method,"item.rules.list")) {
  if(!character_generated) error(id,"wrong_phase","Start a character first.");
  else response(id,deluxe_item_rules());
 }
 else if(streq(method,"item.preferences") || streq(method,"item.rules.clear")) {
  bool changed=false;
  if(!ready || active_prompt || !streq(phase,"playing")) { error(id,"busy","Return to normal play first."); goto done; }
  if(!streq(str(p,"revision"),revision_text)) { error(id,"stale_revision","State changed; choose again."); goto done; }
  if(streq(method,"item.rules.clear")) {
   cJSON *rules=deluxe_item_rules(),*r;
   cJSON_ArrayForEach(r,cJSON_GetObjectItem(rules,"rules")) if(streq(str(r,"id"),str(p,"rule"))) {
    deluxe_clear_rule(r); changed=true; break;
   }
   cJSON_Delete(rules);
  } else {
   struct object *obj=NULL; cJSON *r; int ix=0;
   cJSON_ArrayForEach(r,cJSON_GetObjectItem(snapshot,"items")) {
    ++ix; if(streq(str(r,"id"),str(p,"item")) && ix<(int)item_handle_count) obj=item_handles[ix];
   }
   if(!obj || !obj->known || !item_is_available(obj)) { error(id,"stale_handle","Select an available item."); goto done; }
   if(streq(str(p,"operation"),"ignore_item") && cJSON_IsBool(cJSON_GetObjectItem(p,"enabled"))) {
    if(cJSON_IsTrue(cJSON_GetObjectItem(p,"enabled"))) obj->known->notice|=OBJ_NOTICE_IGNORE;
    else obj->known->notice&=~OBJ_NOTICE_IGNORE;
    changed=true;
   } else if(streq(str(p,"operation"),"ignore_kind") && deluxe_kind_ignore_allowed(obj) && cJSON_IsBool(cJSON_GetObjectItem(p,"enabled"))) {
    if(cJSON_IsTrue(cJSON_GetObjectItem(p,"enabled"))) object_ignore_flavor_of(obj); else kind_ignore_clear(obj->kind);
    changed=true;
   } else if(streq(str(p,"operation"),"autoinscribe") && cJSON_IsString(cJSON_GetObjectItem(p,"text")) && strlen(str(p,"text"))<80) {
    if(*str(p,"text")) add_autoinscription(obj->kind->kidx,str(p,"text"),object_flavor_is_aware(obj));
    else remove_autoinscription(obj->kind->kidx);
    autoinscribe_pack(player); autoinscribe_ground(player); changed=true;
   }
  }
  if(!changed) error(id,"invalid_argument","That item rule is not available.");
  else {
   player->upkeep->notice|=PN_IGNORE;
   player->upkeep->redraw|=PR_INVEN|PR_EQUIP|PR_ITEMLIST|PR_MAP;
   ready=false; Term_keypress(ESCAPE,0); response(id,cJSON_CreateObject());
  }
 }
 else if(streq(method,"item.compare")) {
  const struct object *obj=NULL; cJSON *record; int ix=0;
  if(!player || !player->race || !snapshot || (!ready && !(active_store && !store_busy)) || active_prompt) {
   error(id,"busy","Finish the current interaction first."); goto done;
  }
  if(!streq(str(p,"revision"),revision_text)) { error(id,"stale_revision","State changed."); goto done; }
  cJSON_ArrayForEach(record,cJSON_GetObjectItem(snapshot,"items")) {
   ++ix;
   if(streq(str(record,"id"),str(p,"item")) && ix<(int)item_handle_count) obj=item_handles[ix];
  }
  if(!obj) error(id,"stale_handle","Select a current item.");
  else {
   cJSON *out=cJSON_CreateObject();
   string(out,"item",str(p,"item")); string(out,"revision",revision_text);
   json_bool(out,"fully_known",object_fully_known(obj));
   cJSON_AddItemToObject(out,"options",deluxe_compare(obj)); response(id,out);
  }
 }
 else if (streq(method, "catalog.get")) {
  cJSON *out = cJSON_CreateObject(), *features = cJSON_CreateArray();
  if (initialized) for (i = 0; i < FEAT_MAX; ++i) {
   cJSON *f = cJSON_CreateObject(); number(f, "id", (int)i); string(f, "name", f_info[i].name);
   string(f,"map_kind",tf_has(f_info[i].flags,TF_UPSTAIR)?"up":tf_has(f_info[i].flags,TF_DOWNSTAIR)?"down":tf_has(f_info[i].flags,TF_SHOP)?"shop":tf_has(f_info[i].flags,TF_DOOR_ANY)?"door":tf_has(f_info[i].flags,TF_PASSABLE)?"floor":"wall");
   number(f, "glyph", f_info[i].d_char); number(f, "color", f_info[i].d_attr); cJSON_AddItemToArray(features, f);
  }
  cJSON_AddItemToObject(out, "features", features); response(id, out);
 } else if (streq(method,"birth.action") || streq(method,"birth.cancel")) {
  if(streq(method,"birth.cancel")) cJSON_AddStringToObject(p,"action","cancel");
  deluxe_birth_action(id,p);
 } else if (streq(method, "session.new") || streq(method, "session.load") || streq(method,"session.replay")) {
  const char *name = str(p, "save");
  if (launch_mode >= 0) { error(id, "wrong_phase", "Start another backend process for another game."); goto done; }
  if (!*name || strlen(name) > 64 || strspn(name, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != strlen(name)) {
   error(id, "invalid_argument", "Save name must use letters, numbers, hyphens or underscores."); goto done;
  }
  savefile_set_name(name, false, false);
  if ((streq(method, "session.new") && file_exists(savefile)) ||
      (!streq(method, "session.new") && !file_exists(savefile))) {
   error(id, "invalid_argument", "Save already exists for New, or is missing for Load."); goto done;
  }
  if(streq(method,"session.replay")) {
   const char *description=savefile_get_description(savefile);
   if(!description || !strstr(description,", dead (")) {
    error(id,"invalid_argument","Play Again requires a completed character save."); goto done;
   }
  }
  my_strcpy(birth_race,str(p,"race"),sizeof(birth_race));
  my_strcpy(birth_class,str(p,"class"),sizeof(birth_class));
  use_native_birth=cJSON_IsTrue(cJSON_GetObjectItem(p,"native_birth"));
  launch_mode = streq(method, "session.load") ? GAME_LOAD : GAME_NEW;
  response(id, cJSON_CreateObject());
 } else if (streq(method, "saves.rename") || streq(method, "saves.delete")) {
  const char *name = str(p, "save"), *new_name = str(p, "name");
  char source[sizeof(savefile)];
  bool rename_save = streq(method, "saves.rename");
  if (launch_mode >= 0) { error(id, "wrong_phase", "Manage saves from the main menu."); goto done; }
  if (!*name || strlen(name) > 64 || strspn(name, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != strlen(name) ||
      (rename_save && (!*new_name || strlen(new_name) > 64 || strspn(new_name, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != strlen(new_name)))) {
   error(id, "invalid_argument", "Save names must use letters, numbers, hyphens or underscores."); goto done;
  }
  savefile_set_name(name, false, false); my_strcpy(source, savefile, sizeof(source));
  if (!file_exists(source)) { error(id, "invalid_argument", "That save no longer exists."); goto done; }
  if (rename_save) {
   savefile_set_name(new_name, false, false);
   if (file_exists(savefile)) { error(id, "invalid_argument", "That save name is already in use."); goto done; }
   if (!file_move(source, savefile)) { error(id, "io_error", "Could not rename the save."); goto done; }
  } else if (!file_delete(source)) { error(id, "io_error", "Could not delete the save."); goto done; }
  response(id, cJSON_CreateObject());
 } else if (streq(method, "saves.list")) {
  savefile_getter g = NULL; cJSON *out = cJSON_CreateArray();
  while (got_savefile(&g)) {
   const struct savefile_details *d = get_savefile_details(g); cJSON *j = cJSON_CreateObject();
   string(j, "id", d->fnam); string(j, "description", d->desc); deluxe_save_summary(j,d->fnam,d->desc); cJSON_AddItemToArray(out, j);
  }
  cleanup_savefile_getter(g); response(id, out);
 } else if (!streq(str(p, "session_id"), "session-1")) error(id, "wrong_session", "Stale session.");
 else if(streq(method,"keybindings.get") || streq(method,"keybindings.set")) { deluxe_bindings_request(id,method,p); }
 else if (streq(method,"store.buy") || streq(method,"store.sell") || streq(method,"store.leave")) {
  deluxe_store_request(id, method, p);
 } else if (streq(method, "inspect.get")) {
  cJSON *v, *found = NULL;
  cJSON_ArrayForEach(v, cJSON_GetObjectItem(snapshot, "items")) if (streq(str(v, "id"), str(p, "handle"))) found = v;
  cJSON_ArrayForEach(v, cJSON_GetObjectItem(snapshot, "monsters")) if (streq(str(v, "id"), str(p, "handle"))) found = v;
  if (found) response(id, cJSON_Duplicate(found, true)); else error(id, "stale_handle", "Select from the current snapshot.");
 } else if (streq(method, "prompt.reply")) {
  cJSON *v = cJSON_GetObjectItem(p, "value"); const char *type = str(active_prompt, "type");
  cJSON *option; bool valid_choice = false;
  if (cJSON_IsString(v) && streq(type, "choice"))
   cJSON_ArrayForEach(option, cJSON_GetObjectItem(active_prompt, "choices"))
    if (streq(str(option, "id"), v->valuestring)) valid_choice = true;
  if (!active_prompt || !streq(str(p, "prompt_id"), str(active_prompt, "prompt_id"))) error(id, "stale_handle", "Prompt is no longer active.");
  else if (!v || !(valid_choice || cJSON_IsNull(v) || (streq(type, "confirmation") && cJSON_IsBool(v)) ||
   (streq(type, "text") && cJSON_IsString(v) && strlen(v->valuestring) <= (size_t)num(active_prompt, "maximum", 0)) ||
   (streq(type, "quantity") && cJSON_IsNumber(v) && v->valuedouble == v->valueint && v->valueint >= 0 && v->valueint <= num(active_prompt, "maximum", 0))))
   error(id, "invalid_argument", "Invalid prompt value.");
  else { reply_value = cJSON_Duplicate(v, true); response(id, cJSON_CreateObject()); }
 } else if (streq(method,"journal.get")) {
  if(!character_generated) error(id,"wrong_phase","Start a character first.");
  else response(id,deluxe_journal(false));
 } else if (streq(method,"targeting.blast")) {
  deluxe_blast_preview(id,p);
 } else if (streq(method,"dungeon.route")) {
  deluxe_route_preview(id,p);
 } else if (streq(method,"dungeon.terrain")) {
  cJSON *jx=cJSON_GetObjectItem(p,"x"), *jy=cJSON_GetObjectItem(p,"y");
  struct loc grid=loc(num(p,"x",-1),num(p,"y",-1));
  cmd_code command=deluxe_terrain_command(grid);
  if(active_prompt || !streq(str(p,"context"),context_text)) error(id,"stale_revision","Input context changed.");
  else if(!ready || !character_generated || !streq(phase,"playing") || screen_save_depth || !cJSON_GetObjectItem(snapshot,"dungeon")) error(id,"busy","Return to normal play first.");
  else if(!cJSON_IsNumber(jx) || !cJSON_IsNumber(jy) || jx->valuedouble!=jx->valueint || jy->valuedouble!=jy->valueint || command==CMD_NULL || !streq(str(p,"action"),deluxe_terrain_action(command))) error(id,"invalid_argument","That action is not available on this terrain.");
  else {
   travel_command=command; travel_grid=grid; travel_level=deluxe_level; travel_stage=TRAVEL_START;
   deluxe_travel_begin(grid,travel_command);
   ready=false; Term_keypress(ESCAPE,0); response(id,cJSON_CreateObject());
  }
 } else if (streq(method,"dungeon.pickup")) {
  cJSON *jx=cJSON_GetObjectItem(p,"x"), *jy=cJSON_GetObjectItem(p,"y");
  struct loc grid=loc(num(p,"x",-1),num(p,"y",-1));
  if(active_prompt || !streq(str(p,"context"),context_text)) error(id,"stale_revision","Input context changed.");
  else if(!ready || !character_generated || !streq(phase,"playing") || screen_save_depth || !cJSON_GetObjectItem(snapshot,"dungeon"))
   error(id,"busy","Return to normal play before picking up.");
  else if(!cJSON_IsNumber(jx) || !cJSON_IsNumber(jy) || jx->valuedouble!=jx->valueint || jy->valuedouble!=jy->valueint || !deluxe_pickup_observed(grid))
   error(id,"invalid_argument","No observed item at that location.");
  else {
   travel_command=CMD_PICKUP; travel_grid=grid; travel_level=deluxe_level; travel_stage=TRAVEL_START;
   deluxe_travel_begin(grid,travel_command);
   ready=false; Term_keypress(ESCAPE,0); response(id,cJSON_CreateObject());
  }
 } else if (streq(method,"dungeon.click")) {
  bool exit_look=cJSON_IsTrue(cJSON_GetObjectItem(p,"exit_look")) && target_ui_current && (target_ui_current->mode&TARGET_LOOK);
  cJSON *jx=cJSON_GetObjectItem(p,"x"), *jy=cJSON_GetObjectItem(p,"y");
  struct loc grid=loc(num(p,"x",-1),num(p,"y",-1));
  if(active_prompt || !streq(str(p,"context"),context_text)) error(id,"stale_revision","Input context changed.");
  else if((!ready && !exit_look) || !character_generated || !streq(phase,"playing") || screen_save_depth || !cJSON_GetObjectItem(snapshot,"dungeon") || textui_message_pending)
   error(id,"busy","Return to normal play before moving.");
  else if(!cJSON_IsNumber(jx) || !cJSON_IsNumber(jy) || jx->valuedouble!=jx->valueint || jy->valuedouble!=jy->valueint || !square_in_bounds_fully(cave,grid) ||
          grid.x<terminal.offset_x || grid.y<terminal.offset_y || grid.x>=terminal.offset_x+SCREEN_WID || grid.y>=terminal.offset_y+SCREEN_HGT)
   error(id,"invalid_argument","Select an interior tile in the current viewport.");
  else if(!OPT(player,mouse_movement)) error(id,"invalid_argument","Mouse movement is disabled in Angband's interface options.");
  else {
   int mods=(cJSON_IsTrue(cJSON_GetObjectItem(p,"shift"))?KC_MOD_SHIFT:0) |
    (cJSON_IsTrue(cJSON_GetObjectItem(p,"control"))?KC_MOD_CONTROL:0) |
    (cJSON_IsTrue(cJSON_GetObjectItem(p,"alt"))?KC_MOD_ALT:0);
   if(!mods && !loc_eq(player->grid,grid)) deluxe_travel_begin(grid,CMD_WALK);
   if(exit_look) {
    click_after_look=true; look_click_grid=grid; look_click_mods=mods;
    Term_keypress(ESCAPE,0);
   } else Term_mousepress(COL_MAP+(grid.x-terminal.offset_x)*tile_width,ROW_MAP+(grid.y-terminal.offset_y)*tile_height,(char)(1|(mods<<4)));
   response(id,cJSON_CreateObject());
  }
 } else if (streq(method, "targeting.set") || streq(method, "targeting.begin") || streq(method, "targeting.select") || streq(method, "targeting.control")) {
  const char *operation=str(p,"operation");
  bool immediate=streq(method,"targeting.set");
  bool begin=immediate || streq(method,"targeting.begin"), select=streq(method,"targeting.select");
  cJSON *jx=cJSON_GetObjectItem(p,"x"), *jy=cJSON_GetObjectItem(p,"y");
  struct loc grid=loc(num(p,"x",-1),num(p,"y",-1));
  if(active_prompt || !streq(str(p,"context"),context_text)) error(id,"stale_revision","Input context changed.");
  else if(!character_generated || !streq(phase,"playing") || screen_save_depth || !cJSON_GetObjectItem(snapshot,"dungeon"))
   error(id,"busy","Return to the dungeon view first.");
  else if(begin && (!ready || target_ui_current || textui_aiming || textui_direction)) error(id,"busy","Finish the current interaction first.");
  else if(!begin && !target_ui_current && !textui_aiming && !textui_direction) error(id,"busy","No targeting interaction is active.");
  else if((immediate || select || jx || jy) && (!cJSON_IsNumber(jx) || !cJSON_IsNumber(jy) || jx->valuedouble!=jx->valueint || jy->valuedouble!=jy->valueint || !square_in_bounds_fully(cave,grid)))
   error(id,"invalid_argument","Select an interior dungeon tile.");
  else if(begin) {
   const char *mode=immediate?"target":str(p,"mode");
   if(!streq(mode,"look") && !streq(mode,"target")) error(id,"invalid_argument","Unknown targeting mode.");
   else {
    native_target_mode=streq(mode,"look")?TARGET_LOOK:TARGET_KILL;
    native_target_immediate=immediate;
    native_target_grid=jx?grid:loc(-1,-1);
    ready=false;
    /* Wake the command boundary without interpreting a rebindable play key. */
    Term_keypress(ESCAPE,0); response(id,cJSON_CreateObject());
   }
  } else if(select) {
   bool confirm=cJSON_IsTrue(cJSON_GetObjectItem(p,"confirm"));
   if(target_ui_current) { target_ui_select(grid,confirm); response(id,cJSON_CreateObject()); }
   else {
    /* The engine's aim-direction handler already accepts a mouse location. */
    if(grid.x<terminal.offset_x || grid.y<terminal.offset_y || grid.x>=terminal.offset_x+SCREEN_WID || grid.y>=terminal.offset_y+SCREEN_HGT)
     error(id,"invalid_argument","Select a tile in the current viewport.");
    else if(textui_aiming && confirm) {
     struct monster *mon=square_monster(cave,grid);
     if(target_able(mon)) target_set_monster(mon);
     else target_set_location(grid.y,grid.x);
     /* Resume the original aim handler with its ordinary use-target input. */
     Term_keypress('5',0); response(id,cJSON_CreateObject());
    } else { Term_mousepress(COL_MAP+(grid.x-terminal.offset_x)*tile_width,ROW_MAP+(grid.y-terminal.offset_y)*tile_height,1); response(id,cJSON_CreateObject()); }
   }
  } else {
   int key=streq(operation,"cancel")?ESCAPE:streq(operation,"next")?'+':streq(operation,"previous")?'-':
    streq(operation,"free")?'o':streq(operation,"interesting")?'m':streq(operation,"player")?'p':
    streq(operation,"confirm")?'t':streq(operation,"recall")?'r':streq(operation,"target")?'*':0;
   if(!key) error(id,"invalid_argument","Unknown targeting operation.");
   else if(textui_direction && key!=ESCAPE) error(id,"invalid_argument","Choose a direction or cancel.");
   else if(textui_aiming && key!=ESCAPE && key!='*') error(id,"invalid_argument","Choose a direction or enter targeting first.");
   else if(key=='t' && !target_ui_current->can_confirm && !deluxe_look_location()) error(id,"invalid_argument","This selection cannot be confirmed.");
   else { deluxe_target_key(key); response(id,cJSON_CreateObject()); }
  }
 } else if (streq(method,"rest.cancel")) {
  if(active_prompt || !streq(str(p,"context"),context_text)) error(id,"stale_revision","Input context changed.");
  else {
   if(player && player_is_resting(player)) { disturb(player); msg("Cancelled."); }
   response(id,cJSON_CreateObject());
  }
 } else if (streq(method, "terminal.input")) {
  if(birth_active) { error(id,"busy","Use character creation controls."); goto done; }
  int key = num(p, "key", -1);
  const char *name = str(p, "key");
  if (streq(name, "enter")) key = KC_ENTER;
  else if (streq(name, "escape")) key = ESCAPE;
  else if (streq(name, "backspace")) key = KC_BACKSPACE;
  else if (streq(name, "tab")) key = KC_TAB;
  else if (streq(name, "up")) key = ARROW_UP;
  else if (streq(name, "down")) key = ARROW_DOWN;
  else if (streq(name, "left")) key = ARROW_LEFT;
  else if (streq(name, "right")) key = ARROW_RIGHT;
  if (active_prompt || !streq(str(p, "context"), context_text)) error(id, "stale_revision", "Input context changed.");
  else if (key < 1 || key > 0x10ffff) error(id, "invalid_argument", "Invalid key.");
  else if (active_store && !store_busy) {
   if (key == ESCAPE) { store_operation = STORE_LEAVE; response(id, cJSON_CreateObject()); }
   else error(id, "busy", "Use the store interaction controls.");
  }
  else { if(travel_display_active) deluxe_travel_feedback("Travel cancelled by input",false,true); travel_stage=TRAVEL_IDLE; deluxe_target_key(key); response(id, cJSON_CreateObject()); }
 } else if (streq(method, "command.execute")) {
  if (!ready || active_prompt) error(id, "busy", "Finish the current prompt first.");
  else if (!streq(str(p, "revision"), revision_text)) error(id, "stale_revision", "State changed; choose again.");
  else {
   for (i = 0; i < N_ELEMENTS(commands); ++i) if (streq(str(p, "command"), commands[i].id)) break;
   if (i == N_ELEMENTS(commands)) error(id, "invalid_argument", "Unknown command.");
   else {
    cJSON *out = cJSON_CreateObject();
    const char *item = str(p, "item");
    pending_item = NULL; pending_spell = -1;
    if (*item) {
     cJSON *record; int ix = 0;
     cJSON_ArrayForEach(record, cJSON_GetObjectItem(snapshot, "items")) {
      ++ix;
      if (streq(str(record, "id"), item) && ix < (int)item_handle_count) pending_item = item_handles[ix];
     }
     if (!pending_item) { cJSON_Delete(out); error(id, "stale_handle", "Select a current item."); goto done; }
    }
    if(*str(p,"spell")) {
     bool study=streq(commands[i].id,"core.study");
     if((!study && !streq(commands[i].id,"core.cast")) ||
        (pending_spell=deluxe_requested_spell(pending_item,str(p,"spell"),study))<0) {
      pending_item=NULL; cJSON_Delete(out); error(id,"invalid_argument","That spell is not available from this book for this action."); goto done;
     }
    }
    if(streq(commands[i].id,"core.pickup") && pending_item) {
     if(object_is_carried(player,pending_item) || !loc_eq(player->grid,pending_item->grid) ||
        tval_is_money(pending_item) || ignore_item_ok(player,pending_item) || !inven_carry_okay(pending_item)) {
      pending_item=NULL; cJSON_Delete(out); error(id,"invalid_argument","That item cannot be picked up from here."); goto done;
     }
     pickup_single=true;
    }
    my_strcpy(action, id, sizeof(action)); string(out, "action_id", action);
    /* Route through ordinary text UI prerequisites, inscription checks and queue. */
    {
     int key = commands[i].key;
     cmd_code code = cmd_lookup(key, KEYMAP_MODE_ORIG);
     if (code != CMD_NULL) key = cmd_lookup_key(code, OPT(player, rogue_like_commands) ? KEYMAP_MODE_ROGUE : KEYMAP_MODE_ORIG);
     else if (key == 'l' && OPT(player, rogue_like_commands)) key = 'x';
     /* The engine's explicit bypass preserves semantic commands under user keymaps. */
     if(pickup_single) Term_keypress(ESCAPE,0);
     else { Term_keypress('\\', 0); Term_keypress(key, 0); }
    }
    response(id, out);
   }
  }
 } else if (streq(method, "debug.quit")) {
  /* Deliberately bypass close_game(), which saves a living character.
   * Loading opens the save read-only; leave that existing file untouched. */
  response(id,cJSON_CreateObject()); closing=true; exit(0);
 } else if(streq(method,"run.finish")) {
  if(!run_report || !streq(phase,"dead") || active_prompt || run_finishing)
   error(id,"wrong_phase","The final run summary is not awaiting completion.");
  else {
   run_finishing=true; response(id,cJSON_CreateObject());
   /* Leave the existing death menu normally: close_game still saves the dead
    * character and performs all engine cleanup. Never bypass it with exit(). */
   Term_keypress(KTRL('X'),0);
  }
 } else if(streq(method,"options.get")) {
  if(!character_generated || player->is_dead) error(id,"wrong_phase","Start a living character first.");
  else response(id,deluxe_options());
 } else if(streq(method,"options.set")) {
  if(!ready || active_prompt || !character_generated || player->is_dead || !streq(phase,"playing"))
   error(id,"busy","Return to normal play before changing Angband options.");
  else if(!streq(str(p,"context"),context_text)) error(id,"stale_revision","Input context changed. Reopen Settings and try again.");
  else if(!deluxe_options_apply(cJSON_GetObjectItem(p,"values")))
   error(id,"invalid_argument","Invalid option, value or range. No options were changed.");
  else {
   options_redraw=true; ready=false;
   response(id,deluxe_options()); Term_keypress(ESCAPE,0);
  }
 } else if(streq(method,"debug.status.list")) {
  if(!character_generated) error(id,"wrong_phase","Start a character first.");
  else response(id,deluxe_debug_status_catalog());
 } else if(streq(method,"debug.status")) {
  const int idx=timed_name_to_idx(str(p,"effect"));
  const cJSON *amount=cJSON_GetObjectItem(p,"amount");
  if(!ready || active_prompt || !character_generated || player->is_dead || !streq(phase,"playing"))
   error(id,"busy","Return to normal play before setting a status effect.");
  else if(!deluxe_debug_status_allowed(idx)) error(id,"invalid_argument","Unknown or unsupported standalone status effect.");
  else if(!cJSON_IsNumber(amount) || amount->valuedouble!=amount->valueint || amount->valueint<0 || amount->valueint>30000)
   error(id,"invalid_argument","Amount must be a whole number from 0 to 30000.");
  else {
   debug_status=idx; debug_status_amount=amount->valueint; ready=false;
   response(id,cJSON_CreateObject()); Term_keypress(ESCAPE,0);
  }
 } else if (streq(method, "debug.glow_items")) {
  const char *type=str(p,"kind");
  int kind=streq(type,"artifact")?1:streq(type,"rune")?2:streq(type,"cursed")?3:0;
  const cJSON *jx=cJSON_GetObjectItem(p,"x"),*jy=cJSON_GetObjectItem(p,"y");
  struct loc grid=loc(num(p,"x",-1),num(p,"y",-1));
  if(!ready || active_prompt || !character_generated || player->is_dead || !streq(phase,"playing"))
   error(id,"busy","Return to normal play before spawning items.");
  else if((cJSON_GetObjectItem(p,"kind") || jx || jy) && (!kind || !cJSON_IsNumber(jx) || !cJSON_IsNumber(jy) ||
    jx->valuedouble!=jx->valueint || jy->valuedouble!=jy->valueint || !square_in_bounds_fully(cave,grid) ||
    !square_isseen(cave,grid) || !square_isfloor(cave,grid) || square_object(cave,grid) || square_monster(cave,grid) || square_isplayer(cave,grid)))
   error(id,"invalid_argument","Choose an empty, visible floor tile and an artifact, rune or cursed item.");
  else { debug_glow_kind=kind; debug_glow_grid=grid; debug_glow_items=true; ready=false; response(id,cJSON_CreateObject()); Term_keypress(ESCAPE,0); }
 } else if (streq(method, "debug.experience")) {
  cJSON *amount=cJSON_GetObjectItem(p,"amount");
  if(!ready || active_prompt || !character_generated || player->is_dead || !streq(phase,"playing"))
   error(id,"busy","Return to normal play before giving experience.");
  else if(!cJSON_IsNumber(amount) || amount->valuedouble!=amount->valueint || amount->valueint<1 || amount->valueint>PY_MAX_EXP)
   error(id,"invalid_argument","Experience must be a whole number from 1 to 99999999.");
  else {
   debug_experience=amount->valueint; ready=false;
   response(id,cJSON_CreateObject()); Term_keypress(ESCAPE,0);
  }
 } else if (streq(method, "debug.breath")) {
  const char *element=str(p,"element");
  int type=-1;
  if(streq(element,"FIRE")) type=PROJ_FIRE;
  else if(streq(element,"COLD")) type=PROJ_COLD;
  else if(streq(element,"ELEC")) type=PROJ_ELEC;
  else if(streq(element,"ACID")) type=PROJ_ACID;
  else if(streq(element,"POIS")) type=PROJ_POIS;
  if(!ready || active_prompt || !character_generated || player->is_dead || !streq(phase,"playing"))
   error(id,"busy","Return to normal play before breathing.");
  else if(type<0) error(id,"invalid_argument","Choose Fire, Frost, Lightning, Acid or Poison.");
  else {
   debug_breath_element=type; ready=false;
   response(id,cJSON_CreateObject()); Term_keypress(ESCAPE,0);
  }
 } else if (streq(method, "debug.blink")) {
  if(!ready || active_prompt || !character_generated || player->is_dead || !streq(phase,"playing"))
   error(id,"busy","Return to normal play before casting Blink.");
  else {
   debug_blink=true; ready=false;
   response(id,cJSON_CreateObject()); Term_keypress(ESCAPE,0);
  }
 } else if (streq(method, "debug.blast")) {
  const cJSON *radius=cJSON_GetObjectItem(p,"radius");
  if(!ready || active_prompt || !character_generated || player->is_dead || !streq(phase,"playing"))
   error(id,"busy","Return to normal play before casting a test spell.");
  else if(!cJSON_IsNumber(radius) || radius->valuedouble!=radius->valueint || radius->valueint<1 || radius->valueint>MIN(20,z_info->max_range))
   error(id,"invalid_argument","Radius must be a whole number from 1 to 20 (within engine range).");
  else {
   debug_blast_radius=radius->valueint; ready=false;
   response(id,cJSON_CreateObject()); Term_keypress(ESCAPE,0);
  }
 } else if (streq(method, "debug.damage")) {
  cJSON *amount = cJSON_GetObjectItem(p, "amount");
  if (!ready || active_prompt || !character_generated || player->is_dead || !streq(phase, "playing"))
   error(id, "busy", "Return to normal play before inflicting damage.");
  else if (!cJSON_IsNumber(amount) || amount->valuedouble != amount->valueint || amount->valueint < 1 || amount->valueint > 30000)
   error(id, "invalid_argument", "Damage must be a whole number from 1 to 30000.");
  else {
   debug_damage = amount->valueint; ready = false;
   response(id, cJSON_CreateObject()); Term_keypress(ESCAPE, 0);
  }
 } else if (streq(method, "session.save") || streq(method, "session.close")) {
  if (!ready || active_prompt || !character_generated) error(id, "busy", "Return to normal play before saving.");
  else if (!save_game_checked()) error(id, "io_error", "The engine could not save. Your game remains open.");
  else { response(id, cJSON_CreateObject()); if (streq(method, "session.close")) { closing = true; exit(0); } }
 } else error(id, "unsupported_capability", "Operation is not implemented in protocol 0.1.");
done:
 cJSON_Delete(r);
}

static void combat_feedback(game_event_type type, game_event_data *data, void *user)
{
 cJSON *v;
 if(!data || !data->combat.visible || (!data->combat.player && player->timed[TMD_IMAGE])) return;
 if(!streq(data->combat.kind,"miss") && data->combat.amount<=0) return;
 v=cJSON_CreateObject(); string(v,"kind",data->combat.kind); number(v,"amount",data->combat.amount);
 number(v,"x",data->combat.grid.x); number(v,"y",data->combat.grid.y);
 json_bool(v,"player",data->combat.player); counter(v,"level_id",deluxe_level);
 event("combat.feedback",v);
}
static void rest_activity(game_event_type type, game_event_data *data, void *user)
{
 static bool reported;
 bool resting=player && player->upkeep && player_is_resting(player);
 if(resting!=reported) {
  cJSON *activity=cJSON_CreateObject(); json_bool(activity,"resting",resting);
  event("activity.changed",activity); reported=resting;
 }
 /* Poll only during rest. Never serialize full snapshots in this loop. */
 if(resting && input_available()) pump();
}
static errr xtra(int n, int v)
{
 if (n == TERM_XTRA_EVENT) {
  if (!v) { if (input_available()) pump(); return 0; }
  deluxe_travel_finish();
  travel_stage=TRAVEL_IDLE; /* Never resume an intent after a user-input prompt. */
  publish(); if (ready) complete();
  /* Native post-mortems already own the message log. Do not leave cleanup
   * waiting on a terminal -more- after the user has finished the death menu.
   * Confirmation prompts still go through their normal semantic hook. */
  if(run_finishing && textui_message_pending && !active_prompt) Term_keypress(' ',0);
  while (terminal.key_head == terminal.key_tail && connected) pump();
  ready = false;
 }
 return 0;
}
static errr text_hook(int x, int y, int n, int a, const wchar_t *s) { return 0; }
static errr wipe_hook(int x, int y, int n) { return 0; }
static errr cursor_hook(int x, int y) { return 0; }
static bool deluxe_equipment_browser(void)
{
 if(!native_equipment || !streq(phase,"playing")) return false;
 event("equipment.open",cJSON_CreateObject());
 return true;
}
static bool deluxe_inventory_browser(void)
{
 if(!native_inventory || !streq(phase,"playing")) return false;
 event("inventory.open",cJSON_CreateObject());
 return true;
}
static errr get_command(cmd_context context)
{
 if (context != CTX_GAME) return textui_get_cmd(context);
 /* Short pickup trips may finish between running's interrupt polls. Honor
  * pending input before advancing any stage, especially the final pickup. */
 if(travel_stage!=TRAVEL_IDLE && input_available()) { ready=false; pump(); }
 phase = "playing"; ready = true;
 if(deluxe_travel_continue()) { ready=false; return 0; }
 if(click_after_look) {
  ui_event click={0}; click.type=EVT_MOUSE; click.mouse.button=1; click.mouse.mods=look_click_mods;
  click_after_look=false; ready=false;
  textui_process_click_at(click,look_click_grid);
  return 0;
 }
 {
  errr result = textui_get_cmd(context);
  if(pickup_single) {
   struct object *obj=pending_item;
   const int key=cmd_lookup_key(CMD_PICKUP,OPT(player,rogue_like_commands)?KEYMAP_MODE_ROGUE:KEYMAP_MODE_ORIG);
   pickup_single=false; pending_item=NULL; ready=false;
   /* Explicit selection uses the engine's single-item pickup path. Its normal
    * capacity, quantity, weight, pickup messaging and energy rules still apply. */
   if(obj && loc_eq(player->grid,obj->grid) && inven_carry_okay(obj) &&
      key_confirm_command(key) && get_item_allow(obj,key,CMD_PICKUP,false)) {
    cmdq_push(CMD_PICKUP); cmd_set_arg_item(cmdq_peek(),"item",obj);
   }
  }
  if(native_target_mode) {
   const int mode=native_target_mode; native_target_mode=0;
   if(native_target_immediate) {
    struct monster *mon=square_monster(cave,native_target_grid);
    native_target_immediate=false;
    if(target_able(mon)) target_set_monster(mon);
    else target_set_location(native_target_grid.y,native_target_grid.x);
    msg("Target Selected.");
   }
   else if(target_set_interactive(mode,native_target_grid.x,native_target_grid.y,true)) msg("Target Selected.");
   else if(mode&TARGET_KILL) msg("Target Aborted.");
  }
  if(options_redraw) { options_redraw=false; ready=false; do_cmd_redraw(); }
  if(debug_status>=0) {
   int idx=debug_status,amount=debug_status_amount; debug_status=-1; ready=false;
   player_set_timed(player,idx,amount,true,true);
  }
  if(debug_glow_items) { debug_glow_items=false; ready=false; deluxe_spawn_glow_items(); }
  if(debug_experience) {
   int amount=debug_experience; debug_experience=0; ready=false;
   player_exp_gain(player,amount);
  }
  if (debug_breath_element >= 0) {
   int element=debug_breath_element; debug_breath_element=-1; ready=false;
   deluxe_debug_projection(EF_BREATH, element, 12, 60);
  }
  if (debug_blink) {
   debug_blink=false; ready=false;
   effect_simple(EF_TELEPORT,source_player(),"10",0,0,0,0,0,NULL);
  }
  if (debug_blast_radius) {
   int radius=debug_blast_radius; debug_blast_radius=0; ready=false;
   deluxe_debug_projection(EF_BALL, PROJ_FIRE, radius, 0);
  }
  if (debug_damage) {
   int damage = debug_damage; debug_damage = 0; ready = false;
   take_hit(player, damage, "Deluxe developer tools");
  }
  return result;
 }
}
static void lifecycle(game_event_type type, game_event_data *data, void *user)
{
 if (type == EVENT_ENTER_BIRTH) phase = "birth";
 /* Loading may pause on a study reminder before the first game command.
  * World entry, not command readiness, establishes the gameplay layout. */
 else if (type == EVENT_ENTER_WORLD) { phase = "playing"; deluxe_bindings_init(); }
 else if (type == EVENT_ENTER_STORE) phase = "store";
 else if (type == EVENT_LEAVE_STORE) phase = "playing";
 else if (type == EVENT_ENTER_DEATH) phase = "dead";
}
static void log_hook(const char *message) { fprintf(stderr, "%s\n", message); }

int main(int argc, char **argv)
{
 const char *data = "lib", *user = "deluxe-user"; int i;
#ifdef WINDOWS
 setlocale(LC_ALL, ".UTF8");
#else
 setlocale(LC_ALL, "");
#endif
 setvbuf(stdin, NULL, _IONBF, 0);
 for (i = 1; i + 1 < argc; i += 2) {
  if (streq(argv[i], "--data-dir")) data = argv[i + 1];
  else if (streq(argv[i], "--user-dir")) user = argv[i + 1];
  else { fprintf(stderr, "Unknown option\n"); return 2; }
 }
 plog_aux = log_hook; ANGBAND_SYS = "deluxe";
 init_file_paths(data, data, user); create_needed_dirs();
 term_init(&terminal, SCREEN_W, SCREEN_H, 256);
 terminal.xtra_hook = xtra; terminal.text_hook = text_hook;
 terminal.wipe_hook = wipe_hook; terminal.curs_hook = cursor_hook;
 terminal.never_bored = true; terminal.never_frosh = true;
 Term_activate(&terminal); angband_term[0] = &terminal;
 while (launch_mode < 0) pump();
 init_display(); init_angband(); textui_init(); initialized = true;
 if(use_native_birth) birth_interact_hook=deluxe_birth_session;
 get_check_hook = check_hook; get_string_hook = string_hook; get_quantity_hook = quantity_hook;
 map_visual_hook = deluxe_observe_cell; map_visual_reset_hook = deluxe_reset_view;
 original_get_item = get_item_hook; get_item_hook = item_hook;
 get_spell_hook = deluxe_get_spell; get_spell_from_book_hook = deluxe_spell_choose;
 book_browse_hook = deluxe_browse_book;
 inventory_browse_hook = deluxe_inventory_browser;
 equipment_browse_hook = deluxe_equipment_browser;
 store_interact_hook = deluxe_store_session;
 cmd_get_hook = get_command;
 sound_event_hook=deluxe_sound_event;
 target_selected_hook=deluxe_target_selected;
 event_add_handler(EVENT_COMBAT_FEEDBACK, combat_feedback, NULL);
 event_add_handler(EVENT_ACTOR_MOTION, deluxe_motion_event, NULL);
 event_add_handler(EVENT_BOLT, deluxe_projectile, NULL);
 event_add_handler(EVENT_MISSILE, deluxe_projectile, NULL);
 event_add_handler(EVENT_EXPLOSION, deluxe_projectile, NULL);
 event_add_handler(EVENT_CHECK_INTERRUPT, rest_activity, NULL);
 event_add_handler(EVENT_ENTER_BIRTH, lifecycle, NULL);
 event_add_handler(EVENT_ENTER_WORLD, lifecycle, NULL);
 event_add_handler(EVENT_ENTER_STORE, lifecycle, NULL);
 event_add_handler(EVENT_LEAVE_STORE, lifecycle, NULL);
 event_add_handler(EVENT_ENTER_DEATH, lifecycle, NULL);
 event_add_handler(EVENT_INPUT_FLUSH,deluxe_travel_event,NULL);
 event_add_handler(EVENT_AUTOPICKUP_BEGIN,deluxe_travel_event,NULL);
 event_add_handler(EVENT_AUTOPICKUP_END,deluxe_travel_event,NULL);
 event_add_handler(EVENT_NEW_LEVEL_DISPLAY,deluxe_travel_event,NULL);
 event_add_handler(EVENT_ENTER_STORE,deluxe_travel_event,NULL);
 event_add_handler(EVENT_ENTER_DEATH,deluxe_travel_event,NULL);
 play_game((enum game_mode_type)launch_mode);
 phase = "finished"; ready = false; publish(); complete();
 sound_event_hook=NULL; target_selected_hook=NULL;
 textui_cleanup(); cleanup_angband();
 deluxe_reset_view();
 return 0;
}
