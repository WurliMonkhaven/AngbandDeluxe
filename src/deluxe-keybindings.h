/* Native keymap overlays. Preference-file mappings remain the reset baseline. */
static cJSON *keybinding_overrides;
static struct keypress keybinding_base[2][256][KEYMAP_ACTION_MAX+1];
static bool keybinding_base_user[2][256];
static unsigned keybinding_revision;

static bool deluxe_binding_key(int key)
{
 return (key>=KC_F1 && key<=KC_F12) ||
  (key>=1 && key<=26 && key!=8 && key!=9 && key!=10 && key!=13) ||
  (key>=33 && key<=126 && !(key>='0' && key<='9') && key!='\\' && key!='^');
}
static bool deluxe_binding_exposed(int group,const struct cmd_info *cmd)
{
 return (cmd->cmd || cmd->hook) && cmd->key[0] &&
  (group<5 || (cmd->key[0]<128 && strchr(";.,+psn:V",cmd->key[0])) || cmd->key[0]==KTRL('L'));
}
static struct cmd_info *deluxe_binding_command(int code)
{
 int group; size_t i;
 for(group=0;group<6;++group) for(i=0;i<cmds_all[group].len;++i) {
  struct cmd_info *cmd=&cmds_all[group].list[i];
  if((int)cmd->key[0]==code && deluxe_binding_exposed(group,cmd)) return cmd;
 }
 return NULL;
}
static struct keypress deluxe_binding_press(int code)
{ struct keypress key=KEYPRESS_NULL; key.type=EVT_KBRD; key.code=code; return key; }
static void deluxe_binding_label(cJSON *out,const char *field,int code)
{
 char text[80]; struct keypress keys[2]={KEYPRESS_NULL,KEYPRESS_NULL};
 keys[0]=deluxe_binding_press(code); keypress_to_text(text,sizeof(text),keys,true); string(out,field,text);
}
static bool deluxe_bindings_valid(const cJSON *rows)
{
 const cJSON *row; bool used[2][256]={{false}};
 if(!cJSON_IsArray(rows) || cJSON_GetArraySize(rows)>256) return false;
 cJSON_ArrayForEach(row,rows) {
  const cJSON *mode=cJSON_GetObjectItem(row,"mode"), *key=cJSON_GetObjectItem(row,"key"), *cmd=cJSON_GetObjectItem(row,"command");
  if(!cJSON_IsNumber(mode) || mode->valuedouble!=mode->valueint || mode->valueint<0 || mode->valueint>1 ||
     !cJSON_IsNumber(key) || key->valuedouble!=key->valueint || !deluxe_binding_key(key->valueint) ||
     !cJSON_IsNumber(cmd) || cmd->valuedouble!=cmd->valueint || !deluxe_binding_command(cmd->valueint)) return false;
  if(used[mode->valueint][key->valueint]) return false;
  used[mode->valueint][key->valueint]=true;
 }
 return true;
}
static void deluxe_bindings_install(const cJSON *rows)
{
 const cJSON *row;
 /* Only touch keys managed by this overlay; unrelated keymaps are preserved. */
 cJSON_ArrayForEach(row,keybinding_overrides) {
  int mode=num((cJSON *)row,"mode",0),key=num((cJSON *)row,"key",0);
  keymap_remove(mode,deluxe_binding_press(key));
  if(keybinding_base[mode][key][0].type)
   keymap_add(mode,deluxe_binding_press(key),keybinding_base[mode][key],keybinding_base_user[mode][key]);
 }
 cJSON_ArrayForEach(row,rows) {
  int mode=num((cJSON *)row,"mode",0),key=num((cJSON *)row,"key",0);
  struct cmd_info *cmd=deluxe_binding_command(num((cJSON *)row,"command",0));
  struct keypress action[2]={KEYPRESS_NULL,KEYPRESS_NULL};
  action[0]=deluxe_binding_press(cmd->key[mode]);
  /* Keymaps expand through the normal input path without recursively remapping. */
  keymap_add(mode,deluxe_binding_press(key),action,true);
 }
 cJSON_Delete(keybinding_overrides); keybinding_overrides=cJSON_Duplicate(rows,true);
 ++keybinding_revision;
}
static void deluxe_bindings_init(void)
{
 int mode,key; char path[1024],buffer[65536]; ang_file *file; cJSON *rows;
 if(keybinding_overrides) return;
 for(mode=0;mode<2;++mode) for(key=1;key<256;++key) if(deluxe_binding_key(key)) {
  const struct keypress *action=keymap_find(mode,deluxe_binding_press(key)); size_t i;
  if(action) for(i=0;i<KEYMAP_ACTION_MAX && action[i].type;++i) keybinding_base[mode][key][i]=action[i];
  keybinding_base_user[mode][key]=keymap_is_user(mode,deluxe_binding_press(key));
 }
 keybinding_overrides=cJSON_CreateArray();
 path_build(path,sizeof(path),ANGBAND_DIR_USER,"deluxe-keybindings.json");
 file=file_open(path,MODE_READ,FTYPE_TEXT); if(!file) return;
 rows=file_getl(file,buffer,sizeof(buffer))?cJSON_Parse(buffer):NULL; file_close(file);
 if(deluxe_bindings_valid(rows)) deluxe_bindings_install(rows);
 else fprintf(stderr,"AnybandUI keybindings could not be loaded; keeping native bindings.\n");
 cJSON_Delete(rows);
}
static cJSON *deluxe_bindings_get(void)
{
 int mode,group,key; size_t i; cJSON *out=cJSON_CreateObject(),*catalog=cJSON_AddArrayToObject(out,"commands"),*keys=cJSON_AddArrayToObject(out,"keys");
 deluxe_bindings_init();
 number(out,"revision",keybinding_revision); number(out,"mode",OPT(player,rogue_like_commands)?1:0);
 cJSON_AddItemToObject(out,"bindings",cJSON_Duplicate(keybinding_overrides,true));
 for(group=0;group<6;++group) for(i=0;i<cmds_all[group].len;++i) {
  struct cmd_info *cmd=&cmds_all[group].list[i]; cJSON *row;
  if(!deluxe_binding_exposed(group,cmd)) continue;
  row=cJSON_CreateObject(); number(row,"id",cmd->key[0]); string(row,"label",cmd->desc); string(row,"group",group==5?"Movement and other actions":cmds_all[group].name);
  for(mode=0;mode<2;++mode) { deluxe_binding_label(row,mode?"rogue":"original",cmd->key[mode]); number(row,mode?"rogue_key":"original_key",cmd->key[mode]); }
  cJSON_AddItemToArray(catalog,row);
 }
 for(mode=0;mode<2;++mode) for(key=1;key<256;++key) if(deluxe_binding_key(key)) {
  cJSON *row=cJSON_CreateObject(); char action[256]; const char *label="Unassigned";
  struct keypress *base=keybinding_base[mode][key];
  for(group=0;group<6;++group) for(i=0;i<cmds_all[group].len;++i)
   if(cmds_all[group].list[i].key[mode]==key) label=cmds_all[group].list[i].desc;
  /* Include movement meanings from the current keyset in conflict feedback. */
  if(mode==1 && strchr("hjklyubn",key)) label="Movement";
  if(base[0].type) { keypress_to_text(action,sizeof(action),base,true); string(row,"sequence",action); }
  number(row,"mode",mode); number(row,"key",key); deluxe_binding_label(row,"label",key);
  string(row,"default_action",base[0].type?"Existing Angband keymap":label); cJSON_AddItemToArray(keys,row);
 }
 return out;
}
static bool deluxe_bindings_save(const cJSON *rows)
{
 char path[1024],temp[1024],backup[1024]; char *text=cJSON_PrintUnformatted(rows); ang_file *file; bool ok,existed;
 path_build(path,sizeof(path),ANGBAND_DIR_USER,"deluxe-keybindings.json");
 strnfmt(temp,sizeof(temp),"%s.tmp",path); strnfmt(backup,sizeof(backup),"%s.bak",path);
 if(!text) return false;
 file=file_open(temp,MODE_WRITE,FTYPE_TEXT); if(!file) { cJSON_free(text); return false; }
 ok=file_putf(file,"%s\n",text); if(!file_close(file)) ok=false; cJSON_free(text);
 if(!ok) { file_delete(temp); return false; }
 existed=file_exists(path);
 if(existed) { file_delete(backup); if(!file_move(path,backup)) { file_delete(temp); return false; } }
 if(!file_move(temp,path)) { if(existed) file_move(backup,path); file_delete(temp); return false; }
 if(existed) file_delete(backup);
 return true;
}
static void deluxe_bindings_request(const char *id,const char *method,cJSON *params)
{
 const cJSON *rows=cJSON_GetObjectItem(params,"bindings");
 if(!character_generated || player->is_dead || !keybinding_overrides) { error(id,"wrong_phase","Start a living character first."); return; }
 if(streq(method,"keybindings.get")) { response(id,deluxe_bindings_get()); return; }
 if(!ready || active_prompt || !streq(phase,"playing")) error(id,"busy","Return to normal play before changing bindings.");
 else if(num(params,"revision",-1)!=(int)keybinding_revision) error(id,"stale_revision","Bindings changed. Reopen Settings and try again.");
 else if(!deluxe_bindings_valid(rows)) error(id,"invalid_argument","Invalid binding, reserved key or duplicate trigger. No bindings changed.");
 else if(!deluxe_bindings_save(rows)) error(id,"io_error","Could not save keybindings. Existing bindings are unchanged.");
 else { deluxe_bindings_install(rows); response(id,deluxe_bindings_get()); }
}
