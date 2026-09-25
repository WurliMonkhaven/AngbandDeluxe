#pragma once
#include "imgui.h"
#include <nlohmann/json.hpp>
#include <array>
#include <vector>
#include <string>
#include <algorithm>
#include <functional>
#include <cmath>
#include <stdexcept>

// Layout owns geometry and panel IDs only. Gameplay and inspection state stay in UI.
struct WorkspaceLayout {
 using Json=nlohmann::json;
 enum Panel { Dungeon,Character,Messages,Inventory,Spells,Creatures,Map,Commands,More,Target,Quickbar,DungeonDetails,TrackedCreature,Status,Count };
 inline static constexpr const char *names[]={"Dungeon","Character","Messages","Inventory","Spells","Creatures","Map","Commands","More","Look / Target","Quickbar","Dungeon details","Tracked creature","Status effects"};
 struct Node {
  int id=0,axis=0; float ratio=.5f; // axis: 0 tabs, 1 left/right, 2 top/bottom
  std::vector<int> tabs; int active=-1;
  std::vector<Node> children;
 };
 struct Floating { Node node; float x=.15f,y=.15f,w=.36f,h=.4f; };
 struct Detached { bool open=false; int x=0,y=0,w=560,h=640; bool placed=false; };
 std::array<Detached,Count> detached{};
 bool native_windows_available=false,measuring_drop=false;
 static bool detachable(int p) { return p==Inventory || p==Messages || p==Map || p==Character || p==DungeonDetails || p==TrackedCreature || p==Status; }
 void detach(int p,bool open) { if(!detachable(p)) return; if(open || contains(p)) reveal(p); detached[p].open=open; dirty=true; }
 Node root; std::vector<Floating> floating;
 Json saved=Json::object(),before;
 bool editing=false,dirty=false,dividers_locked=true,floating_locked=false; int next_id=1;
 std::array<bool,Count> panel_headings=[] { std::array<bool,Count> values{}; values.fill(true); return values; }();
 static bool has_heading(int p) { return p==Messages || p==DungeonDetails || p==TrackedCreature || p==Status; }
 bool heading(int p) const { return p==Character || panel_headings[p]; }
 std::function<float(int,float)> compact_height;
 float tracker_content_height=0;
 float quickbar_content_height=0; // Supplied by the quickbar renderer, in current UI pixels.
 char save_name[65]{};
 struct Move { int panel=-1,target=0,edge=0; }; // 0 tabs, 1 left, 2 right, 3 top, 4 bottom, 5 float, 6 hide
 Move pending;
 Node leaf(std::initializer_list<int> tabs) { Node n; n.id=next_id++; n.tabs=tabs; if(!n.tabs.empty()) n.active=n.tabs[0]; return n; }
 Node split(int axis,float ratio,Node a,Node b) { Node n; n.id=next_id++; n.axis=axis; n.ratio=ratio; n.children={std::move(a),std::move(b)}; return n; }
 Node overview() { return split(2,.54f,split(2,.78f,leaf({Character}),leaf({Status})),split(2,.56f,leaf({DungeonDetails}),leaf({TrackedCreature}))); }
 WorkspaceLayout() { preset(0); dirty=false; }
 void preset(int index) {
  next_id=1; floating.clear(); detached={};
  if(index==1) {
   // Keep only the essentials; other panels can be restored from Panels.
   auto play=split(2,.86f,split(2,.88f,leaf({Dungeon}),leaf({Quickbar})),leaf({Messages}));
   root=split(1,.78f,std::move(play),overview());
  } else if(index==2) {
   // Map and messages stay visible even for characters without spells.
   auto bottom=split(1,.58f,leaf({Messages}),leaf({Map}));
   auto play=split(2,.70f,split(2,.88f,leaf({Dungeon}),leaf({Quickbar})),std::move(bottom));
   auto inventory=leaf({Inventory,Creatures,Commands,More,Target});
   auto side=split(2,.56f,overview(),split(2,.55f,std::move(inventory),leaf({Spells})));
   root=split(1,.68f,std::move(play),std::move(side));
  } else {
   auto tools=leaf({Inventory,Spells,Creatures,Map,Commands,More,Target});
   auto play=split(2,.75f,split(2,.88f,leaf({Dungeon}),leaf({Quickbar})),leaf({Messages}));
   auto side=split(2,.68f,overview(),std::move(tools));
   root=split(1,.69f,std::move(play),std::move(side));
  }
  dirty=true;
 }
 static Json encode(const Node &n) {
  Json j={{"id",n.id},{"axis",n.axis},{"ratio",n.ratio},{"tabs",n.tabs},{"active",n.active}};
  if(n.axis) j["children"]={encode(n.children[0]),encode(n.children[1])};
  return j;
 }
 Json arrangement() const {
  Json j={{"version",4},{"root",encode(root)},{"floating",Json::array()}};
  for(const auto &f:floating) j["floating"].push_back({{"node",encode(f.node)},{"x",f.x},{"y",f.y},{"w",f.w},{"h",f.h}});
  j["detached"]=Json::array();
  for(int p=0;p<Count;++p) if(detached[p].placed || detached[p].open) { const auto &d=detached[p]; j["detached"].push_back({{"panel",p},{"open",d.open},{"placed",d.placed},{"x",d.x},{"y",d.y},{"w",d.w},{"h",d.h}}); }
  return j;
 }
 Json serialize() const { return {{"version",4},{"panel_headings",editing && history_ready?edit_initial.headings:panel_headings},{"dividers_locked",editing && history_ready?edit_initial.dividers_locked:dividers_locked},{"floating_locked",editing && history_ready?edit_initial.floating_locked:floating_locked},{"current",editing?before:arrangement()},{"saved",saved}}; }
 struct EditSnapshot {
  Node root; std::vector<Floating> floating; std::array<Detached,Count> detached;
  std::array<bool,Count> headings; bool dividers_locked,floating_locked; int next_id; Json key;
 };
 std::vector<EditSnapshot> undo_steps,redo_steps;
 EditSnapshot edit_initial,edit_checkpoint;
 bool history_ready=false;
 Json edit_key() const { auto key=arrangement(); key["headings"]=panel_headings; key["dividers_locked"]=dividers_locked; key["floating_locked"]=floating_locked; return key; }
 EditSnapshot snapshot() const { return {root,floating,detached,panel_headings,dividers_locked,floating_locked,next_id,edit_key()}; }
 void apply_snapshot(const EditSnapshot &state) {
  root=state.root; floating=state.floating; detached=state.detached; panel_headings=state.headings;
  dividers_locked=state.dividers_locked; floating_locked=state.floating_locked; next_id=state.next_id; pending={}; dirty=true;
 }
 void begin_edit() {
  before=arrangement(); editing=true; undo_steps.clear(); redo_steps.clear();
  edit_initial=edit_checkpoint=snapshot(); history_ready=true;
 }
 void record_edit() {
  if(!editing || !history_ready || edit_key()==edit_checkpoint.key) return;
  undo_steps.push_back(edit_checkpoint); if(undo_steps.size()>64) undo_steps.erase(undo_steps.begin());
  edit_checkpoint=snapshot(); redo_steps.clear();
 }
 void undo_edit() {
  record_edit(); if(undo_steps.empty()) return;
  redo_steps.push_back(edit_checkpoint); apply_snapshot(undo_steps.back()); undo_steps.pop_back(); edit_checkpoint=snapshot();
 }
 void redo_edit() {
  if(redo_steps.empty()) return;
  undo_steps.push_back(edit_checkpoint); apply_snapshot(redo_steps.back()); redo_steps.pop_back(); edit_checkpoint=snapshot();
 }
 void finish_edit(bool cancel=false) {
  if(cancel) { if(history_ready) apply_snapshot(edit_initial); else restore(before); }
  editing=false; dirty=true; history_ready=false; undo_steps.clear(); redo_steps.clear();
 }
 // Bounded parser rejects duplicates, invalid panels and unreasonable trees.
 bool restore(const Json &j) {
  try {
   int serial=1; std::array<bool,Count> seen{};
   std::function<Node(const Json&,int)> parse=[&](const Json &v,int depth) {
    if(depth>20 || serial>64) throw std::runtime_error("Layout too deep");
    Node n; n.id=serial++; n.axis=v.at("axis").get<int>();
    if(n.axis<0 || n.axis>2) throw std::runtime_error("Invalid split");
    n.ratio=v.value("ratio",.5f); if(!std::isfinite(n.ratio)) throw std::runtime_error("Invalid ratio"); n.ratio=std::clamp(n.ratio,.08f,.92f);
    if(n.axis) {
     const auto &children=v.at("children"); if(children.size()!=2) throw std::runtime_error("Invalid children");
     n.children={parse(children[0],depth+1),parse(children[1],depth+1)};
    } else {
     for(const auto &entry:v.at("tabs")) {
      int p=entry.get<int>(); if(p<0 || p>=Count || seen[p]) throw std::runtime_error("Duplicate or unknown panel");
      seen[p]=true; n.tabs.push_back(p);
     }
     if(n.tabs.empty()) throw std::runtime_error("Empty tabs");
     if(std::find(n.tabs.begin(),n.tabs.end(),Dungeon)!=n.tabs.end() && n.tabs.size()!=1) throw std::runtime_error("Dungeon must remain visible");
     n.active=v.value("active",n.tabs.front());
     if(std::find(n.tabs.begin(),n.tabs.end(),n.active)==n.tabs.end()) n.active=n.tabs.front();
    }
    return n;
   };
   Node candidate=parse(j.at("root"),0); std::vector<Floating> floats;
   for(const auto &v:j.value("floating",Json::array())) {
    Floating f; f.node=parse(v.at("node"),0);
    if(contains(f.node,Dungeon)) throw std::runtime_error("Dungeon must remain docked");
    f.x=v.value("x",.15f); f.y=v.value("y",.15f); f.w=v.value("w",.36f); f.h=v.value("h",.4f);
    if(!std::isfinite(f.x)||!std::isfinite(f.y)||!std::isfinite(f.w)||!std::isfinite(f.h)) throw std::runtime_error("Invalid floating geometry");
    f.w=std::clamp(f.w,.12f,1.f); f.h=std::clamp(f.h,.1f,1.f); f.x=std::clamp(f.x,0.f,1-f.w); f.y=std::clamp(f.y,0.f,1-f.h); floats.push_back(std::move(f));
   }
   std::array<Detached,Count> windows{};
   for(const auto &v:j.value("detached",Json::array())) {
    int p=v.at("panel").get<int>(); if(!detachable(p) || !seen[p]) continue;
    auto &d=windows[p]; d.open=v.value("open",false); d.placed=v.value("placed",false);
    d.x=std::clamp(v.value("x",0),-100000,100000); d.y=std::clamp(v.value("y",0),-100000,100000);
    d.w=std::clamp(v.value("w",560),280,4096); d.h=std::clamp(v.value("h",640),180,4096);
   }
   if(!seen[Dungeon]) return false;
   detached=windows;
   root=std::move(candidate); floating=std::move(floats); next_id=serial; pending={};
   // Older layouts included dungeon details inside Character. Preserve their
   // location when separating it, without reviving deliberately hidden v2 panels.
   if(j.value("version",1)<2 && !contains(DungeonDetails)) {
    std::function<bool(Node&)> migrate=[&](Node &n) {
     if(!n.axis && contains(n,Character)) {
      Node old=std::move(n); n=split(2,.70f,std::move(old),leaf({DungeonDetails})); return true;
     }
     for(auto &child:n.children) if(migrate(child)) return true;
     return false;
    };
    if(!migrate(root)) for(auto &f:floating) if(migrate(f.node)) break;
   }
   // Split out the formerly embedded tracker once. Version 3 preserves hiding.
   if(j.value("version",1)<3 && !contains(TrackedCreature)) {
    std::function<bool(Node&)> migrate_tracker=[&](Node &n) {
     if(!n.axis && contains(n,Character)) {
      Node old=std::move(n); n=split(2,.75f,std::move(old),leaf({TrackedCreature})); return true;
     }
     for(auto &child:n.children) if(migrate_tracker(child)) return true;
     return false;
    };
    if(!migrate_tracker(root)) for(auto &f:floating) if(migrate_tracker(f.node)) break;
   }
   if(j.value("version",1)<4 && !contains(Status)) {
    std::function<bool(Node&)> migrate_status=[&](Node &n) {
     if(!n.axis && contains(n,Character)) {
      Node old=std::move(n); n=split(2,.78f,std::move(old),leaf({Status})); return true;
     }
     for(auto &child:n.children) if(migrate_status(child)) return true;
     return false;
    };
    if(!migrate_status(root)) for(auto &f:floating) if(migrate_status(f.node)) break;
   }
   return true;
  } catch(...) { return false; }
 }
 void load(const Json &j) {
  if(!j.is_object() || j.value("version",0)<1 || j.value("version",0)>4) return;
  panel_headings.fill(j.contains("show_headings") && j["show_headings"].is_boolean()?j["show_headings"].get<bool>():true);
  if(j.contains("panel_headings") && j["panel_headings"].is_array()) {
   const auto &values=j["panel_headings"];
   for(int p=0;p<Count && p<int(values.size());++p) if(values[p].is_boolean()) panel_headings[p]=values[p].get<bool>();
  }
  panel_headings[Character]=true; // Identity is information, not an optional heading.
  if(j.contains("floating_locked") && j["floating_locked"].is_boolean()) floating_locked=j["floating_locked"].get<bool>();
  if(j.contains("dividers_locked") && j["dividers_locked"].is_boolean()) dividers_locked=j["dividers_locked"].get<bool>();
  if(j.contains("current")) restore(j["current"]);
  if(j.contains("saved") && j["saved"].is_object()) {
   for(auto it=j["saved"].begin();it!=j["saved"].end() && saved.size()<32;++it) {
    WorkspaceLayout check; if(it.key().size()<=64 && check.restore(it.value())) saved[it.key()]=it.value();
   }
  }
 }
 Node *find(Node &n,int id) {
  if(n.id==id) return &n;
  for(auto &child:n.children) if(auto *result=find(child,id)) return result;
  return nullptr;
 }
 Node *find(int id) { if(auto *n=find(root,id)) return n; for(auto &f:floating) if(auto *n=find(f.node,id)) return n; return nullptr; }
 static bool contains(const Node &n,int panel) {
  if(std::find(n.tabs.begin(),n.tabs.end(),panel)!=n.tabs.end()) return true;
  for(const auto &c:n.children) if(contains(c,panel)) return true;
  return false;
 }
 bool contains(int panel) const { if(contains(root,panel)) return true; for(const auto &f:floating) if(contains(f.node,panel)) return true; return false; }
 static bool remove(Node &n,int panel) {
  if(!n.axis) {
   n.tabs.erase(std::remove(n.tabs.begin(),n.tabs.end(),panel),n.tabs.end());
   if(n.active==panel) n.active=n.tabs.empty()?-1:n.tabs.front();
   return n.tabs.empty();
  }
  bool a=remove(n.children[0],panel),b=remove(n.children[1],panel);
  if(a&&b) return true;
  if(a||b) { Node keep=std::move(n.children[a?1:0]); n=std::move(keep); }
  return false;
 }
 bool move(Move action) {
  if(action.edge<0 || action.edge>6 || action.panel<0 || action.panel>=Count || (action.panel==Dungeon && action.edge>=5)) return false;
  auto *destination=find(action.target);
  if(action.edge<5 && (!destination || destination->axis || (contains(*destination,action.panel)&&destination->tabs.size()==1))) return false;
  // Moving the dungeon into a tab group is intentionally disallowed: it must stay visible.
  if(action.edge==0 && (action.panel==Dungeon || contains(*destination,Dungeon))) return false;
  if(contains(root,action.panel) && !root.axis && root.tabs.size()==1) return false;
  detached[action.panel].open=false;
  remove(root,action.panel);
  for(auto it=floating.begin();it!=floating.end();) { if(remove(it->node,action.panel)) it=floating.erase(it); else ++it; }
  if(action.edge==5) { Floating f; f.node=leaf({action.panel}); floating.push_back(std::move(f)); }
  else if(action.edge<5) {
   destination=find(action.target); if(!destination) return false;
   if(!action.edge) { destination->tabs.push_back(action.panel); destination->active=action.panel; }
   else {
    Node old=std::move(*destination), added=leaf({action.panel});
    const bool first=action.edge==1||action.edge==3;
    *destination=split(action.edge<=2?1:2,.5f,first?std::move(added):std::move(old),first?std::move(old):std::move(added));
   }
  }
  dirty=true; return true;
 }
 void reveal(int panel,bool restore_hidden=true) {
  if(!contains(panel) && !restore_hidden) return;
  if(!contains(panel)) { Floating f; f.node=leaf({panel}); floating.push_back(std::move(f)); dirty=true; }
  std::function<void(Node&)> select=[&](Node &n) { if(std::find(n.tabs.begin(),n.tabs.end(),panel)!=n.tabs.end()) n.active=panel; for(auto &c:n.children) select(c); };
  select(root); for(auto &f:floating) select(f.node);
 }
 void toggle(int panel) {
  if(panel<=Dungeon || panel>=Count) return;
  if(contains(panel)) move({panel,0,6}); else reveal(panel);
 }
 void menu(bool available,const std::function<bool(int)> &enabled) {
  ImGui::BeginDisabled(!available);
  if(ImGui::Button("Layout")) ImGui::OpenPopup("Layout menu");
  ImGui::EndDisabled();
  if(ImGui::BeginPopup("Layout menu")) {
   if(ImGui::MenuItem(editing?"Finish customizing":"Customize layout")) {
    if(editing) finish_edit(); else begin_edit();
   }
   if(editing) {
    if(ImGui::MenuItem("Undo","Ctrl+Z",false,!undo_steps.empty())) undo_edit();
    if(ImGui::MenuItem("Redo","Ctrl+Y",false,!redo_steps.empty())) redo_edit();
    if(ImGui::MenuItem("Cancel customization")) finish_edit(true);
   }
   ImGui::SeparatorText("Panels");
   if(ImGui::BeginMenu("Show / hide panels")) {
    ImGui::TextDisabled("Checked panels are visible in your workspace.");
    ImGui::Separator();
    for(int p=Character;p<Count;++p) {
     const bool usable=enabled(p);
     if(ImGui::MenuItem(names[p],nullptr,usable && contains(p),usable)) toggle(p);
     if(!usable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("This panel is unavailable in the current game state or gameplay settings.");
    }
    ImGui::EndMenu();
   }
   if(ImGui::BeginMenu("Panel headings")) {
    for(int p=0;p<Count;++p) if(has_heading(p))
     if(ImGui::MenuItem(names[p],nullptr,panel_headings[p])) { panel_headings[p]=!panel_headings[p]; dirty=true; }
    ImGui::EndMenu();
   }
   if(native_windows_available && ImGui::BeginMenu("Separate windows",!editing)) {
    ImGui::TextDisabled("Checked panels open in their own window.");
    ImGui::Separator();
    for(int p=0;p<Count;++p) if(detachable(p) && enabled(p))
     if(ImGui::MenuItem(names[p],nullptr,detached[p].open)) detach(p,!detached[p].open);
    const bool any=std::any_of(detached.begin(),detached.end(),[](const Detached &d) { return d.open; });
    ImGui::Separator();
    if(ImGui::MenuItem("Return all to main window",nullptr,false,any)) { for(auto &d:detached) d.open=false; dirty=true; }
    ImGui::EndMenu();
   }
   if(native_windows_available && editing && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Finish customizing before opening separate windows.");
   ImGui::SeparatorText("Movement during play");
   if(ImGui::MenuItem("Lock dividers",nullptr,dividers_locked)) { dividers_locked=!dividers_locked; dirty=true; }
   if(ImGui::IsItemHovered()) ImGui::SetTooltip("Prevent accidental divider resizing during play. Customization always allows resizing where space permits.");
   if(ImGui::MenuItem("Lock floating panels",nullptr,floating_locked)) { floating_locked=!floating_locked; dirty=true; }
   if(ImGui::IsItemHovered()) ImGui::SetTooltip("Prevent dragging panels floated inside the main window. Customization always allows moving them.");
   ImGui::SeparatorText("Arrangements");
   if(ImGui::BeginMenu("Starting layouts")) {
    const char *presets[]={"Classic","Dungeon first","Command centre"};
    const char *descriptions[]={"Restore the classic dungeon, messages and tabbed sidebar.","Large dungeon, character overview and messages. Optional panels hidden.","Separate inventory, map and messages, plus spells when available."};
    for(int i=0;i<3;++i) {
     if(ImGui::MenuItem(presets[i])) preset(i);
     if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",descriptions[i]);
    }
    ImGui::EndMenu();
   }
   if(ImGui::BeginMenu("Saved layouts")) {
    if(saved.empty()) ImGui::TextDisabled("No saved layouts yet.");
    else {
     ImGui::TextDisabled("Choose a layout to restore it.");
     for(auto it=saved.begin();it!=saved.end();++it) if(ImGui::MenuItem(it.key().c_str())) { restore(it.value()); dirty=true; }
    }
    ImGui::SeparatorText("Save current arrangement");
    ImGui::SetNextItemWidth(ImGui::GetFontSize()*18);
    ImGui::InputTextWithHint("##layout name","Layout name",save_name,sizeof(save_name));
    ImGui::BeginDisabled(save_name[0]==0 || (saved.size()>=32 && !saved.contains(save_name)));
    if(ImGui::Button(saved.contains(save_name)?"Replace saved layout":"Save layout")) { saved[save_name]=arrangement(); dirty=true; save_name[0]=0; }
    ImGui::EndDisabled();
    if(ImGui::BeginMenu("Delete saved layout",!saved.empty())) {
     std::string erase;
     for(auto it=saved.begin();it!=saved.end();++it) if(ImGui::MenuItem(it.key().c_str())) erase=it.key();
     if(!erase.empty()) { saved.erase(erase); dirty=true; }
     ImGui::EndMenu();
    }
    ImGui::EndMenu();
   }
   ImGui::EndPopup();
  }
 }
 using Enabled=std::function<bool(int)>;
 static bool visible(const Node &n,const Enabled &enabled) {
  for(int p:n.tabs) if(enabled(p)) return true;
  for(const auto &c:n.children) if(visible(c,enabled)) return true;
  return false;
 }
 void source(int panel) {
  if(editing && ImGui::BeginDragDropSource()) {
   ImGui::SetDragDropPayload("DELUXE_PANEL",&panel,sizeof(panel)); ImGui::TextUnformatted(names[panel]); ImGui::EndDragDropSource();
  }
 }
 struct DockPreview { bool active=false; ImVec2 pos,size; std::string label; } dock_preview;
 ImVec2 workspace_pos,workspace_size,workspace_view_pos,workspace_view_size,floating_preview_pos,floating_preview_size;
 bool measure_panel(const Node &n,ImVec2 pos,ImVec2 size,int panel,const Enabled &enabled,ImVec2 &out_pos,ImVec2 &out_size) const {
  if(!visible(n,enabled) || !contains(n,panel)) return false;
  if(!n.axis) {
   if(float fitted=fitted_height(n,enabled,size.x); fitted>0) size.y=std::min(size.y,fitted);
   out_pos=pos; out_size=size; return true;
  }
  const bool a=visible(n.children[0],enabled),b=visible(n.children[1],enabled);
  if(!a || !b) return measure_panel(n.children[a?0:1],pos,size,panel,enabled,out_pos,out_size);
  const float usable=std::max(2.f,(n.axis==1?size.x:size.y)-6);
  const float measured_first=n.axis==1?first_width(n,enabled,size.x):size.x;
  auto ma=minimum(n.children[0],enabled,measured_first),mb=minimum(n.children[1],enabled,n.axis==1?size.x-6-measured_first:size.x);
  float lower=n.axis==1?ma.x:ma.y,upper=n.axis==1?mb.x:mb.y;
  if(lower+upper>usable) { float fit=usable/(lower+upper); lower*=fit; upper*=fit; }
  upper=std::max(lower,usable-upper);
  const float fa=n.axis==2?fitted_height(n.children[0],enabled,size.x):0,fb=n.axis==2?fitted_height(n.children[1],enabled,size.x):0;
  const float first=std::clamp(fa>0?fa:fb>0?usable-fb:usable*n.ratio,lower,upper);
  const int child=contains(n.children[0],panel)?0:1;
  if(n.axis==1) { if(child) pos.x+=first+6; size.x=child?usable-first:first; }
  else { if(child) pos.y+=first+6; size.y=child?usable-first:first; }
  return measure_panel(n.children[child],pos,size,panel,enabled,out_pos,out_size);
 }
 void target(int id,int edge,ImVec2 pos,ImVec2 size,const Enabled &enabled) {
  if(ImGui::BeginDragDropTarget()) {
   if(const auto *payload=ImGui::AcceptDragDropPayload("DELUXE_PANEL",ImGuiDragDropFlags_AcceptBeforeDelivery|ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
    const int panel=*static_cast<const int*>(payload->Data);
    auto *destination=find(id);
    const std::string destination_name=destination && destination->active>=0?names[destination->active]:"panel";
    WorkspaceLayout planned; planned.root=root; planned.floating=floating; planned.detached=detached; planned.next_id=next_id;
    planned.compact_height=compact_height; planned.editing=editing; planned.measuring_drop=true; planned.quickbar_content_height=quickbar_content_height; planned.tracker_content_height=tracker_content_height;
    const Move action{panel,id,edge};
    if(planned.move(action)) {
     // Measure the proposed tree after removing the source, so its vacated
     // space and fixed-height panels are included in the landing preview.
     ImVec2 landing_pos=pos,landing_size=size;
     const auto planned_size=planned.canvas_size(planned.root,enabled,workspace_view_size);
     ImVec2 planned_pos=workspace_view_pos;
     if(planned_size.x>workspace_view_size.x+.5f || planned_size.y>workspace_view_size.y+.5f) {
      const float scrollbar=ImGui::GetStyle().ScrollbarSize;
      planned_pos.x-=std::clamp(workspace_view_pos.x-workspace_pos.x,0.f,std::max(0.f,planned_size.x-workspace_view_size.x+scrollbar));
      planned_pos.y-=std::clamp(workspace_view_pos.y-workspace_pos.y,0.f,std::max(0.f,planned_size.y-workspace_view_size.y+scrollbar));
     }
     if(!planned.measure_panel(planned.root,planned_pos,planned_size,panel,enabled,landing_pos,landing_size)) {
      for(const auto &f:planned.floating) if(contains(f.node,panel)) {
       // Floating destinations retain their window footprint.
       planned.measure_panel(f.node,floating_preview_pos,floating_preview_size,panel,enabled,landing_pos,landing_size); break;
      }
     }
     static const char *verbs[]={"Stack with ","Place left of ","Place right of ","Place above ","Place below "};
     dock_preview={true,landing_pos,landing_size,std::string(verbs[edge])+destination_name};
     if(payload->IsDelivery()) pending=action;
    } else if(payload->IsPreview()) ImGui::SetTooltip("This panel cannot be docked here. The dungeon must stay visible.");
   }
   ImGui::EndDragDropTarget();
  }
 }
 void draw_dock_preview() const {
  if(!dock_preview.active) return;
  const auto &p=dock_preview; auto *d=ImGui::GetForegroundDrawList();
  ImVec4 accent=ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive); accent.w=.3f;
  const ImVec2 end(p.pos.x+p.size.x,p.pos.y+p.size.y);
  d->AddRectFilled(p.pos,end,ImGui::GetColorU32(accent),4);
  accent.w=1; d->AddRect(p.pos,end,ImGui::GetColorU32(accent),4,0,2);
  const float pad=ImGui::GetFontSize()*.5f,wrap=std::max(1.f,p.size.x-4*pad);
  const ImVec2 text=ImGui::CalcTextSize(p.label.c_str(),nullptr,false,wrap);
  const ImVec2 label(p.pos.x+pad,p.pos.y+pad);
  d->AddRectFilled(label,{label.x+text.x+2*pad,label.y+text.y+2*pad},ImGui::GetColorU32(ImGuiCol_WindowBg),3);
  d->AddText(ImGui::GetFont(),ImGui::GetFontSize(),{label.x+pad,label.y+pad},ImGui::GetColorU32(ImGuiCol_Text),p.label.c_str(),nullptr,wrap);
 }
 // Measure the same widths and chrome that draw_node actually uses. In
 // particular, tabbing compact panels must not turn them into flexible panels.
 float panel_height(int panel,float width) const {
  return panel==Quickbar?quickbar_content_height:panel==TrackedCreature?tracker_content_height:
   compact_height?compact_height(panel,std::max(1.f,(width>0?width:22*ImGui::GetFontSize())-2*ImGui::GetStyle().WindowPadding.x)):0;
 }
 float chrome_height(const Node &n,const Enabled &enabled) const {
  int count=0; for(int p:n.tabs) if(enabled(p)) ++count;
  float height=2*ImGui::GetStyle().WindowPadding.y;
  if(editing || count>1) height+=ImGui::GetFrameHeightWithSpacing();
  return height;
 }
 float minimum_width(const Node &n,const Enabled &enabled) const {
  if(!visible(n,enabled)) return 0;
  if(n.axis) {
   float a=minimum_width(n.children[0],enabled),b=minimum_width(n.children[1],enabled);
   if(!a || !b) return std::max(a,b);
   return n.axis==1?a+b+6:std::max(a,b);
  }
  float units=10;
  for(int p:n.tabs) if(enabled(p)) units=std::max(units,
   p==Dungeon||p==Quickbar||p==Character||p==Status||p==DungeonDetails||p==TrackedCreature?22.f:p==Inventory||p==Spells?18.f:14.f);
  return units*ImGui::GetFontSize();
 }
 float first_width(const Node &n,const Enabled &enabled,float width) const {
  const float usable=std::max(2.f,width-6),a=minimum_width(n.children[0],enabled),b=minimum_width(n.children[1],enabled);
  if(a+b>usable) return usable*a/std::max(1.f,a+b);
  return std::clamp(usable*n.ratio,a,usable-b);
 }
 float fitted_height(const Node &n,const Enabled &enabled,float width=0) const {
  if(!visible(n,enabled)) return 0;
  if(n.axis) {
   bool a=visible(n.children[0],enabled),b=visible(n.children[1],enabled);
   if(!a || !b) return fitted_height(n.children[a?0:1],enabled,width);
   const float first=n.axis==1?first_width(n,enabled,width):width;
   float ah=fitted_height(n.children[0],enabled,first),bh=fitted_height(n.children[1],enabled,n.axis==1?std::max(1.f,width-6-first):width);
   return ah>0 && bh>0?(n.axis==2?ah+bh+6:std::max(ah,bh)):0;
  }
  float selected=0; int active=-1;
  for(int p:n.tabs) if(enabled(p)) {
   const float h=panel_height(p,width);
   if(h<=0) return 0; // A mixed group still needs a resizable content area.
   if(active<0 || p==n.active) { active=p; selected=h; }
  }
  // ImGui floors child rectangles to pixels; round up to avoid tiny scrollbars.
  return selected>0?std::ceil(selected+chrome_height(n,enabled))+1.f:0;
 }
 ImVec2 minimum(const Node &n,const Enabled &enabled,float width=0) const {
  if(!visible(n,enabled)) return {0,0};
  if(n.axis) {
   const bool va=visible(n.children[0],enabled),vb=visible(n.children[1],enabled);
   if(!va || !vb) return minimum(n.children[va?0:1],enabled,width);
   const float first=n.axis==1?first_width(n,enabled,width):width;
   auto a=minimum(n.children[0],enabled,first),b=minimum(n.children[1],enabled,n.axis==1?std::max(1.f,width-6-first):width);
   return n.axis==1?ImVec2(a.x+b.x+6,std::max(a.y,b.y)):ImVec2(std::max(a.x,b.x),a.y+b.y+6);
  }
  if(float height=fitted_height(n,enabled,width); height>0) return {minimum_width(n,enabled),height};
  float height=5*ImGui::GetFontSize();
  for(int p:n.tabs) if(enabled(p)) {
   const float content=panel_height(p,width);
   height=std::max(height,content>0?content:(p==Dungeon?10.f:p==Character?12.f:p==Inventory||p==Spells?8.f:5.f)*ImGui::GetFontSize());
  }
  return {minimum_width(n,enabled),std::ceil(height+chrome_height(n,enabled))+1.f};
 }
 // Resolve scrollbar space before drawing (or previewing) the tree. Waiting
 // for last frame's scrollbar width causes compact rows to oscillate on resize.
 ImVec2 canvas_size(const Node &n,const Enabled &enabled,ImVec2 viewport) const {
  bool horizontal=false,vertical=false;
  ImVec2 canvas=viewport;
  for(int i=0;i<3;++i) {
   const float w=std::max(1.f,viewport.x-(vertical?ImGui::GetStyle().ScrollbarSize:0));
   const float h=std::max(1.f,viewport.y-(horizontal?ImGui::GetStyle().ScrollbarSize:0));
   canvas.x=std::max(w,minimum_width(n,enabled));
   canvas.y=std::max(h,minimum(n,enabled,canvas.x).y);
   horizontal=horizontal || canvas.x>w+.5f; vertical=vertical || canvas.y>h+.5f;
  }
  return canvas;
 }
 // Ancestor geometry lets a divider push a compact panel through the stack
 // without stretching it or changing unrelated siblings' heights.
 struct SplitFrame { Node *node; int child; float usable,first,lower,upper; bool fitted; };
 std::vector<SplitFrame> split_path;
 std::function<void(int)> trace_content; // Runs inside the content child for optional UI checks.
 std::function<void(int,ImVec2,ImVec2)> trace_panel; // Optional offscreen geometry checks.
 int resize_ancestor(bool trailing) const {
  for(int i=int(split_path.size())-1;i>=0;--i) {
   const auto &frame=split_path[i];
   if(frame.node->axis!=2) return -1;
   if(frame.child==(trailing?0:1)) return frame.fitted?-1:i;
  }
  return -1;
 }
 void push_fixed_divider(int ancestor,bool trailing,float delta) {
  auto &outer=split_path[ancestor];
  outer.node->ratio=(outer.first+delta)/outer.usable;
  const float growth=trailing?delta:-delta;
  for(int i=ancestor+1;i<int(split_path.size());++i) {
   auto &frame=split_path[i];
   if(!frame.fitted) frame.node->ratio=(frame.first+(frame.child==0?growth:0))/(frame.usable+growth);
  }
 }
 void draw_node(Node &n,ImVec2 pos,ImVec2 size,const Enabled &enabled,const std::function<void(int)> &draw) {
  if(!visible(n,enabled)) return;
  if(n.axis) {
   const bool a=visible(n.children[0],enabled),b=visible(n.children[1],enabled);
   if(!a||!b) { draw_node(n.children[a?0:1],pos,size,enabled,draw); return; }
   const float gap=6.f,extent=n.axis==1?size.x:size.y,usable=std::max(2.f,extent-gap);
   const float measured_first=n.axis==1?first_width(n,enabled,size.x):size.x;
   const auto min_a=minimum(n.children[0],enabled,measured_first),min_b=minimum(n.children[1],enabled,n.axis==1?size.x-6-measured_first:size.x);
   float lower=n.axis==1?min_a.x:min_a.y,upper=n.axis==1?min_b.x:min_b.y;
   const bool space_limited=lower+upper>=usable-.5f;
   if(lower+upper>usable) { const float fit=usable/(lower+upper); lower*=fit; upper*=fit; }
   upper=std::max(lower,usable-upper);
   const float fixed_a=n.axis==2?fitted_height(n.children[0],enabled,size.x):0;
   const float fixed_b=n.axis==2?fitted_height(n.children[1],enabled,size.x):0;
   const bool fitted_split=fixed_a>0 || fixed_b>0;
   const float desired=fixed_a>0?fixed_a:fixed_b>0?usable-fixed_b:usable*n.ratio;
   const float first=std::clamp(desired,lower,upper);
   ImVec2 as=size,bs=size,bp=pos,handle=pos,hs=size;
   if(n.axis==1) { as.x=first; bs.x=usable-first; bp.x+=first+gap; handle.x+=first; hs.x=gap; }
   else { as.y=first; bs.y=usable-first; bp.y+=first+gap; handle.y+=first; hs.y=gap; }
   split_path.push_back({&n,0,usable,first,lower,upper,fitted_split});
   draw_node(n.children[0],pos,as,enabled,draw);
   split_path.back().child=1;
   draw_node(n.children[1],bp,bs,enabled,draw);
   split_path.pop_back();
   ImGui::SetCursorScreenPos(handle); ImGui::PushID(n.id);
   ImGui::InvisibleButton("Split",hs);
   const bool trailing=fixed_b>0;
   const int ancestor=fitted_split && !(fixed_a>0 && fixed_b>0)?resize_ancestor(trailing):-1;
   float move_min=0,move_max=0;
   if(ancestor>=0) {
    const auto &outer=split_path[ancestor];
    move_min=outer.lower-outer.first; move_max=outer.upper-outer.first;
    // The flexible neighbour must also retain its minimum height.
    if(trailing) move_min=std::max(move_min,min_a.y-as.y);
    else move_max=std::min(move_max,bs.y-min_b.y);
   }
   const bool pushed_resize=ancestor>=0 && move_max-move_min>.5f;
   const bool can_resize=(fitted_split?pushed_resize:!space_limited) && (editing || !dividers_locked);
   if(can_resize && ImGui::IsItemActive()) {
    if(fitted_split) {
     const float delta=std::clamp(ImGui::GetIO().MouseDelta.y,move_min,move_max);
     push_fixed_divider(ancestor,trailing,delta);
    } else n.ratio=std::clamp(((n.axis==1?ImGui::GetIO().MousePos.x-pos.x:ImGui::GetIO().MousePos.y-pos.y))/usable,lower/usable,upper/usable);
   }
   if(can_resize && ImGui::IsItemDeactivated()) dirty=true;
   if(can_resize && ImGui::IsItemHovered()) ImGui::SetMouseCursor(n.axis==1?ImGuiMouseCursor_ResizeEW:ImGuiMouseCursor_ResizeNS);
   if(editing || (can_resize && (ImGui::IsItemHovered() || ImGui::IsItemActive()))) ImGui::GetWindowDrawList()->AddRectFilled(handle,{handle.x+hs.x,handle.y+hs.y},ImGui::GetColorU32(ImGui::IsItemHovered()?ImGuiCol_SeparatorHovered:ImGuiCol_Separator));
   if(!can_resize && (editing || ImGui::IsItemHovered()) && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel)) {
    const ImVec2 c(handle.x+hs.x*.5f,handle.y+hs.y*.5f);
    // The badge extends beyond the divider; child panels must not paint over it.
    auto *d=ImGui::GetForegroundDrawList(); const auto ink=ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const float scale=std::max(1.f,ImGui::GetFontSize()/18.f);
    d->AddRectFilled({c.x-12*scale,c.y-14*scale},{c.x+12*scale,c.y+11*scale},ImGui::GetColorU32(ImGuiCol_WindowBg),3*scale);
    d->AddRect({c.x-4.5f*scale,c.y-12*scale},{c.x+4.5f*scale,c.y},ink,3*scale,0,2*scale);
    d->AddRectFilled({c.x-9*scale,c.y-3*scale},{c.x+9*scale,c.y+9*scale},ink,2*scale);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",!editing && dividers_locked?
     "Dividers are locked. Unlock them in Layout or enter Customize layout.":fitted_split?
     "This fixed-height panel cannot move farther here: there is no flexible space beyond it. Enlarge the area or move another panel.":space_limited?
     "These panels are at their minimum sizes. Enlarge this area, hide a panel, or move one elsewhere to free space.":
     "Dividers are locked. Unlock them in Layout or enter Customize layout.");
   }
   ImGui::PopID(); return;
  }
  if(float height=fitted_height(n,enabled,size.x); height>0) size.y=std::min(size.y,height);
  if(trace_panel) for(int p:n.tabs) if(enabled(p)) trace_panel(p,pos,size);
  ImGui::SetCursorScreenPos(pos); ImGui::PushID(n.id);
  ImGui::BeginChild("Pane",size,ImGuiChildFlags_Borders,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
  std::vector<int> tabs; for(int p:n.tabs) if(enabled(p)) tabs.push_back(p);
  if(std::find(tabs.begin(),tabs.end(),n.active)==tabs.end()) n.active=tabs.front();
  if(editing || tabs.size()>1) {
   // A scrolling tab strip keeps small panes usable and gives every panel a drag handle.
   if(ImGui::BeginTabBar("Panels",ImGuiTabBarFlags_FittingPolicyScroll)) {
    for(int p:tabs) {
     bool selected=ImGui::BeginTabItem(names[p],nullptr,p==n.active?ImGuiTabItemFlags_SetSelected:ImGuiTabItemFlags_None);
     if(ImGui::IsItemClicked()) { n.active=p; dirty=true; }
     source(p);
     if(editing && p!=Dungeon && ImGui::BeginPopupContextItem()) {
      if(native_windows_available && detachable(p) && ImGui::MenuItem("Detach into new window")) detach(p,true);
      if(ImGui::MenuItem("Float inside window")) pending={p,0,5};
      if(ImGui::MenuItem("Hide panel")) pending={p,0,6};
      ImGui::EndPopup();
     }
     if(selected) ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
   }
  }
  // Gameplay controls are disabled during editing; layout controls remain active.
  const auto guides_pos=ImGui::GetCursorScreenPos();
  const float natural=panel_height(n.active,size.x);
  const bool clipped=natural>ImGui::GetContentRegionAvail().y+.5f;
  ImGui::BeginChild("Content",ImVec2(0,0),ImGuiChildFlags_None,!clipped && (n.active==Dungeon||n.active==Map||natural>0)?ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse:ImGuiWindowFlags_None);
  ImGui::BeginDisabled(editing); draw(n.active); ImGui::EndDisabled();
  if(trace_content) trace_content(n.active);
  ImGui::EndChild();
  // Docking controls overlay disabled content so starting a drag cannot push
  // every target away or suddenly make the workspace taller.
  if(editing && ImGui::GetDragDropPayload()) {
   ImGui::SetCursorScreenPos(guides_pos);
   ImGui::PushStyleColor(ImGuiCol_ChildBg,ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
   ImGui::BeginChild("Dock guides",{0,ImGui::GetTextLineHeightWithSpacing()+ImGui::GetFrameHeightWithSpacing()},ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
   ImGui::TextDisabled("Dock here");
   const char *labels[]={"Tabs","Left","Right","Above","Below"};
   for(int e=0;e<5;++e) {
    if(e) ImGui::SameLine();
    ImGui::SmallButton(labels[e]); target(n.id,e,pos,size,enabled);
   }
   ImGui::EndChild(); ImGui::PopStyleColor();
  }
  ImGui::EndChild(); ImGui::PopID();
 }
 void draw(const Enabled &enabled,const std::function<void(int)> &panel) {
  if(editing && !history_ready) { edit_initial=edit_checkpoint=snapshot(); history_ready=true; undo_steps.clear(); redo_steps.clear(); }
  if(editing && !ImGui::IsMouseDown(0) && !ImGui::GetDragDropPayload()) {
   record_edit();
   if(!ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl) {
    if(ImGui::IsKeyPressed(ImGuiKey_Z,false)) { if(ImGui::GetIO().KeyShift) redo_edit(); else undo_edit(); }
    else if(ImGui::IsKeyPressed(ImGuiKey_Y,false)) redo_edit();
   }
  }
  if(editing) {
   ImGui::TextColored(ImVec4(.5f,.9f,.7f,1),"LAYOUT EDITOR"); ImGui::SameLine();
   if(ImGui::Button("Done")) finish_edit();
   ImGui::SameLine(); if(ImGui::Button("Cancel")) finish_edit(true);
   ImGui::SameLine(); ImGui::BeginDisabled(undo_steps.empty());
   if(ImGui::Button("Undo")) undo_edit();
   if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Undo the last layout change (Ctrl+Z)");
   ImGui::EndDisabled(); ImGui::SameLine(); ImGui::BeginDisabled(redo_steps.empty());
   if(ImGui::Button("Redo")) redo_edit();
   if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Redo the last undone change (Ctrl+Y or Ctrl+Shift+Z)");
   ImGui::EndDisabled();
   ImGui::TextWrapped("Drag panel tabs onto docking guides. Drag dividers to resize. Right-click a tab to float or hide it. Use Layout to bring panels back.");
  }
  const ImVec2 pos=ImGui::GetCursorScreenPos(),size=ImGui::GetContentRegionAvail();
  if(size.x<4||size.y<4) return;
  workspace_view_pos=workspace_pos=pos; workspace_view_size=workspace_size=size; dock_preview.active=false;
  const auto canvas=canvas_size(root,enabled,size);
  if(canvas.x>size.x+.5f || canvas.y>size.y+.5f) {
   // Preserve content minima when the window or a new dock arrangement cannot
   // fit. Scrolling is preferable to silently compressing unscrollable panels.
   ImGui::SetNextWindowContentSize(canvas);
   ImGui::BeginChild("Workspace overflow",size,ImGuiChildFlags_None,ImGuiWindowFlags_HorizontalScrollbar);
   const auto origin=ImGui::GetCursorScreenPos();
   workspace_pos=origin; workspace_size=canvas;
   draw_node(root,origin,canvas,enabled,panel);
   ImGui::SetCursorScreenPos(origin); ImGui::Dummy(canvas);
   ImGui::EndChild();
  } else draw_node(root,pos,size,enabled,panel);
  for(auto &f:floating) {
   if(!visible(f.node,enabled)) continue;
   float w=std::clamp(f.w*size.x,std::min(size.x,ImGui::GetFontSize()*16),size.x);
   float h=std::clamp(f.h*size.y,std::min(size.y,ImGui::GetFontSize()*8),size.y);
   w=std::min(size.x,std::max(w,minimum_width(f.node,enabled)+2*ImGui::GetStyle().WindowPadding.x));
   const float inner_width=std::max(1.f,w-2*ImGui::GetStyle().WindowPadding.x);
   const float floating_chrome=2*ImGui::GetStyle().WindowPadding.y+
    (editing||!floating_locked?ImGui::GetFrameHeightWithSpacing():0)+(editing?ImGui::GetFrameHeightWithSpacing():0);
   h=std::min(size.y,std::max(h,minimum(f.node,enabled,inner_width).y+floating_chrome));
   const float fitted=fitted_height(f.node,enabled,inner_width);
   if(fitted>0) h=std::min(size.y,fitted+2*ImGui::GetStyle().WindowPadding.y+
    (editing||!floating_locked?ImGui::GetFrameHeightWithSpacing():0)+(editing?ImGui::GetFrameHeightWithSpacing():0));
   float x=std::clamp(f.x*size.x,0.f,size.x-w),y=std::clamp(f.y*size.y,0.f,size.y-h);
   // Floating panels are sibling child windows, so their chrome and contents
   // are above the docked panes and capture clicks instead of passing them through.
   ImGui::SetCursorScreenPos({pos.x+x,pos.y+y}); ImGui::PushID(f.node.id);
   ImGui::BeginChild("Floating",{w,h},ImGuiChildFlags_Borders,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
   if(editing || !floating_locked) {
    const std::string title=editing?"Move floating panel":(!f.node.axis && f.node.active>=0?names[f.node.active]:"Floating panels");
    ImGui::Button((title+"###Move floating panel").c_str(),{std::max(1.f,ImGui::GetContentRegionAvail().x),0});
    if(ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) { x=std::clamp(x+ImGui::GetIO().MouseDelta.x,0.f,size.x-w); y=std::clamp(y+ImGui::GetIO().MouseDelta.y,0.f,size.y-h); }
    if(ImGui::IsItemDeactivated()) dirty=true;
    if(ImGui::IsItemHovered()) { ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll); ImGui::SetTooltip("Drag to move this floating panel"); }
   }
   auto content_pos=ImGui::GetCursorScreenPos(),content_size=ImGui::GetContentRegionAvail();
   if(editing) content_size.y=std::max(1.f,content_size.y-ImGui::GetFrameHeightWithSpacing());
   floating_preview_pos=content_pos; floating_preview_size=content_size;
   draw_node(f.node,content_pos,content_size,enabled,panel);
   if(editing) {
    const char *resize_label=fitted>0?"Width":"Resize";
    const float button_width=ImGui::CalcTextSize(resize_label).x+2*ImGui::GetStyle().FramePadding.x;
    ImGui::SetCursorScreenPos({content_pos.x+std::max(0.f,content_size.x-button_width),content_pos.y+content_size.y+ImGui::GetStyle().ItemSpacing.y});
    ImGui::Button(resize_label,{button_width,0});
    if(ImGui::IsItemActive()) { w=std::clamp(w+ImGui::GetIO().MouseDelta.x,std::min(size.x-x,ImGui::GetFontSize()*16),size.x-x); if(fitted<=0) h=std::clamp(h+ImGui::GetIO().MouseDelta.y,std::min(size.y-y,ImGui::GetFontSize()*8),size.y-y); dirty=true; }
   }
   ImGui::EndChild(); ImGui::PopID();
   f.x=x/size.x; f.y=y/size.y; f.w=w/size.x; if(fitted<=0) f.h=h/size.y;
  }
  ImGui::SetCursorScreenPos(pos); ImGui::Dummy(size);
  if(pending.panel>=0) { auto action=pending; pending={}; move(action); }
  if(editing && !ImGui::IsMouseDown(0) && !ImGui::GetDragDropPayload()) record_edit();
  draw_dock_preview();
 }
};
