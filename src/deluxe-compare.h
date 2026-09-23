/* On-demand, read-only hypothetical equipment comparison. Engine thread only. */
static void comparison_metric(cJSON *rows,const char *id,const char *label,int before,int after,int scale)
{
 cJSON *row=cJSON_CreateObject();
 string(row,"id",id); string(row,"label",label); number(row,"before",before);
 number(row,"after",after); number(row,"scale",scale); number(row,"delta",after-before);
 cJSON_AddItemToArray(rows,row);
}
static int comparison_damage(int *brands,int *slays)
{
 struct object *weapon=equipped_item_by_slot_name(player,"weapon");
 int normal=0;
 bool nonweapon=false;
 memset(brands,0,z_info->brand_max*sizeof(int)); memset(slays,0,z_info->slay_max*sizeof(int));
 if(!weapon || !weapon->known || !weapon->known->dd || !weapon->known->ds) return -1;
 if(OPT(player,birth_percent_damage)) o_obj_known_damage(weapon,&normal,brands,slays,&nonweapon,false);
 else obj_known_damage(weapon,&normal,brands,slays,&nonweapon,false);
 return normal;
}
static void comparison_change(cJSON *changes,const char *label,const char *before,const char *after)
{
 cJSON *row=cJSON_CreateObject();
 string(row,"label",label); string(row,"before",before); string(row,"after",after);
 cJSON_AddItemToArray(changes,row);
}
static const char *comparison_resistance(int value)
{
 return value<0?"Vulnerable":value==0?"Unprotected":value>=3?"Immune":value==2?"Double resistance":"Resistant";
}
static cJSON *deluxe_compare(const struct object *obj)
{
 static const char *stat_names[]={"STR","INT","WIS","DEX","CON"};
 static const char *elements[]={"Acid","Lightning","Fire","Cold","Poison","Light","Dark","Sound","Shards","Nexus","Nether","Chaos","Disenchantment"};
 cJSON *options=cJSON_CreateArray();
 struct player_state saved_state=player->state, saved_known=player->known_state, before={0};
 struct object **saved_slots;
 int *before_brands,*after_brands,*before_slays,*after_slays;
 int weight=player->upkeep->total_weight, slot=wield_slot(obj), n,i,damage_before;
 bool carried=object_is_carried(player,obj);
 uint32_t rng_state[RAND_DEG],rng_index=state_i,rng_value=Rand_value;
 bool rng_quick=Rand_quick;
 if(!obj->known || !tval_is_wearable(obj) || slot<0 || slot>=player->body.count) return options;
 before_brands=mem_zalloc(z_info->brand_max*sizeof(int)); after_brands=mem_zalloc(z_info->brand_max*sizeof(int));
 before_slays=mem_zalloc(z_info->slay_max*sizeof(int)); after_slays=mem_zalloc(z_info->slay_max*sizeof(int));
 memcpy(rng_state,STATE,sizeof(rng_state));
 saved_slots=mem_alloc(player->body.count*sizeof(*saved_slots));
 for(i=0;i<player->body.count;++i) saved_slots[i]=player->body.slots[i].obj;
 calc_bonuses(player,&before,true,false);
 player->state=before; player->known_state=before;
 damage_before=comparison_damage(before_brands,before_slays);
 for(n=0;n<player->body.count;++n) {
  struct player_state after={0};
  cJSON *option,*rows,*changes;
  int damage_after;
  char replaced[512];
  if(player->body.slots[n].type!=player->body.slots[slot].type) continue;
  for(i=0;i<player->body.count;++i) player->body.slots[i].obj=saved_slots[i]==obj?NULL:saved_slots[i];
  player->body.slots[n].obj=(struct object *)obj;
  player->upkeep->total_weight=weight+(carried?0:object_weight_one(obj));
  calc_bonuses(player,&after,true,false);
  player->state=after; player->known_state=after;
  damage_after=comparison_damage(after_brands,after_slays);
  option=cJSON_CreateObject(); rows=cJSON_CreateArray(); changes=cJSON_CreateArray();
  number(option,"slot",n); string(option,"slot_label",player->body.slots[n].name);
  if(saved_slots[n]) object_desc(replaced,sizeof(replaced),saved_slots[n],ODESC_PREFIX|ODESC_FULL,player);
  else my_strcpy(replaced,"Empty slot",sizeof(replaced));
  string(option,"replaces",replaced);
  json_bool(option,"blocked",saved_slots[n] && saved_slots[n]!=obj && !obj_can_takeoff(saved_slots[n]));
  if(damage_before>=0 && damage_after>=0) comparison_metric(rows,"damage","Melee dmg / rd",damage_before,damage_after,10);
  comparison_metric(rows,"blows","Blows / rd",before.num_blows/10,after.num_blows/10,10);
  comparison_metric(rows,"shots","Shots / rd",before.num_shots,after.num_shots,10);
  {
   struct object *old_weapon=NULL,*new_weapon=equipped_item_by_slot_name(player,"weapon");
   struct object *old_bow=NULL,*new_bow=equipped_item_by_slot_name(player,"shooting");
   int wi=slot_by_name(player,"weapon"),bi=slot_by_name(player,"shooting");
   if(wi>=0) old_weapon=saved_slots[wi]; if(bi>=0) old_bow=saved_slots[bi];
   comparison_metric(rows,"melee_hit","Melee hit bonus",before.to_h+(old_weapon&&old_weapon->known?object_to_hit(old_weapon->known):0)+(before.bless_wield?2:0),
    after.to_h+(new_weapon&&new_weapon->known?object_to_hit(new_weapon->known):0)+(after.bless_wield?2:0),1);
   comparison_metric(rows,"ranged_hit","Ranged hit bonus",before.to_h+(old_bow&&old_bow->known?object_to_hit(old_bow->known):0),
    after.to_h+(new_bow&&new_bow->known?object_to_hit(new_bow->known):0),1);
   comparison_metric(rows,"launcher_damage","Launcher dmg bonus",old_bow&&old_bow->known?object_to_dam(old_bow->known):0,new_bow&&new_bow->known?object_to_dam(new_bow->known):0,1);
   comparison_metric(rows,"launcher_mult","Launcher multiplier",before.ammo_mult,after.ammo_mult,1);
  }
  if(damage_before<0 || damage_after<0) string(option,"damage_note","Melee damage requires known weapon dice in both loadouts.");
  comparison_metric(rows,"armour","Armour",before.ac+before.to_a,after.ac+after.to_a,1);
  comparison_metric(rows,"speed","Speed",before.speed-110,after.speed-110,1);
  comparison_metric(rows,"weight","Carried lb",weight,player->upkeep->total_weight,10);
  for(i=0;i<STAT_MAX;++i) comparison_metric(rows,stat_names[i],stat_names[i],before.stat_use[i],after.stat_use[i],0);
  for(i=0;i<(int)N_ELEMENTS(elements);++i) if(before.el_info[i].res_level!=after.el_info[i].res_level)
   comparison_change(changes,elements[i],comparison_resistance(before.el_info[i].res_level),comparison_resistance(after.el_info[i].res_level));
  for(i=1;i<OF_MAX;++i) if(of_has(before.flags,i)!=of_has(after.flags,i)) {
   struct obj_property *prop=lookup_obj_property(OBJ_PROPERTY_FLAG,i);
   if(prop) comparison_change(changes,prop->name,of_has(before.flags,i)?"Present":"Absent",of_has(after.flags,i)?"Present":"Absent");
  }
  if(damage_before>=0 && damage_after>=0) {
   for(i=0;i<z_info->brand_max;++i) if(before_brands[i]>0 || after_brands[i]>0) {
    char key[40],label[160];
    strnfmt(key,sizeof(key),"brand-%d",i); strnfmt(label,sizeof(label),"Melee vs non-resistant %s",brands[i].name);
    comparison_metric(rows,key,label,before_brands[i]>0?before_brands[i]:damage_before,after_brands[i]>0?after_brands[i]:damage_after,10);
    if((before_brands[i]>0)!=(after_brands[i]>0)) {
     strnfmt(label,sizeof(label),"%s brand",brands[i].name);
     comparison_change(changes,label,before_brands[i]>0?"Present":"Absent",after_brands[i]>0?"Present":"Absent");
    }
   }
   for(i=0;i<z_info->slay_max;++i) if(before_slays[i]>0 || after_slays[i]>0) {
    char key[40],label[160];
    strnfmt(key,sizeof(key),"slay-%d",i); strnfmt(label,sizeof(label),"Melee vs %s",slays[i].name);
    comparison_metric(rows,key,label,before_slays[i]>0?before_slays[i]:damage_before,after_slays[i]>0?after_slays[i]:damage_after,10);
    if((before_slays[i]>0)!=(after_slays[i]>0)) {
     strnfmt(label,sizeof(label),"Slay %s",slays[i].name);
     comparison_change(changes,label,before_slays[i]>0?"Present":"Absent",after_slays[i]>0?"Present":"Absent");
    }
   }
  }
  if(before.heavy_wield!=after.heavy_wield) comparison_change(changes,"Heavy weapon",before.heavy_wield?"Yes":"No",after.heavy_wield?"Yes":"No");
  if(before.heavy_shoot!=after.heavy_shoot) comparison_change(changes,"Heavy launcher",before.heavy_shoot?"Yes":"No",after.heavy_shoot?"Yes":"No");
  if(before.cumber_armor!=after.cumber_armor) comparison_change(changes,"Armour hinders mana",before.cumber_armor?"Yes":"No",after.cumber_armor?"Yes":"No");
  cJSON_AddItemToObject(option,"metrics",rows); cJSON_AddItemToObject(option,"changes",changes);
  cJSON_AddItemToArray(options,option);
 }
 for(i=0;i<player->body.count;++i) player->body.slots[i].obj=saved_slots[i];
 player->upkeep->total_weight=weight; player->state=saved_state; player->known_state=saved_known;
 memcpy(STATE,rng_state,sizeof(rng_state)); state_i=rng_index; Rand_value=rng_value; Rand_quick=rng_quick;
 mem_free(saved_slots); mem_free(before_brands); mem_free(after_brands); mem_free(before_slays); mem_free(after_slays); return options;
}
