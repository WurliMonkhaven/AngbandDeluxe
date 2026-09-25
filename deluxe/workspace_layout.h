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
 enum Panel { Dungeon,Character,Messages,Inventory,Spells,Creatures,Map,Commands,More,Target,Quickbar,DungeonDetails,Count };
 inline static constexpr const char *names[]={"Dungeon","Character","Messages","Inventory","Spells","Creatures","Map","Commands","More","Look / Target","Quickbar","Dungeon details"};
 struct Node {
  int id=0,axis=0; float ratio=.5f; // axis: 0 tabs, 1 left/right, 2 top/bottom
  std::vector<int> tabs; int active=-1;
  std::vector<Node> children;
 };
 struct Floating { Node node; float x=.15f,y=.15f,w=.36f,h=.4f; };
 struct Detached { bool open=false; int x=0,y=0,w=560,h=640; bool placed=false; };
 std::array<Detached,Count> detached{};
 bool native_windows_available=false;
 static bool detachable(int p) { return p==Inventory || p==Messages || p==Map || p==Character || p==DungeonDetails; }
 void detach(int p,bool open) { if(!detachable(p)) return; if(open || contains(p)) reveal(p); detached[p].open=open; dirty=true; }
 Node root; std::vector<Floating> floating;
 Json saved=Json::object(),before;
 bool editing=false,dirty=false,dividers_locked=true,floating_locked=false; int next_id=1;
 float quickbar_content_height=0; // Supplied by the quickbar renderer, in current UI pixels.
 char save_name[65]{};
 struct Move { int panel=-1,target=0,edge=0; }; // 0 tabs, 1 left, 2 right, 3 top, 4 bottom, 5 float, 6 hide
 Move pending;
 Node leaf(std::initializer_list<int> tabs) { Node n; n.id=next_id++; n.tabs=tabs; if(!n.tabs.empty()) n.active=n.tabs[0]; return n; }
 Node split(int axis,float ratio,Node a,Node b) { Node n; n.id=next_id++; n.axis=axis; n.ratio=ratio; n.children={std::move(a),std::move(b)}; return n; }
 Node overview() { return split(2,.70f,leaf({Character}),leaf({DungeonDetails})); }
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
   auto side=split(2,.44f,overview(),split(2,.55f,std::move(inventory),leaf({Spells})));
   root=split(1,.68f,std::move(play),std::move(side));
  } else {
   auto tools=leaf({Inventory,Spells,Creatures,Map,Commands,More,Target});
   auto play=split(2,.75f,split(2,.88f,leaf({Dungeon}),leaf({Quickbar})),leaf({Messages}));
   auto side=split(2,.46f,overview(),std::move(tools));
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
  Json j={{"version",2},{"root",encode(root)},{"floating",Json::array()}};
  for(const auto &f:floating) j["floating"].push_back({{"node",encode(f.node)},{"x",f.x},{"y",f.y},{"w",f.w},{"h",f.h}});
  j["detached"]=Json::array();
  for(int p=0;p<Count;++p) if(detached[p].placed || detached[p].open) { const auto &d=detached[p]; j["detached"].push_back({{"panel",p},{"open",d.open},{"placed",d.placed},{"x",d.x},{"y",d.y},{"w",d.w},{"h",d.h}}); }
  return j;
 }
 Json serialize() const { return {{"version",2},{"dividers_locked",dividers_locked},{"floating_locked",floating_locked},{"current",editing?before:arrangement()},{"saved",saved}}; }
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
   return true;
  } catch(...) { return false; }
 }
 void load(const Json &j) {
  if(!j.is_object() || j.value("version",0)<1 || j.value("version",0)>2) return;
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
    if(editing) { editing=false; dirty=true; }
    else { before=arrangement(); editing=true; }
   }
   if(ImGui::MenuItem("Lock dividers",nullptr,dividers_locked)) { dividers_locked=!dividers_locked; dirty=true; }
   if(ImGui::IsItemHovered()) ImGui::SetTooltip("Unlock to resize panel dividers during normal play. Customization always allows resizing.");
   if(ImGui::MenuItem("Lock floating panels",nullptr,floating_locked)) { floating_locked=!floating_locked; dirty=true; }
   if(ImGui::IsItemHovered()) ImGui::SetTooltip("Floating panels can be dragged by their title bar during play unless locked. Customization always allows moving them.");
   ImGui::SeparatorText("Starting layouts");
   const char *presets[]={"Classic","Dungeon first","Command centre"};
   const char *descriptions[]={"Dungeon, messages and the familiar tabbed sidebar.","Large dungeon, character overview and messages. Optional panels hidden.","Separate inventory, map and messages, plus spells when available."};
   for(int i=0;i<3;++i) {
    if(ImGui::MenuItem(presets[i])) preset(i);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",descriptions[i]);
   }
   if(!saved.empty()) {
    ImGui::SeparatorText("Saved layouts");
    for(auto it=saved.begin();it!=saved.end();++it) if(ImGui::MenuItem(it.key().c_str())) { restore(it.value()); dirty=true; }
   }
   ImGui::Separator();
   if(ImGui::BeginMenu("Panels")) {
    ImGui::TextDisabled("Checked panels are included. Click to show or hide.");
    ImGui::Separator();
    for(int p=Character;p<Count;++p) {
     const bool usable=enabled(p);
     if(ImGui::MenuItem(names[p],nullptr,usable && contains(p),usable)) toggle(p);
     if(!usable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("This panel is unavailable in the current game state or gameplay settings.");
    }
    ImGui::EndMenu();
   }
   if(native_windows_available && ImGui::BeginMenu("Detached windows",!editing)) {
    for(int p=0;p<Count;++p) if(detachable(p) && enabled(p))
     if(ImGui::MenuItem(names[p],nullptr,detached[p].open)) detach(p,!detached[p].open);
    ImGui::Separator();
    if(ImGui::MenuItem("Return all panels")) { for(auto &d:detached) d.open=false; dirty=true; }
    ImGui::EndMenu();
   }
   ImGui::InputTextWithHint("##layout name","Layout name",save_name,sizeof(save_name));
   ImGui::BeginDisabled(save_name[0]==0 || (saved.size()>=32 && !saved.contains(save_name)));
   if(ImGui::Button("Save named layout")) { saved[save_name]=arrangement(); dirty=true; save_name[0]=0; }
   ImGui::EndDisabled();
   if(ImGui::BeginMenu("Delete saved layout",!saved.empty())) {
    std::string erase;
    for(auto it=saved.begin();it!=saved.end();++it) if(ImGui::MenuItem(it.key().c_str())) erase=it.key();
    if(!erase.empty()) { saved.erase(erase); dirty=true; }
    ImGui::EndMenu();
   }
   if(ImGui::MenuItem("Reset layout")) preset(0);
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
 void target(int id,int edge) {
  if(ImGui::BeginDragDropTarget()) {
   if(const auto *p=ImGui::AcceptDragDropPayload("DELUXE_PANEL")) pending={*static_cast<const int*>(p->Data),id,edge};
   ImGui::EndDragDropTarget();
  }
 }
 // A dedicated quickbar has a natural height, not a share of a split.
 // Mixed tab groups remain flexible because their other panels need the space.
 float fitted_height(const Node &n,const Enabled &enabled) const {
  if(!visible(n,enabled)) return 0;
  if(n.axis) {
   bool a=visible(n.children[0],enabled),b=visible(n.children[1],enabled);
   if(!a || !b) return fitted_height(n.children[a?0:1],enabled);
   float ah=fitted_height(n.children[0],enabled),bh=fitted_height(n.children[1],enabled);
   return ah>0 && bh>0?(n.axis==2?ah+bh+6:std::max(ah,bh)):0;
  }
  int count=0,panel=-1;
  for(int p:n.tabs) if(enabled(p)) { ++count; panel=p; }
  if(count!=1 || panel!=Quickbar || quickbar_content_height<=0) return 0;
  float height=quickbar_content_height+2*ImGui::GetStyle().WindowPadding.y;
  if(editing) {
   height+=ImGui::GetFrameHeightWithSpacing();
   if(ImGui::GetDragDropPayload()) height+=ImGui::GetTextLineHeightWithSpacing()+ImGui::GetFrameHeightWithSpacing();
  }
  return height;
 }
 ImVec2 minimum(const Node &n,const Enabled &enabled) const {
  if(!visible(n,enabled)) return {0,0};
  if(n.axis) {
   auto a=minimum(n.children[0],enabled),b=minimum(n.children[1],enabled);
   if(a.x==0) return b; if(b.x==0) return a;
   return n.axis==1?ImVec2(a.x+b.x+6,std::max(a.y,b.y)):ImVec2(std::max(a.x,b.x),a.y+b.y+6);
  }
  if(float height=fitted_height(n,enabled); height>0) return {22*ImGui::GetFontSize(),height};
  float width=10,height=5;
  for(int p:n.tabs) if(enabled(p)) {
   width=std::max(width,p==Character?20.f:p==Dungeon||p==Quickbar?22.f:p==Inventory||p==Spells?18.f:14.f);
   height=std::max(height,p==Dungeon?10.f:p==Character?12.f:p==Inventory||p==Spells?8.f:5.f);
  }
  return {width*ImGui::GetFontSize(),height*ImGui::GetFontSize()+(editing?ImGui::GetFrameHeightWithSpacing():0)};
 }
 void draw_node(Node &n,ImVec2 pos,ImVec2 size,const Enabled &enabled,const std::function<void(int)> &draw) {
  if(!visible(n,enabled)) return;
  if(n.axis) {
   const bool a=visible(n.children[0],enabled),b=visible(n.children[1],enabled);
   if(!a||!b) { draw_node(n.children[a?0:1],pos,size,enabled,draw); return; }
   const float gap=6.f,extent=n.axis==1?size.x:size.y,usable=std::max(2.f,extent-gap);
   const auto min_a=minimum(n.children[0],enabled),min_b=minimum(n.children[1],enabled);
   float lower=n.axis==1?min_a.x:min_a.y,upper=n.axis==1?min_b.x:min_b.y;
   if(lower+upper>usable) { const float fit=usable/(lower+upper); lower*=fit; upper*=fit; }
   upper=std::max(lower,usable-upper);
   const float fixed_a=n.axis==2?fitted_height(n.children[0],enabled):0;
   const float fixed_b=n.axis==2?fitted_height(n.children[1],enabled):0;
   const bool fitted_split=fixed_a>0 || fixed_b>0;
   const float desired=fixed_a>0?fixed_a:fixed_b>0?usable-fixed_b:usable*n.ratio;
   const float first=std::clamp(desired,lower,upper);
   ImVec2 as=size,bs=size,bp=pos,handle=pos,hs=size;
   if(n.axis==1) { as.x=first; bs.x=usable-first; bp.x+=first+gap; handle.x+=first; hs.x=gap; }
   else { as.y=first; bs.y=usable-first; bp.y+=first+gap; handle.y+=first; hs.y=gap; }
   draw_node(n.children[0],pos,as,enabled,draw); draw_node(n.children[1],bp,bs,enabled,draw);
   ImGui::SetCursorScreenPos(handle); ImGui::PushID(n.id);
   ImGui::InvisibleButton("Split",hs);
   const bool can_resize=!fitted_split && (editing || !dividers_locked);
   if(can_resize && ImGui::IsItemActive()) { n.ratio=std::clamp(((n.axis==1?ImGui::GetIO().MousePos.x-pos.x:ImGui::GetIO().MousePos.y-pos.y))/usable,lower/usable,upper/usable); }
   if(can_resize && ImGui::IsItemDeactivated()) dirty=true;
   if(can_resize && ImGui::IsItemHovered()) ImGui::SetMouseCursor(n.axis==1?ImGuiMouseCursor_ResizeEW:ImGuiMouseCursor_ResizeNS);
   if(editing || (can_resize && (ImGui::IsItemHovered() || ImGui::IsItemActive()))) ImGui::GetWindowDrawList()->AddRectFilled(handle,{handle.x+hs.x,handle.y+hs.y},ImGui::GetColorU32(ImGui::IsItemHovered()?ImGuiCol_SeparatorHovered:ImGuiCol_Separator));
   ImGui::PopID(); return;
  }
  if(float height=fitted_height(n,enabled); height>0) size.y=std::min(size.y,height);
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
  if(editing && ImGui::GetDragDropPayload()) {
   ImGui::TextDisabled("Dock here");
   const char *labels[]={"Tabs","Left","Right","Above","Below"};
   for(int e=0;e<5;++e) {
    if(e) ImGui::SameLine();
    ImGui::SmallButton(labels[e]); target(n.id,e);
   }
  }
  // Gameplay controls are disabled during editing; layout controls remain active.
  ImGui::BeginChild("Content",ImVec2(0,0),ImGuiChildFlags_None,n.active==Dungeon||n.active==Map||n.active==Quickbar?ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse:ImGuiWindowFlags_None);
  ImGui::BeginDisabled(editing); draw(n.active); ImGui::EndDisabled();
  ImGui::EndChild(); ImGui::EndChild(); ImGui::PopID();
 }
 void draw(const Enabled &enabled,const std::function<void(int)> &panel) {
  if(editing) {
   ImGui::TextColored(ImVec4(.5f,.9f,.7f,1),"LAYOUT EDITOR"); ImGui::SameLine();
   if(ImGui::Button("Done")) { editing=false; dirty=true; }
   ImGui::SameLine(); if(ImGui::Button("Cancel")) { restore(before); editing=false; dirty=true; }
   ImGui::TextWrapped("Drag panel tabs onto docking guides. Drag dividers to resize. Right-click a tab to float or hide it. Use Layout to bring panels back.");
  }
  const ImVec2 pos=ImGui::GetCursorScreenPos(),size=ImGui::GetContentRegionAvail();
  if(size.x<4||size.y<4) return;
  draw_node(root,pos,size,enabled,panel);
  for(auto &f:floating) {
   if(!visible(f.node,enabled)) continue;
   float w=std::clamp(f.w*size.x,std::min(size.x,ImGui::GetFontSize()*16),size.x);
   float h=std::clamp(f.h*size.y,std::min(size.y,ImGui::GetFontSize()*8),size.y);
   const float fitted=fitted_height(f.node,enabled);
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
   draw_node(f.node,content_pos,content_size,enabled,panel);
   if(editing) {
    const char *resize_label=fitted>0?"Width":"Resize";
    const float button_width=ImGui::CalcTextSize(resize_label).x+2*ImGui::GetStyle().FramePadding.x;
    ImGui::SetCursorScreenPos({content_pos.x+std::max(0.f,content_size.x-button_width),content_pos.y+content_size.y+ImGui::GetStyle().ItemSpacing.y});
    ImGui::Button(resize_label,{button_width,0});
    if(ImGui::IsItemActive()) { w=std::clamp(w+ImGui::GetIO().MouseDelta.x,std::min(size.x-x,ImGui::GetFontSize()*16),size.x-x); if(fitted<=0) h=std::clamp(h+ImGui::GetIO().MouseDelta.y,std::min(size.y-y,ImGui::GetFontSize()*8),size.y-y); dirty=true; }
   }
   ImGui::EndChild(); ImGui::PopID();
   f.x=x/size.x; f.y=y/size.y; f.w=w/size.x; f.h=h/size.y;
  }
  ImGui::SetCursorScreenPos(pos); ImGui::Dummy(size);
  if(pending.panel>=0) { auto action=pending; pending={}; move(action); }
 }
};
