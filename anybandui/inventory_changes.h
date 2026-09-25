#pragma once
// Compare owned quantities, not revision-based handles, labels or pack slots.
// Grouping identical items also makes equipment moves and stack merges quiet.
struct InventoryChanges {
 struct Change { int amount=0; bool fresh=false; };
 bool initialized=false;
 std::map<std::string,int> previous, families;
 std::map<std::string,Change> pending;
 static bool owned(const json &item) {
  const auto location=item.value("location","");
  return !location.empty() && location!="Floor" && location!="Store" && location!="Home";
 }
 static std::string key(const json &item) { return item.value("binding_key",""); }
 void reset() { initialized=false; previous.clear(); families.clear(); pending.clear(); }
 void update(const json &state) {
  const auto phase=state.value("phase","");
  if(phase!="playing" && phase!="store") { reset(); return; }
  if(!state.contains("items")) return;
  std::map<std::string,int> current,totals;
  std::map<std::string,std::string> family;
  for(const auto &item:state["items"]) if(owned(item)) {
   const auto id=key(item); if(id.empty()) continue;
   const auto kind=item.value("kind_key",id);
   const int quantity=std::max(0,item.value("quantity",0));
   current[id]+=quantity; totals[kind]+=quantity; family[id]=kind;
  }
  if(initialized) {
   std::map<std::string,int> gains;
   for(const auto &[kind,n]:totals) gains[kind]=std::max(0,n-families[kind]);
   for(const auto &[id,n]:current) {
    // Enchanting an existing weapon changes its binding but not its family
    // total. Only a genuine increase in owned items creates a notification.
    const int added=std::min(std::max(0,n-previous[id]),gains[family[id]]);
    if(added>0) {
     auto &change=pending[id]; change.amount+=added; change.fresh|=previous[id]==0;
     gains[family[id]]-=added;
    }
   }
  }
  for(auto it=pending.begin();it!=pending.end();) {
   const auto found=current.find(it->first);
   if(found==current.end() || found->second<=0) it=pending.erase(it);
   else { it->second.amount=std::min(it->second.amount,found->second); ++it; }
  }
  previous=std::move(current); families=std::move(totals); initialized=true;
 }
 const Change *find(const json &item) const {
  const auto it=pending.find(key(item)); return it==pending.end()?nullptr:&it->second;
 }
 void acknowledge(const json &item) { pending.erase(key(item)); }
};
