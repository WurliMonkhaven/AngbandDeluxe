/* Read-only character sheet data; no terminal scraping or client rules. */
static void deluxe_sheet_row(void *user, const char *group, const char *label, const char *value, int attr)
{
 cJSON *rows=user, *row=cJSON_CreateObject();
 string(row,"group",group); string(row,"label",label); string(row,"value",value); number(row,"color",attr);
 cJSON_AddItemToArray(rows,row);
}
static void deluxe_sheet_stat(cJSON *row, const char *key, int value)
{
 char text[32];
 if(value>18) strnfmt(text,sizeof(text),"18/%02d",value-18);
 else strnfmt(text,sizeof(text),"%d",value);
 string(row,key,text);
}
static void deluxe_character_sheet(cJSON *p)
{
 static const char *stats[]={"STR","INT","WIS","DEX","CON"};
 static const char *elements[]={"Acid","Lightning","Fire","Cold","Poison","Light","Dark","Sound","Shards","Nexus","Nether","Chaos","Disenchantment"};
 cJSON *sheet=cJSON_CreateObject(), *rows=cJSON_CreateArray(), *attributes=cJSON_CreateArray();
 cJSON *resists=cJSON_CreateArray(), *abilities=cJSON_CreateArray();
 struct player_ability *ability;
 int i;
 character_sheet_rows(deluxe_sheet_row,rows);
 string(sheet,"history",player->history?player->history:"");
 for(i=0;i<STAT_MAX;++i) {
  cJSON *row=cJSON_CreateObject();
  string(row,"label",stats[i]); deluxe_sheet_stat(row,"base",player->stat_max[i]);
  deluxe_sheet_stat(row,"current",player->state.stat_use[i]);
  deluxe_sheet_stat(row,"best",player->state.stat_top[i]);
  number(row,"race",player->race->r_adj[i]); number(row,"class",player->class->c_adj[i]);
  number(row,"equipment",player->state.stat_add[i]);
  json_bool(row,"sustained",of_has(player->known_state.flags,sustain_flag(i)));
  cJSON_AddItemToArray(attributes,row);
 }
 for(i=0;i<(int)N_ELEMENTS(elements);++i) {
  cJSON *row=cJSON_CreateObject();
  int level=player->known_state.el_info[i].res_level;
  string(row,"label",elements[i]); number(row,"level",level);
  string(row,"value",level<0?"Vulnerable":level==0?"Unprotected":level>=3?"Immune":level==2?"Double resistance":"Resistant");
  cJSON_AddItemToArray(resists,row);
 }
 for(i=1;i<OF_MAX;++i) {
  struct obj_property *prop=lookup_obj_property(OBJ_PROPERTY_FLAG,i);
  if(prop && of_has(player->known_state.flags,i)) {
   cJSON *row=cJSON_CreateObject();
   string(row,"label",prop->name); string(row,"description",prop->desc?prop->desc:"");
   cJSON_AddItemToArray(abilities,row);
  }
 }
 for(ability=player_abilities;ability;ability=ability->next) {
  if(streq(ability->type,"player") &&
    (class_has_ability(player->class,ability) || race_has_ability(player->race,ability))) {
   cJSON *row=cJSON_CreateObject();
   string(row,"label",ability->name); string(row,"description",ability->desc?ability->desc:"");
   cJSON_AddItemToArray(abilities,row);
  }
 }
 cJSON_AddItemToObject(sheet,"rows",rows); cJSON_AddItemToObject(sheet,"attributes",attributes);
 cJSON_AddItemToObject(sheet,"resistances",resists); cJSON_AddItemToObject(sheet,"abilities",abilities);
 cJSON_AddItemToObject(p,"character_sheet",sheet);
}
