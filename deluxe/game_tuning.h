// Draft editor for engine-authored constants metadata. No live engine mutation.
struct GameTuning {
 json entries=json::array(), values=json::object(), original=json::object();
 std::string revision,error,warning;
 char search[160]{};
 bool loaded=false,modified_only=false,advanced=false,force_save=false;
 int category=0;
 void reset() { *this=GameTuning{}; }
 void load(const json &j) {
  loaded=true; force_save=false; error.clear();
  if(j.contains("error")) { error=j.value("error",""); return; }
  entries=j.value("entries",json::array()); values=original=j.value("values",json::object());
  revision=j.value("revision",""); warning=j.value("warning","");
 }
 bool changed() const { return loaded && error.empty() && (force_save || values!=original); }
 int modified_count() const {
  int n=0; for(const auto &e:entries) if(values.contains(e["id"].get<std::string>()) && values[e["id"].get<std::string>()]!=e["default"]) ++n;
  return n;
 }
 void restore() { force_save=true; for(const auto &e:entries) values[e["id"].get<std::string>()]=e["default"]; }
 bool visible(const json &e) const {
  const auto id=e.value("id","");
  if(!values.contains(id)) return false;
  if(modified_only && values[id]==e["default"]) return false;
  if(!matches(e.value("label","")+" "+e.value("description","")+" "+id+" "+e.value("category",""),search)) return false;
  return true;
 }
 void help(const json &e) const {
  if(!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) return;
  ImGui::BeginTooltip(); ImGui::PushTextWrapPos(ImGui::GetFontSize()*30);
  ImGui::TextWrapped("%s",e.value("description","").c_str());
  ImGui::Separator(); ImGui::TextDisabled("%s",e.value("id","").c_str());
  if(!e.value("tier",false)) ImGui::Text("Range: %d to %d",e.value("minimum",0),e.value("maximum",0));
  ImGui::TextWrapped("%s",e.value("structural",false)?"New characters only. Existing saves retain their original storage sizes.":"Applies the next time a character is opened. Never changes a running game.");
  ImGui::PopTextWrapPos(); ImGui::EndTooltip();
 }
 void scalar(const json &e) {
  const auto id=e.value("id","");
  const int lo=e.value("minimum",0),hi=e.value("maximum",0);
  const bool changed=values[id]!=e["default"];
  ImGui::PushID(id.c_str()); ImGui::TableNextRow(); ImGui::TableNextColumn();
  ImGui::AlignTextToFramePadding();
  if(changed) ImGui::PushStyleColor(ImGuiCol_Text,DeluxeTheme::green());
  ImGui::TextWrapped("%s",e.value("label","").c_str()); help(e);
  if(changed) ImGui::PopStyleColor();
  if(e.value("structural",false)) ImGui::TextDisabled("New characters");
  if(lo==hi) ImGui::TextDisabled("Engine limit - fixed");
  ImGui::TableNextColumn(); ImGui::BeginDisabled(lo==hi); ImGui::SetNextItemWidth(-1);
  int value=values[id].get<int>();
  if(ImGui::InputInt("##value",&value,0,0)) values[id]=std::clamp(value,lo,hi);
  help(e); ImGui::EndDisabled();
  ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("%d",e["default"].get<int>());
  ImGui::TableNextColumn(); ImGui::BeginDisabled(!changed);
  if(ImGui::Button("Reset")) values[id]=e["default"];
  ImGui::EndDisabled(); ImGui::PopID();
 }
 void tiers(const std::string &group) {
  const bool old=group.rfind("o-",0)==0;
  const char *names[]={"Good","Great","Superb","Excellent","Exceptional"};
  const char *messages[]={"HIT_GOOD","HIT_GREAT","HIT_SUPERB","HIT_HI_GREAT","HIT_HI_SUPERB"};
  ImGui::PushID(group.c_str());
  if(ImGui::BeginTable("tiers",old?5:6,ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchProp)) {
   ImGui::TableSetupColumn("Tier"); ImGui::TableSetupColumn(old?"1 in N":"Below power");
   ImGui::TableSetupColumn(old?"Extra dice":"Multiplier");
   if(!old) ImGui::TableSetupColumn("Bonus");
   ImGui::TableSetupColumn("Message",ImGuiTableColumnFlags_WidthStretch,1.8f); ImGui::TableSetupColumn("",ImGuiTableColumnFlags_WidthFixed,ImGui::GetFontSize()*4);
   ImGui::TableHeadersRow();
   for(const auto &e:entries) if(e.value("group","")==group && visible(e)) {
    const auto id=e.value("id",""); auto &v=values[id]; const int n=int(v.size());
    ImGui::PushID(id.c_str()); ImGui::TableNextRow(); ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(e.value("label","").c_str()); help(e);
    for(int i=0;i<n-1;++i) {
     ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1); int value=v[i];
     if(ImGui::InputInt(("##"+std::to_string(i)).c_str(),&value,0,0)) v[i]=std::clamp(value,i==0?(old?1:-1):0,i==0?10000:100);
    }
    ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1); int choice=0;
    for(int i=0;i<5;++i) if(v[n-1]==messages[i]) choice=i;
    if(ImGui::Combo("##message",&choice,names,5)) v[n-1]=messages[choice];
    ImGui::TableNextColumn(); ImGui::BeginDisabled(v==e["default"]);
    if(ImGui::Button("Reset")) v=e["default"];
    ImGui::EndDisabled(); ImGui::PopID();
   }
   ImGui::EndTable();
  }
  ImGui::TextDisabled("%s",old?"Last tier is the fallback.":"Ascending cutoffs; the final cutoff is unused. -1 denotes the fallback.");
  ImGui::PopID();
 }
 void draw() {
  if(!loaded) { ImGui::TextDisabled("Loading game tuning..."); return; }
  if(!error.empty()) { ImGui::TextWrapped("%s",error.c_str()); return; }
  ImGui::TextWrapped("Overrides apply the next time a character is opened. Storage sizes marked New characters stay fixed for existing saves.");
  if(!warning.empty()) ImGui::TextWrapped("%s",warning.c_str());
  ImGui::Spacing(); ImGui::TextColored(DeluxeTheme::green(),"%d custom settings",modified_count());
  ImGui::SameLine(); if(ImGui::SmallButton("Restore all defaults")) restore();
  ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##search","Search tuning, descriptions or constants",search,sizeof(search));
  ImGui::Checkbox("Show modified only",&modified_only); ImGui::SameLine(); ImGui::Checkbox("Advanced",&advanced);
  const char *categories[]={"Player & inventory","Monsters","World & generation","Shops & items","Advanced combat"};
  ImGui::SetNextItemWidth(-1); ImGui::Combo("##category",&category,categories,5);
  bool any=false;
  struct Group { const char *id,*title; };
  const Group groups[]={
   {"player","Player"},{"carry-cap","Inventory & carrying"},{"mon-gen","Population & spawning"},{"mon-play","Monster behaviour"},{"level-max","Engine population limit"},
   {"world","World"},{"dun-gen","Dungeon generation"},{"store","Shops"},{"obj-make","Items & fuel"},
   {"melee-critical","Melee critical formula"},{"melee-critical-level","Melee critical tiers"},{"ranged-critical","Ranged critical formula"},{"ranged-critical-level","Ranged critical tiers"},
   {"o-melee-critical","Alternate melee formula"},{"o-melee-critical-level","Alternate melee tiers"},{"o-ranged-critical","Alternate ranged formula"},{"o-ranged-critical-level","Alternate ranged tiers"}};
  for(const auto &group:groups) {
   std::vector<const json*> rows;
   for(const auto &e:entries) {
    if(e.value("group","")!=group.id || !visible(e)) continue;
    if(!*search && !modified_only && e.value("category","")!=categories[category]) continue;
    if(!advanced && category!=4 && !*search && !modified_only && e.value("advanced",false)) continue;
    rows.push_back(&e);
   }
   if(rows.empty()) continue;
   any=true; ImGui::Spacing();
   if(!ImGui::CollapsingHeader(group.title,ImGuiTreeNodeFlags_DefaultOpen)) continue;
   if(rows.front()->value("tier",false)) { tiers(group.id); continue; }
   ImGui::PushID(group.id);
   if(ImGui::BeginTable("values",4,ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Setting",ImGuiTableColumnFlags_WidthStretch,2.8f);
    ImGui::TableSetupColumn("Value",ImGuiTableColumnFlags_WidthStretch,1.2f);
    ImGui::TableSetupColumn("Stock",ImGuiTableColumnFlags_WidthStretch,.8f);
    ImGui::TableSetupColumn("",ImGuiTableColumnFlags_WidthFixed,ImGui::GetFontSize()*4);
    ImGui::TableHeadersRow(); for(const auto *e:rows) scalar(*e); ImGui::EndTable();
   }
   ImGui::PopID();
  }
  if(!any) ImGui::TextDisabled("No matching settings.");
 }
};
