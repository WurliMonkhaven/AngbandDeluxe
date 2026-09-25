/* Native item preferences, using the same flags and setters as Angband menus. */
static bool anybandui_kind_ignore_allowed(const struct object *obj)
{
 return ignore_tval(obj->tval) && (!obj->artifact || !object_flavor_is_aware(obj));
}
static void anybandui_item_preferences(cJSON *record,const struct object *obj)
{
 cJSON *p=cJSON_CreateObject(); char label[160];
 bool aware=object_flavor_is_aware(obj);
 object_desc(label,sizeof(label),obj,ODESC_NOEGO|ODESC_BASE|ODESC_PLURAL,player);
 string(p,"kind_label",label); json_bool(p,"kind_allowed",anybandui_kind_ignore_allowed(obj));
 json_bool(p,"item_ignored",obj->known && (obj->known->notice&OBJ_NOTICE_IGNORE));
 json_bool(p,"kind_ignored",kind_is_ignored_aware(obj->kind)||kind_is_ignored_unaware(obj->kind));
 json_bool(p,"effectively_ignored",object_is_ignored(obj));
 string(p,"autoinscription",get_autoinscription(obj->kind,aware));
 cJSON_AddItemToObject(record,"preferences",p);
}
static void anybandui_rule_row(cJSON *rows,const char *type,int index,int subtype,const char *label,const char *value)
{
 cJSON *r=cJSON_CreateObject(); char id[80];
 strnfmt(id,sizeof(id),"%s-%d-%d",type,index,subtype);
 string(r,"id",id); string(r,"type",type); number(r,"index",index); number(r,"subtype",subtype);
 string(r,"label",label); string(r,"value",value); cJSON_AddItemToArray(rows,r);
}
static cJSON *anybandui_item_rules(void)
{
 cJSON *out=cJSON_CreateObject(),*rows=cJSON_CreateArray(); int i,j;
 string(out,"revision",revision_text);
 for(i=1;i<(int)item_handle_count;++i) {
  struct object *obj=item_handles[i]; char label[512];
  if(obj && obj->known && (obj->known->notice&OBJ_NOTICE_IGNORE)) {
   object_desc(label,sizeof(label),obj,ODESC_PREFIX|ODESC_FULL,player);
   anybandui_rule_row(rows,"instance",i,0,label,"Ignore this item");
  }
 }
 for(i=0;i<z_info->k_max;++i) {
  struct object_kind *kind=&k_info[i]; char label[160];
  if(!kind->name) continue;
  object_kind_name(label,sizeof(label),kind,false);
  if(kind_is_ignored_aware(kind)) anybandui_rule_row(rows,"kind",i,1,label,"Ignore when known");
  if(kind_is_ignored_unaware(kind)) anybandui_rule_row(rows,"kind",i,0,label,"Ignore when unknown");
  if(kind->note_aware) anybandui_rule_row(rows,"auto",i,1,label,quark_str(kind->note_aware));
  if(kind->note_unaware) anybandui_rule_row(rows,"auto",i,0,label,quark_str(kind->note_unaware));
 }
 for(i=0;i<ITYPE_MAX;++i) if(ignore_level[i]!=IGNORE_NONE)
  anybandui_rule_row(rows,"quality",i,0,ignore_name_for_type(i),quality_values[ignore_level[i]].name);
 for(i=0;i<z_info->e_max;++i) if(e_info[i].name) for(j=0;j<ITYPE_MAX;++j) if(ego_is_ignored(i,j)) {
  char label[160]; strnfmt(label,sizeof(label),"%s %s",ignore_name_for_type(j),e_info[i].name);
  anybandui_rule_row(rows,"ego",i,j,label,"Ignore");
 }
 cJSON_AddItemToObject(out,"rules",rows); return out;
}
static void anybandui_clear_rule(const cJSON *r)
{
 int i=num(r,"index",-1),sub=num(r,"subtype",0);
 const char *type=str(r,"type");
 if(streq(type,"instance")) item_handles[i]->known->notice &= ~OBJ_NOTICE_IGNORE;
 else if(streq(type,"kind")) k_info[i].ignore &= ~(sub?IGNORE_IF_AWARE:IGNORE_IF_UNAWARE);
 else if(streq(type,"auto")) {
  if(sub) k_info[i].note_aware=0; else k_info[i].note_unaware=0;
 } else if(streq(type,"quality")) ignore_level[i]=IGNORE_NONE;
 else if(streq(type,"ego")) ego_ignore_toggle(i,sub);
}
