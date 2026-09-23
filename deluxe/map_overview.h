// Read-only floor overview. This component has no connection or command access.
struct MapOverview {
 float zoom=1.f;
 ImVec2 centre{};
 bool fitted=true;
 std::string level;
 int columns=0,rows=0;
 static ImVec2 world_at(ImVec2 mouse,ImVec2 origin,ImVec2 size,ImVec2 centre,float cell) {
  return ImVec2(centre.x+(mouse.x-origin.x-size.x*.5f)/cell,centre.y+(mouse.y-origin.y-size.y*.5f)/cell);
 }
 void zoom_at(float next,ImVec2 mouse,ImVec2 origin,ImVec2 size,float base) {
  const auto anchor=world_at(mouse,origin,size,centre,base*zoom);
  zoom=std::clamp(next,1.f,24.f); fitted=false;
  const auto after=world_at(mouse,origin,size,centre,base*zoom);
  centre.x+=anchor.x-after.x; centre.y+=anchor.y-after.y;
 }
 static ImU32 ink(const std::string &kind) {
  return kind=="up"?IM_COL32(95,205,255,255):kind=="down"?IM_COL32(255,193,85,255):kind=="shop"?IM_COL32(218,141,255,255):IM_COL32(245,250,255,255);
 }
 static void marker(ImDrawList *draw,ImVec2 p,float r,const std::string &kind) {
  draw->AddCircleFilled(p,r+2,IM_COL32(8,13,20,235),12);
  if(kind=="shop") draw->AddRectFilled(ImVec2(p.x-r,p.y-r),ImVec2(p.x+r,p.y+r),ink(kind),1);
  else if(kind=="player") {
   draw->AddQuadFilled(ImVec2(p.x,p.y-r),ImVec2(p.x+r,p.y),ImVec2(p.x,p.y+r),ImVec2(p.x-r,p.y),ink(kind));
  } else {
   const float direction=kind=="up"?-1.f:1.f;
   draw->AddTriangleFilled(ImVec2(p.x,p.y+direction*r),ImVec2(p.x-r,p.y-direction*r),ImVec2(p.x+r,p.y-direction*r),ink(kind));
  }
 }
 static void legend() {
  const float r=ImGui::GetFontSize()*.23f;
  const float left=ImGui::GetCursorScreenPos().x,right=left+ImGui::GetContentRegionAvail().x;
  bool first=true;
  for(const auto &entry:std::array<std::pair<const char*,const char*>,4>{{{"player","You"},{"up","Up"},{"down","Down"},{"shop","Shop"}}}) {
   const float width=ImGui::CalcTextSize(entry.second).x+4*r;
   if(!first && ImGui::GetItemRectMax().x+ImGui::GetStyle().ItemSpacing.x+width<right) ImGui::SameLine();
   auto p=ImGui::GetCursorScreenPos(); marker(ImGui::GetWindowDrawList(),ImVec2(p.x+r,p.y+ImGui::GetFontSize()*.5f),r,entry.first);
   ImGui::GetWindowDrawList()->AddText(ImVec2(p.x+3*r,p.y),ImGui::GetColorU32(ImGuiCol_TextDisabled),entry.second);
   ImGui::Dummy(ImVec2(width,ImGui::GetTextLineHeight())); first=false;
  }
 }
 bool draw(const json &state,const json &catalog) {
  if(!state.contains("map") || !state["map"].contains("known") || !catalog.contains("features")) { ImGui::TextDisabled("No map available."); return false; }
  const auto &map=state["map"]["known"],&features=catalog["features"];
  if(map.empty() || map[0].empty()) return false;
  const int w=int(map[0].size()),h=int(map.size());
  const auto new_level=state["map"].value("level_id",std::to_string(state.value("player",json::object()).value("depth",0)));
  if(level!=new_level || columns!=w || rows!=h) { level=new_level; columns=w; rows=h; fitted=true; zoom=1; }
  bool interacted=false;
  if(ImGui::Button("Fit floor")) { fitted=true; zoom=1; interacted=true; }
  const float centre_width=ImGui::CalcTextSize("Centre on player").x+2*ImGui::GetStyle().FramePadding.x;
  if(ImGui::GetContentRegionAvail().x>centre_width+ImGui::GetItemRectSize().x+ImGui::GetStyle().ItemSpacing.x) ImGui::SameLine();
  ImGui::BeginDisabled(!state.contains("player"));
  if(ImGui::Button("Centre on player")) { centre=ImVec2(state["player"].value("x",0)+.5f,state["player"].value("y",0)+.5f); fitted=false; interacted=true; }
  ImGui::EndDisabled();
  legend();
  ImGui::TextDisabled("%.0f%%",zoom*100); ImGui::SameLine(); ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)); ImGui::TextWrapped("Wheel: zoom / Drag: pan"); ImGui::PopStyleColor();
  const auto origin=ImGui::GetCursorScreenPos();
  const ImVec2 size(std::max(1.f,ImGui::GetContentRegionAvail().x),std::max(1.f,ImGui::GetContentRegionAvail().y));
  ImGui::InvisibleButton("Floor overview",size,ImGuiButtonFlags_MouseButtonLeft);
  const bool hovered=ImGui::IsItemHovered(),active=ImGui::IsItemActive();
  const float base=std::max(.01f,std::min(std::max(1.f,size.x-12)/w,std::max(1.f,size.y-12)/h));
  if(fitted) centre=ImVec2(w*.5f,h*.5f);
  if(hovered) {
   ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
   if(ImGui::GetIO().MouseWheel!=0) { zoom_at(zoom*std::pow(1.2f,ImGui::GetIO().MouseWheel),ImGui::GetIO().MousePos,origin,size,base); interacted=true; }
  }
  const float cell=base*zoom;
  if(active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
   const auto delta=ImGui::GetIO().MouseDelta; centre.x-=delta.x/cell; centre.y-=delta.y/cell; fitted=false; interacted=true;
   ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
  }
  centre.x=std::clamp(centre.x,0.f,float(w)); centre.y=std::clamp(centre.y,0.f,float(h));
  interacted|=active;
  const auto point=[&](float x,float y) { return ImVec2(origin.x+size.x*.5f+(x-centre.x)*cell,origin.y+size.y*.5f+(y-centre.y)*cell); };
  auto *draw=ImGui::GetWindowDrawList();
  draw->AddRectFilled(origin,ImVec2(origin.x+size.x,origin.y+size.y),IM_COL32(8,12,18,255));
  draw->PushClipRect(origin,ImVec2(origin.x+size.x,origin.y+size.y),true);
  struct Landmark { int x,y; std::string kind; }; std::vector<Landmark> marks;
  for(int y=0;y<h;++y) for(int x=0;x<int(map[y].size());++x) {
   const int f=map[y][x]; if(f<=0 || f>=int(features.size())) continue;
   const auto kind=features[f].value("map_kind",""); const auto p=point(float(x),float(y));
   if(p.x+cell<origin.x || p.x>origin.x+size.x || p.y+cell<origin.y || p.y>origin.y+size.y) continue;
   const ImU32 terrain=kind=="wall"?IM_COL32(67,78,94,255):kind=="door"?IM_COL32(164,119,72,255):IM_COL32(27,39,50,255);
   draw->AddRectFilled(p,ImVec2(p.x+cell,p.y+cell),terrain);
   if(kind=="up" || kind=="down" || kind=="shop") marks.push_back({x,y,kind});
  }
  if(state.contains("dungeon")) {
   const auto &view=state["dungeon"];
   const auto a=point(float(view.value("x",0)),float(view.value("y",0))),b=point(float(view.value("x",0)+view.value("width",0)),float(view.value("y",0)+view.value("height",0)));
   draw->AddRect(a,b,IM_COL32(122,172,212,180),0,0,1.5f);
  }
  const float radius=std::max(ImGui::GetFontSize()*.24f,std::min(cell*.42f,ImGui::GetFontSize()*.5f));
  auto at=world_at(ImGui::GetIO().MousePos,origin,size,centre,cell); int hx=int(std::floor(at.x)),hy=int(std::floor(at.y));
  float nearest=(radius+2)*(radius+2);
  for(const auto &mark:marks) {
   const auto p=point(mark.x+.5f,mark.y+.5f); marker(draw,p,radius,mark.kind);
   const float dx=ImGui::GetIO().MousePos.x-p.x,dy=ImGui::GetIO().MousePos.y-p.y;
   if(dx*dx+dy*dy<nearest) { nearest=dx*dx+dy*dy; hx=mark.x; hy=mark.y; }
  }
  if(state.contains("player")) { const auto &p=state["player"]; marker(draw,point(p.value("x",0)+.5f,p.value("y",0)+.5f),radius+1,"player"); }
  draw->PopClipRect();
  draw->AddRect(origin,ImVec2(origin.x+size.x,origin.y+size.y),ImGui::GetColorU32(ImGuiCol_Border));
  if(hovered && !active && hx>=0 && hy>=0 && hy<h && hx<int(map[hy].size())) {
   const int f=map[hy][hx];
   ImGui::BeginTooltip(); ImGui::PushTextWrapPos(ImGui::GetFontSize()*25);
   if(state.contains("player") && hx==state["player"].value("x",-1) && hy==state["player"].value("y",-1)) ImGui::TextUnformatted("You are here");
   ImGui::TextUnformatted(f>0 && f<int(features.size())?display_label(features[f].value("name","")).c_str():"Unexplored");
   ImGui::TextDisabled("%d, %d",hx,hy); ImGui::TextDisabled("Outline: main dungeon view");
   ImGui::PopTextWrapPos(); ImGui::EndTooltip();
  }
  return interacted;
 }
};
