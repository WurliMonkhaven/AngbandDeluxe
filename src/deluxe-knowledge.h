/* Read-only, on-demand knowledge records. Engine recall owns descriptions. */
#include "mon-lore.h"
#include "obj-make.h"

static bool deluxe_knowledge_visible(const char *category,int i)
{
 if(streq(category,"creatures")) return i>0 && i<z_info->r_max && r_info[i].name && (get_lore(&r_info[i])->sights || get_lore(&r_info[i])->all_known);
 if(streq(category,"items")) return i>=0 && i<z_info->k_max && k_info[i].name && !kf_has(k_info[i].kind_flags,KF_INSTA_ART) && (k_info[i].everseen || k_info[i].flavor || OPT(player,cheat_xtra));
 if(streq(category,"artifacts")) return i>=0 && i<z_info->a_max && a_info[i].name && (is_artifact_seen(&a_info[i]) || player->wizard || OPT(player,cheat_xtra));
 if(streq(category,"terrain")) return i>=0 && i<FEAT_MAX && f_info[i].name && !f_info[i].mimic;
 return false;
}
static cJSON *deluxe_knowledge_entry(const char *category,int i)
{
 cJSON *row=cJSON_CreateObject(); char name[256]; const char *group=""; int attr=COLOUR_WHITE;
 number(row,"id",i); json_bool(row,"known",!streq(category,"items") || !k_info[i].flavor || k_info[i].aware);
 if(streq(category,"creatures")) {
  struct monster_race *race=&r_info[i]; my_strcpy(name,race->name,sizeof(name));
  group=rf_has(race->flags,RF_UNIQUE)?"Unique creatures":race->base->name; attr=race->d_attr;
 } else if(streq(category,"items")) {
  /* Name the complete type, not just its flavour. Copy the kind because
   * object_desc marks aware kinds as everseen even for a recall template. */
  struct object_kind kind=k_info[i];
  struct object obj=OBJECT_NULL,known=OBJECT_NULL;
  obj.kind=&kind; obj.tval=kind.tval; obj.sval=kind.sval; obj.number=1;
  known=obj; obj.known=&known;
  object_desc(name,sizeof(name),&obj,ODESC_BASE|ODESC_SINGULAR,player);
  group=tval_find_name(kind.tval); attr=object_kind_attr(&kind);
  if(attr==COLOUR_DARK) attr=COLOUR_WHITE;
 } else if(streq(category,"artifacts")) {
  my_strcpy(name,a_info[i].name,sizeof(name)); group=tval_find_name(a_info[i].tval); attr=COLOUR_VIOLET;
 } else {
  struct feature *feat=&f_info[i]; my_strcpy(name,feat->name,sizeof(name)); attr=feat->d_attr;
  group=feat->shopnum?"Shops":tf_has(feat->flags,TF_PASSABLE)?"Passable terrain":"Obstacles";
 }
 my_strcap(name); string(row,"name",name); string(row,"group",group?group:"Other"); number(row,"color",attr);
 return row;
}
static void deluxe_knowledge_text(cJSON *out,const char *title,textblock *tb)
{
 const wchar_t *text=textblock_text(tb);
 inspection_section(out,title,title,text,wcslen(text)); textblock_free(tb);
}
static void deluxe_knowledge_value(cJSON *out,const char *label,int value)
{
 cJSON *row=cJSON_CreateObject(); string(row,"label",label); number(row,"value",value);
 cJSON_AddItemToArray(cJSON_GetObjectItem(out,"stats"),row);
}
static cJSON *deluxe_knowledge_detail(const char *category,int i)
{
 cJSON *out=deluxe_knowledge_entry(category,i); textblock *tb;
 uint32_t saved_state[RAND_DEG], saved_index=state_i,saved_value=Rand_value; bool saved_quick=Rand_quick;
 memcpy(saved_state,STATE,sizeof(saved_state));
 cJSON_AddItemToObject(out,"stats",cJSON_CreateArray());
 cJSON_AddItemToObject(out,"description_sections",cJSON_CreateArray());
 if(streq(category,"creatures")) {
  struct monster_race *race=&r_info[i]; const struct monster_lore *lore=get_lore(race); bitflag flags[RF_SIZE];
  monster_flags_known(race,lore,flags);
  deluxe_knowledge_value(out,"Sightings",lore->sights); deluxe_knowledge_value(out,"Kills this life",lore->pkills);
  deluxe_knowledge_value(out,"Total kills",lore->tkills);
  tb=textblock_new(); lore_append_flavor(tb,race); deluxe_knowledge_text(out,"Description",tb);
  tb=textblock_new(); lore_append_movement(tb,race,lore,flags); lore_append_awareness(tb,race,lore,flags); deluxe_knowledge_text(out,"Movement & awareness",tb);
  tb=textblock_new(); lore_append_toughness(tb,race,lore,flags); lore_append_attack(tb,race,lore,flags); deluxe_knowledge_text(out,"Combat",tb);
  tb=textblock_new(); lore_append_abilities(tb,race,lore,flags); lore_append_spells(tb,race,lore,flags); deluxe_knowledge_text(out,"Abilities & spells",tb);
  tb=textblock_new(); lore_append_drop(tb,race,lore,flags); lore_append_exp(tb,race,lore,flags); deluxe_knowledge_text(out,"Rewards",tb);
  tb=textblock_new(); lore_append_kills(tb,race,lore,flags); lore_append_friends(tb,race,lore,flags); deluxe_knowledge_text(out,"Encounters",tb);
 } else if(streq(category,"terrain")) {
  struct feature *feat=&f_info[i]; tb=textblock_new();
  textblock_append(tb,"%s",feat->desc?feat->desc:"No further description."); deluxe_knowledge_text(out,"Description",tb);
  tb=textblock_new(); textblock_append(tb,"%s",tf_has(feat->flags,TF_PASSABLE)?"You can walk across this terrain.":"This terrain blocks ordinary movement.");
  if(feat->shopnum) textblock_append(tb," This is a shop entrance.");
  deluxe_knowledge_text(out,"Traversal",tb);
 } else {
  struct object *obj=object_new(), *known=object_new();
  bool artifact=streq(category,"artifacts");
  if(artifact) make_fake_artifact(obj,&a_info[i]); else object_prep(obj,&k_info[i],0,EXTREMIFY);
  if(artifact || k_info[i].aware || !k_info[i].flavor) object_copy(known,obj);
  obj->known=known;
  tb=object_info_sections(obj,OINFO_FAKE,inspection_section,NULL,out); textblock_free(tb);
  object_delete(NULL,NULL,&known); object_delete(NULL,NULL,&obj);
 }
 memcpy(STATE,saved_state,sizeof(saved_state)); state_i=saved_index; Rand_value=saved_value; Rand_quick=saved_quick;
 return out;
}
static void deluxe_knowledge_request(const char *id,const char *method,const cJSON *p)
{
 const char *category=str(p,"category"); int i,max=0; cJSON *out,*entries;
 if(!character_generated || !streq(phase,"playing")) { error(id,"wrong_phase","Knowledge is available during play."); return; }
 if(streq(category,"creatures")) max=z_info->r_max;
 else if(streq(category,"items")) max=z_info->k_max;
 else if(streq(category,"artifacts")) max=z_info->a_max;
 else if(streq(category,"terrain")) max=FEAT_MAX;
 else { error(id,"invalid_argument","Unknown knowledge category."); return; }
 if(streq(method,"knowledge.get")) {
  const cJSON *entry=cJSON_GetObjectItem(p,"id"); i=num(p,"id",-1);
  if(!cJSON_IsNumber(entry) || entry->valuedouble!=i || !deluxe_knowledge_visible(category,i)) { error(id,"invalid_argument","Knowledge entry is unavailable."); return; }
  out=deluxe_knowledge_detail(category,i);
 } else {
  out=cJSON_CreateObject(); entries=cJSON_CreateArray();
  for(i=0;i<max;++i) if(deluxe_knowledge_visible(category,i)) cJSON_AddItemToArray(entries,deluxe_knowledge_entry(category,i));
  cJSON_AddItemToObject(out,"entries",entries);
 }
 string(out,"category",category); response(id,out);
}
