/* Semantic storefront adapter. Included by main-deluxe.c so engine handles
 * remain private to the adapter. No pricing or transaction rules live here. */
static struct store *active_store;
static bool store_busy;
static enum { STORE_IDLE, STORE_BUY, STORE_SELL, STORE_LEAVE } store_operation;
static struct object *store_selection;
static void publish(void);
static void pump(void);

static bool deluxe_store_can_sell(const struct object *obj)
{
 return item_is_available((struct object *)obj) && store_will_buy_tester(obj) &&
  (!object_is_equipped(player->body,obj) || obj_can_takeoff(obj));
}

static void deluxe_capture_store(cJSON *state, cJSON *items, int *index)
{
 struct object **stock = mem_zalloc(z_info->store_inven_max * sizeof(*stock));
 cJSON *shop = cJSON_CreateObject(), *stock_ids = cJSON_CreateArray();
 cJSON *inventory = cJSON_CreateArray(), *record;
 bool home = active_store->feat == FEAT_HOME;
 int i;
 string(shop,"name",f_info[active_store->feat].name);
 json_bool(state,"message_pending",textui_message_pending);
 json_bool(shop,"home",home); json_bool(shop,"ready",!store_busy);
 json_bool(shop,"no_selling",OPT(player,birth_no_selling));
 if (!home && active_store->owner) {
  string(shop,"owner",active_store->owner->name);
  number(shop,"owner_purse",active_store->owner->max_cost);
 }
 /* Inventory quotes and eligibility come from the current engine store. */
 i = 0;
 cJSON_ArrayForEach(record, items) {
  struct object *obj = item_handles[++i];
  cJSON *quote;
  if (!item_is_available(obj)) continue;
  quote = cJSON_CreateObject();
  string(quote,"item_id",str(record,"id"));
  json_bool(quote,"eligible",deluxe_store_can_sell(obj));
  if (!home) number(quote,"unit_price",price_item(active_store,obj,true,1));
  cJSON_AddItemToArray(inventory,quote);
 }
 store_stock_list(active_store,stock,z_info->store_inven_max);
 for (i = 0; i < active_store->stock_num; ++i) {
  cJSON *quote = cJSON_CreateObject(), *comparisons = cJSON_CreateArray();
  int slot = wield_slot(stock[i]), n;
  record = item_record(stock[i],home ? "Home" : "Store",++*index);
  string(quote,"item_id",str(record,"id"));
  if (!home) number(quote,"unit_price",price_item(active_store,stock[i],false,1));
  /* Include every occupied matching slot (both rings, for instance), rather
   * than inventing client-side equipment categories or bonus calculations. */
  if (slot >= 0 && slot < player->body.count) {
   for (n = 0; n < player->body.count; ++n) {
    struct object *equipped = player->body.slots[n].obj;
    size_t handle;
    if (!equipped || player->body.slots[n].type != player->body.slots[slot].type) continue;
    for (handle = 1; handle < item_handle_count; ++handle) if (item_handles[handle] == equipped) {
     char id[80]; strnfmt(id,sizeof(id),"item-%lu-%u",revision,(unsigned)handle);
     cJSON_AddItemToArray(comparisons,cJSON_CreateString(id)); break;
    }
   }
  }
  cJSON_AddItemToObject(quote,"compare_with",comparisons);
  cJSON_AddItemToArray(items,record); cJSON_AddItemToArray(stock_ids,quote);
 }
 mem_free(stock);
 cJSON_AddItemToObject(shop,"stock",stock_ids);
 cJSON_AddItemToObject(shop,"inventory",inventory);
 cJSON_AddItemToObject(state,"store",shop);
}

static void deluxe_store_request(const char *id, const char *method, cJSON *params)
{
 cJSON *record;
 struct object *obj = NULL;
 int i = 0;
 bool purchase = streq(method,"store.buy");
 if (!streq(str(params,"context"),context_text)) { error(id,"stale_revision","Store contents changed; choose again."); return; }
 if (!active_store || store_busy || active_prompt || store_operation != STORE_IDLE) { error(id,"busy","Finish the current interaction first."); return; }
 if (streq(method,"store.leave")) { store_operation = STORE_LEAVE; response(id,cJSON_CreateObject()); return; }
 cJSON_ArrayForEach(record,cJSON_GetObjectItem(snapshot,"items")) {
  ++i;
  if (streq(str(record,"id"),str(params,"item")) && i < (int)item_handle_count) obj = item_handles[i];
 }
 if (!obj) { error(id,"stale_handle","Select a current item."); return; }
 if (purchase ? !pile_contains(active_store->stock,obj) : !deluxe_store_can_sell(obj)) {
  error(id,"invalid_argument","That item is not available for this transaction."); return;
 }
 store_selection = obj; store_operation = purchase ? STORE_BUY : STORE_SELL;
 response(id,cJSON_CreateObject());
}

static void deluxe_store_session(struct store *store)
{
 active_store = store; ready = false; phase = "store";
 store_busy = false; store_operation = STORE_IDLE;
 for (;;) {
  publish();
  while (store_operation == STORE_IDLE) pump();
  if (store_operation == STORE_LEAVE) break;
  store_busy = true;
  if (store_operation == STORE_SELL) pending_item = store_selection;
  textui_store_transaction(store_selection,store_operation == STORE_BUY);
  pending_item = NULL; store_selection = NULL;
  store_operation = STORE_IDLE; store_busy = false;
 }
 active_store = NULL; store_operation = STORE_IDLE;
}
