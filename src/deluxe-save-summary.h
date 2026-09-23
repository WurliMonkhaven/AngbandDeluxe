/* Read only the engine's own lightweight description and file timestamp.
 * Never load a character just to populate the launcher. Unknown formats retain
 * their original description rather than inventing metadata. */
static void deluxe_save_summary(cJSON *out,const char *filename,const char *description)
{
 char path[1024],date[40],name[1024],identity[1024];
 const char *marker,*depth;
 struct stat st;
 int level,dungeon,used=0;
 path_build(path,sizeof(path),ANGBAND_DIR_SAVE,filename);
 if(stat(path,&st)==0) {
  cJSON_AddNumberToObject(out,"modified",(double)st.st_mtime);
  strftime(date,sizeof(date),"%d %b %Y  %H:%M",localtime(&st.st_mtime));
  string(out,"last_saved",date);
 }
 if(!description) return;
 marker=strstr(description,", dead (");
 if(marker) {
  size_t n=MIN((size_t)(marker-description),sizeof(name)-1);
  memcpy(name,description,n); name[n]=0; string(out,"name",name);
  json_bool(out,"dead",true); return;
 }
 marker=strstr(description,", L"); depth=strstr(description,", at DL");
 if(!marker || !depth || depth<=marker || sscanf(marker,", L%d %n",&level,&used)!=1 || used<=0) return;
 if(sscanf(depth,", at DL%d",&dungeon)!=1 || marker+used>=depth) return;
 {
  size_t n=MIN((size_t)(marker-description),sizeof(name)-1);
  memcpy(name,description,n); name[n]=0;
  n=MIN((size_t)(depth-(marker+used)),sizeof(identity)-1);
  memcpy(identity,marker+used,n); identity[n]=0;
 }
 string(out,"name",name); string(out,"identity",identity);
 number(out,"level",level); number(out,"depth",dungeon); json_bool(out,"dead",false);
}
