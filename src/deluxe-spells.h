/* Spell presentation and input bridge. The original cast/study commands own
 * prerequisites, inscriptions, mana warnings, randomness and aiming. */
static int pending_spell = -1;
static bool spell_selection, spell_browsing;
static cJSON *prompt(const char *type, const char *text, int maximum, const char *initial);

static cJSON *deluxe_spell_record(int index, bool available)
{
 const struct class_spell *spell = spell_by_index(player,index);
 cJSON *record = cJSON_CreateObject();
 char id[40], info[256] = "";
 int flags = player->spell_flags[index];
 const char *status = spell->slevel >= 99 ? "Illegible" :
  (flags & PY_SPELL_FORGOTTEN) ? "Forgotten" :
  (flags & PY_SPELL_LEARNED) ? ((flags & PY_SPELL_WORKED) ? "Learned" : "Untried") :
  spell->slevel <= player->lev ? "Unknown" : "Difficult";
 strnfmt(id,sizeof(id),"spell-%d",index);
 string(record,"id",id); string(record,"label",spell->name);
 string(record,"description",spell->text); string(record,"status",status);
 number(record,"level",spell->slevel); number(record,"mana",spell->smana);
 number(record,"failure",spell_chance(index));
 json_bool(record,"can_cast",available && player_can_cast(player,false) && spell_okay_to_cast(player,index));
 json_bool(record,"can_study",available && player_can_study(player,false) && spell_okay_to_study(player,index));
 json_bool(record,"needs_aim",spell_needs_aim(index));
 json_bool(record,"low_mana",spell->smana > player->csp);
 if ((flags & PY_SPELL_WORKED) && !(flags & PY_SPELL_FORGOTTEN)) {
  /* get_spell_info evaluates dice expressions. Queries must not consume RNG. */
  uint32_t saved[RAND_DEG], saved_i=state_i, saved_value=Rand_value;
  bool quick=Rand_quick;
  memcpy(saved,STATE,sizeof(saved));
  get_spell_info(index,info,sizeof(info));
  memcpy(STATE,saved,sizeof(saved)); state_i=saved_i; Rand_value=saved_value; Rand_quick=quick;
 }
 string(record,"info",info);
 return record;
}

static void deluxe_book_record(cJSON *record, const struct object *obj)
{
 const struct class_book *book;
 bool available = item_is_available((struct object *)obj);
 cJSON *spells;
 int i;
 if (!player->spell_flags || !obj_can_browse(obj) ||
     (!available && !streq(str(record,"location"),"Store") && !streq(str(record,"location"),"Home"))) return;
 book=player_object_to_book(player,obj); spells=cJSON_CreateArray();
 for(i=0;i<book->num_spells;++i) cJSON_AddItemToArray(spells,deluxe_spell_record(book->spells[i].sidx,available));
 cJSON_AddItemToObject(record,"spells",spells);
 json_bool(record,"book_available",available);
 json_bool(record,"choose_spells",player_has(player,PF_CHOOSE_SPELLS));
}

/* Validate a direct selection against this exact current book. Never turn an
 * untrusted spell handle into an unchecked engine array index. */
static int deluxe_requested_spell(const struct object *obj, const char *id, bool study)
{
 const struct class_book *book;
 int i;
 if (!obj || !item_is_available((struct object *)obj) || !obj_can_browse(obj)) return -1;
 if (study ? (!player_can_study(player,false) || !player_has(player,PF_CHOOSE_SPELLS)) : !player_can_cast(player,false)) return -1;
 book=player_object_to_book(player,obj);
 for(i=0;i<book->num_spells;++i) {
  int index=book->spells[i].sidx; char handle[40];
  strnfmt(handle,sizeof(handle),"spell-%d",index);
  if(streq(handle,id) && (study ? spell_okay_to_study(player,index) : spell_okay_to_cast(player,index))) return index;
 }
 return -1;
}

static int deluxe_spell_choose(struct player *p, const char *verb, struct object *obj,
 const char *error_text, bool (*filter)(const struct player *,int))
{
 const struct class_book *book=player_object_to_book(p,obj);
 int i, count=0, result=-1;
 cJSON *answer;
 char text[256];
 if(!book) return -1;
 if(pending_spell>=0) {
  int wanted=pending_spell; pending_spell=-1;
  for(i=0;i<book->num_spells;++i)
   if(book->spells[i].sidx==wanted && filter(p,wanted)) return wanted;
  return -1;
 }
 next_choices=cJSON_CreateArray();
 for(i=0;i<book->num_spells;++i) {
  int index=book->spells[i].sidx; cJSON *choice;
  char id[32], key[2]={i<(int)strlen(all_letters_nohjkl)?all_letters_nohjkl[i]:0,0};
  if(!filter(p,index)) continue;
  choice=deluxe_spell_record(index,item_is_available(obj));
  strnfmt(id,sizeof(id),"%d",index);
  cJSON_ReplaceItemInObject(choice,"id",cJSON_CreateString(id));
  string(choice,"shortcut",key); cJSON_AddItemToArray(next_choices,choice); ++count;
 }
 if(!count) {
  cJSON_Delete(next_choices); next_choices=NULL;
  if(error_text) msg("%s",error_text);
  return -1;
 }
 track_object(p->upkeep,obj); handle_stuff(p);
 strnfmt(text,sizeof(text),"%s — %s",verb,book->realm->spell_noun);
 spell_selection=true;
 answer=prompt("choice",text,count,"");
 spell_selection=false;
 if(cJSON_IsString(answer)) result=atoi(answer->valuestring);
 cJSON_Delete(answer);
 return result;
}

static int deluxe_get_spell(struct player *p, const char *verb, item_tester book_filter,
 cmd_code cmd, const char *book_error, bool (*filter)(const struct player *,int),
 const char *spell_error, struct object **rtn_book)
{
 char text[128]; struct object *book;
 strnfmt(text,sizeof(text),"%s which book?",verb); my_strcap(text);
 if(!get_item(&book,text,book_error,cmd,book_filter,USE_INVEN|USE_FLOOR)) return -1;
 if(rtn_book) *rtn_book=book;
 return deluxe_spell_choose(p,verb,book,spell_error,filter);
}

static void deluxe_browse_book(const struct object *obj)
{
 spell_browsing=true;
 deluxe_spell_choose(player,"Browse",(struct object *)obj,"You cannot browse that.",spell_okay_to_browse);
 spell_browsing=false;
}
