/* Native character options. Included by the main-thread semantic adapter. */
static bool options_redraw;

static cJSON *deluxe_options(void)
{
 cJSON *out=cJSON_CreateObject(), *entries=cJSON_AddArrayToObject(out,"entries");
 int i;
 cJSON_AddStringToObject(out,"context",context_text);
 for(i=0;i<OPT_MAX;i++) if(option_type(i)==OP_INTERFACE) {
  cJSON *row=cJSON_CreateObject();
  cJSON_AddStringToObject(row,"id",option_name(i));
  cJSON_AddStringToObject(row,"label",option_desc(i));
  cJSON_AddBoolToObject(row,"value",player->opts.opt[i]);
  cJSON_AddItemToArray(entries,row);
 }
 cJSON_AddNumberToObject(out,"hitpoint_warn",player->opts.hitpoint_warn);
 cJSON_AddNumberToObject(out,"delay_factor",player->opts.delay_factor);
 cJSON_AddNumberToObject(out,"lazymove_delay",player->opts.lazymove_delay);
 return out;
}

/* Validate the entire batch before writing anything, including types/ranges.
 * Birth/cheat/score flags cannot be changed through this interface. */
static bool deluxe_options_apply(const cJSON *values)
{
 const cJSON *v;
 if(!cJSON_IsObject(values)) return false;
 cJSON_ArrayForEach(v,values) {
  const cJSON *previous;
  int i,maximum=-1;
  for(previous=values->child;previous!=v;previous=previous->next)
   if(streq(previous->string,v->string)) return false;
  if(streq(v->string,"hitpoint_warn")) maximum=9;
  else if(streq(v->string,"delay_factor") || streq(v->string,"lazymove_delay")) maximum=255;
  if(maximum>=0) {
   if(!cJSON_IsNumber(v) || v->valuedouble!=v->valueint || v->valueint<0 || v->valueint>maximum) return false;
   continue;
  }
  for(i=0;i<OPT_MAX;i++)
   if(option_type(i)==OP_INTERFACE && streq(option_name(i),v->string)) break;
  if(i==OPT_MAX || !cJSON_IsBool(v)) return false;
 }
 cJSON_ArrayForEach(v,values) {
  if(streq(v->string,"hitpoint_warn")) player->opts.hitpoint_warn=(uint8_t)v->valueint;
  else if(streq(v->string,"delay_factor")) player->opts.delay_factor=(uint8_t)v->valueint;
  else if(streq(v->string,"lazymove_delay")) player->opts.lazymove_delay=(uint8_t)v->valueint;
  else option_set(v->string,cJSON_IsTrue(v));
 }
 return true;
}
