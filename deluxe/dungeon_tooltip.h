// Delayed local presentation; never sends a command, changes tracking or rolls RNG.
struct DungeonTooltip {
 std::string context;
 int x=-1,y=-1;
 double since=0;
 json content;
 void reset() { context.clear(); x=y=-1; content=nullptr; }
 bool dwell(bool eligible,const std::string &current,int tx,int ty,double now) {
  if(!eligible) { reset(); return false; }
  if(context!=current || x!=tx || y!=ty) {
   context=current; x=tx; y=ty; since=now; content=nullptr; return false;
  }
  return now-since>=.55;
 }
 static json describe_tile(const json &state,const json &catalog,int x,int y) {
  const auto &view=state.at("dungeon");
  const int vx=x-view.value("x",0),vy=y-view.value("y",0);
  if(vx<0 || vy<0 || vx>=view.value("width",0) || vy>=view.value("height",0)) return nullptr;
  const auto &cell=view.at("cells")[vy][vx];
  const bool seen=cell[10].get<int>()!=0,hallucinating=cell[11].get<int>()!=0;
  if(cell[8]==0 && cell[4]==0 && cell[6]==0 && cell[12]==0) return nullptr;
  json result={{"terrain","Unknown terrain"},{"seen",seen},{"items",json::array()},{"statuses",json::array()}};
  if(catalog.contains("features")) for(const auto &f:catalog["features"])
   if(f.value("id",-1)==cell[8].get<int>()) { result["terrain"]=display_label(f.value("name","Unknown terrain")); break; }
  if(hallucinating) { result["hallucinating"]=true; return result; }
  if(cell[12].get<int>() && state.contains("player")) {
   const auto &p=state["player"]; auto name=p.value("name","");
   result["name"]=name.empty()?"You":name; result["subtitle"]=p.value("race","")+" "+p.value("class","");
   result["hp"]=p.value("hp",0); result["max_hp"]=p.value("max_hp",0);
   if(p.contains("statuses")) for(const auto &status:p["statuses"])
    if(status.value("visible",false)) result["statuses"].push_back(status.value("name",""));
  } else if(state.contains("monsters")) for(const auto &m:state["monsters"])
   if(m.value("visible",false) && m.value("x",-1)==x && m.value("y",-1)==y) {
    result["name"]=display_label(m.value("name","Creature")); result["color"]=m.value("color",1);
    result["hp"]=m.value("hp",0); result["max_hp"]=m.value("max_hp",0);
    result["subtitle"]=display_label(m.value("condition",m.value("asleep",false)?"Asleep":"")); break;
   }
  if(cell[2].get<int>()) result["trap"]=true;
  if(view.contains("items")) for(const auto &item:view["items"])
   if(item.value("x",-1)==x && item.value("y",-1)==y) result["items"].push_back(item);
  if(cell[4].get<int>() && result["items"].empty()) result["unresolved_item"]=true;
  return result;
 }
 void draw(const json &state,const json &catalog) {
  if(content.is_null()) content=describe_tile(state,catalog,x,y);
  if(content.is_null()) return;
  const float width=std::min(ImGui::GetFontSize()*23,ImGui::GetMainViewport()->WorkSize.x-24);
  ImGui::SetNextWindowSizeConstraints(ImVec2(0,0),ImVec2(width+20,FLT_MAX));
  // ImGui tooltips are non-interactive and clamp to the viewport by the cursor.
  ImGui::BeginTooltip(); ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+width);
  if(content.contains("name")) {
   ImGui::PushStyleColor(ImGuiCol_Text,color(std::max(1,content.value("color",1))));
   ImGui::TextWrapped("%s",content["name"].get_ref<const std::string&>().c_str()); ImGui::PopStyleColor();
   const auto subtitle=content.value("subtitle",""); if(!subtitle.empty()) ImGui::TextWrapped("%s",subtitle.c_str());
   const int hp=std::max(0,content.value("hp",0)),max_hp=content.value("max_hp",0);
   if(max_hp>0) {
    const auto label="HP "+std::to_string(hp)+" / "+std::to_string(max_hp);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram,ImVec4(.60f,.18f,.20f,1));
    ImGui::ProgressBar(resource_fraction(hp,max_hp),ImVec2(width,ImGui::GetTextLineHeight()+4),label.c_str());
    ImGui::PopStyleColor();
   }
   int count=0; for(const auto &status:content["statuses"]) { if(count++==3) { ImGui::TextDisabled("More effects in character details"); break; } ImGui::TextWrapped("%s",status.get_ref<const std::string&>().c_str()); }
   ImGui::Separator();
  }
  ImGui::TextWrapped("%s",content["terrain"].get_ref<const std::string&>().c_str());
  if(!content.value("seen",false)) ImGui::TextDisabled("Remembered location");
  if(content.value("hallucinating",false)) ImGui::TextWrapped("Hallucinating — appearances are unreliable.");
  if(content.value("trap",false)) ImGui::TextWrapped("Known trap");
  const auto &items=content["items"];
  if(!items.empty() || content.value("unresolved_item",false)) {
   ImGui::SeparatorText(content.value("seen",false)?"On the ground":"Remembered items");
   int count=0; for(const auto &item:items) {
    if(count++==5) { ImGui::TextDisabled("And %d more — use Look",int(items.size())-5); break; }
    ImGui::PushStyleColor(ImGuiCol_Text,color(std::max(1,item.value("color",1))));
    ImGui::TextWrapped("%s",item.value("label","Item").c_str()); ImGui::PopStyleColor();
   }
   if(content.value("unresolved_item",false)) ImGui::TextUnformatted("An observed item");
  }
  ImGui::PopTextWrapPos(); ImGui::EndTooltip();
 }
};
