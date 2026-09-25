/* Tile visuals are a second, read-only presentation of the known map. The
 * terminal and all gameplay continue to use their original ASCII visuals. */
struct deluxe_visual_bank {
 uint8_t *ma, *ka, *fa, *terrain_a[LIGHTING_MAX], *trap_a[LIGHTING_MAX];
 wchar_t *mc, *kc, *fc, *terrain_c[LIGHTING_MAX], *trap_c[LIGHTING_MAX];
};
static struct deluxe_visual_bank deluxe_tile_bank;
static int deluxe_tiles_id, deluxe_tiles_loaded;
static bool deluxe_tiles_failed;

static void deluxe_visual_swap(struct deluxe_visual_bank *b)
{
#define SWAP_VISUAL(field, global, type) do { type *tmp=global; global=b->field; b->field=tmp; } while(0)
 SWAP_VISUAL(ma,monster_x_attr,uint8_t); SWAP_VISUAL(mc,monster_x_char,wchar_t);
 SWAP_VISUAL(ka,kind_x_attr,uint8_t); SWAP_VISUAL(kc,kind_x_char,wchar_t);
 SWAP_VISUAL(fa,flavor_x_attr,uint8_t); SWAP_VISUAL(fc,flavor_x_char,wchar_t);
 for(int i=0;i<LIGHTING_MAX;++i) {
  SWAP_VISUAL(terrain_a[i],feat_x_attr[i],uint8_t); SWAP_VISUAL(terrain_c[i],feat_x_char[i],wchar_t);
  SWAP_VISUAL(trap_a[i],trap_x_attr[i],uint8_t); SWAP_VISUAL(trap_c[i],trap_x_char[i],wchar_t);
 }
#undef SWAP_VISUAL
}
static void deluxe_tiles_free(void)
{
 struct deluxe_visual_bank *b=&deluxe_tile_bank;
 mem_free(b->ma); mem_free(b->mc); mem_free(b->ka); mem_free(b->kc); mem_free(b->fa); mem_free(b->fc);
 for(int i=0;i<LIGHTING_MAX;++i) {
  mem_free(b->terrain_a[i]); mem_free(b->terrain_c[i]); mem_free(b->trap_a[i]); mem_free(b->trap_c[i]);
 }
 memset(b,0,sizeof(*b)); deluxe_tiles_loaded=0;
}
static bool deluxe_tiles_prepare(void)
{
 graphics_mode *mode=get_graphics_mode(deluxe_tiles_id), *old_mode=current_graphics_mode;
 int old_graphics=use_graphics, max_flavor=0;
 uint8_t old_proj_a[PROJ_MAX][BOLT_MAX]; wchar_t old_proj_c[PROJ_MAX][BOLT_MAX];
 struct deluxe_visual_bank *b=&deluxe_tile_bank;
 if(!deluxe_tiles_id || !mode || deluxe_tiles_failed) return false;
 if(deluxe_tiles_loaded==deluxe_tiles_id) return true;
 deluxe_tiles_free();
 for(struct flavor *f=flavors;f;f=f->next) max_flavor=MAX(max_flavor,f->fidx);
#define ALLOC_VISUAL(field,count,type) b->field=mem_zalloc((count)*sizeof(type))
 ALLOC_VISUAL(ma,z_info->r_max,uint8_t); ALLOC_VISUAL(mc,z_info->r_max,wchar_t);
 ALLOC_VISUAL(ka,z_info->k_max,uint8_t); ALLOC_VISUAL(kc,z_info->k_max,wchar_t);
 ALLOC_VISUAL(fa,max_flavor+1,uint8_t); ALLOC_VISUAL(fc,max_flavor+1,wchar_t);
 for(int i=0;i<LIGHTING_MAX;++i) {
  ALLOC_VISUAL(terrain_a[i],FEAT_MAX,uint8_t); ALLOC_VISUAL(terrain_c[i],FEAT_MAX,wchar_t);
  ALLOC_VISUAL(trap_a[i],z_info->trap_max,uint8_t); ALLOC_VISUAL(trap_c[i],z_info->trap_max,wchar_t);
 }
#undef ALLOC_VISUAL
 memcpy(old_proj_a,proj_to_attr,sizeof(old_proj_a)); memcpy(old_proj_c,proj_to_char,sizeof(old_proj_c));
 deluxe_visual_swap(b); use_graphics=deluxe_tiles_id; current_graphics_mode=mode;
 reset_visuals(false);
 deluxe_tiles_failed=!process_tile_pref_file(mode->path,mode->pref);
 deluxe_visual_swap(b); use_graphics=old_graphics; current_graphics_mode=old_mode;
 memcpy(proj_to_attr,old_proj_a,sizeof(old_proj_a)); memcpy(proj_to_char,old_proj_c,sizeof(old_proj_c));
 if(deluxe_tiles_failed) return false;
 deluxe_tiles_loaded=deluxe_tiles_id; return true;
}
static void deluxe_capture_tiles(cJSON *view,int ox,int oy,int width,int height)
{
 if(!deluxe_tiles_prepare()) return;
 graphics_mode *old_mode=current_graphics_mode;
 int old_graphics=use_graphics;
 cJSON *rows=cJSON_CreateArray();
 deluxe_visual_swap(&deluxe_tile_bank);
 use_graphics=deluxe_tiles_id; current_graphics_mode=get_graphics_mode(deluxe_tiles_id);
 for(int y=0;y<height;++y) {
  cJSON *row=cJSON_CreateArray();
  for(int x=0;x<width;++x) {
   struct map_visual v;
   map_visual_readonly(loc(x+ox,y+oy),&v);
   int layers[8]={v.terrain_char,v.terrain_attr,v.trap_char,v.trap_attr,
    v.object_char,v.object_attr,v.actor_char,v.actor_attr};
   cJSON_AddItemToArray(row,ints(layers,8));
  }
  cJSON_AddItemToArray(rows,row);
 }
 deluxe_visual_swap(&deluxe_tile_bank); use_graphics=old_graphics; current_graphics_mode=old_mode;
 number(view,"tileset",deluxe_tiles_id); cJSON_AddItemToObject(view,"tiles",rows);
}

/* Development catalogue is derived from Shockbolt's overdraw rows, rather than
 * guessing from monster size, type or name. Does not alter the active tileset. */
static bool *deluxe_tall_races;
static bool deluxe_is_tall_race(int race)
{
 if(!deluxe_tall_races) {
  int saved_id=deluxe_tiles_id;
  bool saved_failure=deluxe_tiles_failed;
  deluxe_tall_races=mem_zalloc(z_info->r_max*sizeof(bool));
  deluxe_tiles_id=5; deluxe_tiles_failed=false;
  if(deluxe_tiles_prepare()) {
   graphics_mode *m=get_graphics_mode(5);
   for(int i=1;i<z_info->r_max;++i) {
    int row=deluxe_tile_bank.ma[i]&127;
    deluxe_tall_races[i]=(deluxe_tile_bank.ma[i]&128) && row>=m->overdrawRow && row<=m->overdrawMax;
   }
  }
  deluxe_tiles_free(); deluxe_tiles_id=saved_id; deluxe_tiles_failed=saved_failure;
 }
 return race>0 && race<z_info->r_max && deluxe_tall_races[race];
}
