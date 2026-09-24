/* Immutable, engine-owned post-mortem, captured after death_knowledge(). */
static cJSON *deluxe_run_record(const cJSON *state)
{
 struct high_score score;
 time_t ended=time(NULL);
 char date[40];
 const cJSON *item;
 cJSON *out=cJSON_CreateObject(),*items=cJSON_AddArrayToObject(out,"items");
 build_score(&score,player,player->died_from,&ended);
 strftime(date,sizeof(date),"%Y-%m-%d %H:%M:%S",localtime(&ended));
 string(out,"ended",date); string(out,"engine","org.angband.angband");
 string(out,"cause",player->died_from); number(out,"score",atol(score.pts));
 number(out,"turns",turn); number(out,"max_depth",player->max_depth);
 number(out,"max_level",player->max_lev); json_bool(out,"winner",player->total_winner);
 json_bool(out,"retired",streq(player->died_from,"Retiring"));
 cJSON_AddItemToObject(out,"player",cJSON_Duplicate(cJSON_GetObjectItem(state,"player"),true));
 cJSON_AddItemToObject(out,"messages",cJSON_Duplicate(cJSON_GetObjectItem(state,"messages"),true));
 cJSON_ArrayForEach(item,cJSON_GetObjectItem(state,"items")) if(!streq(str(item,"location"),"Floor")) {
  cJSON *copy=cJSON_Duplicate(item,true);
  cJSON_DeleteItemFromObject(copy,"actions");
  cJSON_DeleteItemFromObject(copy,"id"); /* Live revision handles have no meaning in an archive. */
  cJSON_AddItemToArray(items,copy);
 }
 cJSON_AddItemToObject(out,"journal",deluxe_journal(true));
 return out;
}
