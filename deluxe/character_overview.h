#include "status_effects.h"
// Compact character overview. All values and XP thresholds come from the engine.
struct CharacterOverview {
 static void meter(const char *label,const std::string &value,float fraction,ImVec4 fill,const std::string &badge="") {
  ImGui::PushStyleColor(ImGuiCol_PlotHistogram,fill);
  ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(fill.x*.22f,fill.y*.22f,fill.z*.22f,1));
  ImGui::ProgressBar(fraction,ImVec2(-1,ImGui::GetFrameHeight()),"");
  const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
  const float pad=ImGui::GetStyle().FramePadding.x;
  const float y=a.y+(b.y-a.y-ImGui::GetFontSize())*.5f;
  auto *draw=ImGui::GetWindowDrawList(); const auto ink=ImGui::GetColorU32(ImGuiCol_Text);
  // A lit glass face, a thin bright edge, and fine scale marks below the text.
  const float badge_width=badge.empty()?0.f:std::min(ImGui::CalcTextSize(badge.c_str()).x+2*pad,(b.x-a.x)*.55f);
  const ImVec2 track(a.x+badge_width,a.y);
  // Labels share one continuous fill; the level is not a separate segment.
  const float edge=a.x+(b.x-a.x)*std::clamp(fraction,0.f,1.f);
  if(edge>a.x) {
   const auto top=ImGui::GetColorU32(ImVec4(fill.x*1.15f,fill.y*1.15f,fill.z*1.15f,1));
   const auto bottom=ImGui::GetColorU32(ImVec4(fill.x*.55f,fill.y*.55f,fill.z*.55f,1));
   draw->AddRectFilledMultiColor(a,ImVec2(edge,b.y),top,top,bottom,bottom);
   draw->AddLine(ImVec2(a.x+1,a.y+1),ImVec2(edge,a.y+1),ImGui::GetColorU32(ImVec4(fill.x+.18f,fill.y+.18f,fill.z+.18f,.8f)));
  }
  for(int i=1;i<10;++i) {
   const float x=a.x+(b.x-a.x)*i/10.f;
   draw->AddLine(ImVec2(x,b.y-3),ImVec2(x,b.y-1),IM_COL32(180,210,200,65));
  }
  draw->AddRect(a,b,ImGui::GetColorU32(ImGuiCol_Border),2);
  const float value_width=ImGui::CalcTextSize(value.c_str()).x;
  if(!badge.empty()) {
   const ImVec2 end(track.x,b.y);
   draw->PushClipRect(a,end,true);
   draw->AddText(ImVec2(a.x+pad,y),ink,badge.c_str());
   draw->PopClipRect();
  }
  draw->PushClipRect(track,b,true);
  if(ImGui::CalcTextSize(label).x+value_width+3*pad<b.x-track.x) draw->AddText(ImVec2(track.x+pad,y),ink,label);
  draw->AddText(ImVec2(std::max(track.x+pad,b.x-pad-value_width),y),ink,value.c_str());
  draw->PopClipRect(); ImGui::PopStyleColor(2);
 }
 static void metric(const char *label,const std::string &value,const std::string &tooltip="") {
  // Keep each label/value pair together inside its own quiet, bounded tile.
  const auto a=ImGui::GetCursorScreenPos();
  const float width=ImGui::GetContentRegionAvail().x,pad=ImGui::GetFontSize()*.4f;
  const float label_width=ImGui::CalcTextSize(label).x,value_width=ImGui::CalcTextSize(value.c_str()).x;
  const float gap=*label?ImGui::GetFontSize()*.6f:0.f;
  const bool stacked=*label && label_width+gap+value_width+2*pad>width;
  const float height=ImGui::GetFontSize()*(stacked?2.f:1.f)+2*pad;
  const ImVec2 b(a.x+width,a.y+height);
  auto *draw=ImGui::GetWindowDrawList();
  draw->AddRectFilledMultiColor(a,b,IM_COL32(23,37,40,255),IM_COL32(17,28,32,255),IM_COL32(12,22,27,255),IM_COL32(17,28,31,255));
  draw->AddRect(a,b,ImGui::GetColorU32(ImGuiCol_Border),2);
  DeluxeTheme::corners(draw,a,b,IM_COL32(74,109,92,180),pad);
  draw->PushClipRect(a,b,true);
  draw->AddText(ImVec2(a.x+pad,a.y+pad),ImGui::GetColorU32(ImGuiCol_TextDisabled),label);
  draw->AddText(ImVec2(a.x+pad+(stacked?0:label_width+gap),a.y+pad+(stacked?ImGui::GetFontSize():0)),ImGui::GetColorU32(ImGuiCol_Text),value.c_str());
  draw->PopClipRect(); ImGui::Dummy(ImVec2(width,height));
  if(ImGui::IsItemHovered()) {
   ImGui::BeginTooltip(); ImGui::PushTextWrapPos(ImGui::GetFontSize()*28.f);
   const auto text=tooltip.empty()?(*label?std::string(label)+": "+value:value):tooltip;
   ImGui::TextUnformatted(text.c_str()); ImGui::PopTextWrapPos(); ImGui::EndTooltip();
  }
 }
 static void section(const char *label) {
  ImGui::Dummy(ImVec2(0,ImGui::GetFontSize()*.55f));
  DeluxeTheme::section(label);
  ImGui::Dummy(ImVec2(0,ImGui::GetFontSize()*.1f));
 }
 static bool draw(const json &p,bool can_open,bool *open_spells=nullptr) {
  bool open=false;
  std::string identity=p.value("name","");
  if(!identity.empty()) identity+=" · ";
  identity+=p.value("race","")+" "+p.value("class","");
  const auto title=p.value("title","");
  if(!title.empty()) identity+=" · "+display_label(title);
  if(ImGui::BeginTable("Character identity",2,ImGuiTableFlags_SizingStretchProp)) {
   ImGui::TableSetupColumn("Identity",ImGuiTableColumnFlags_WidthStretch);
   ImGui::TableSetupColumn("Details",ImGuiTableColumnFlags_WidthFixed,ImGui::CalcTextSize("Details").x+2*ImGui::GetStyle().FramePadding.x);
   ImGui::TableNextRow(); ImGui::TableNextColumn();
   const bool compact=ImGui::CalcTextSize(identity.c_str()).x+ImGui::GetFontSize()*1.3f>ImGui::GetContentRegionAvail().x;
   auto name=p.value("name",""); if(name.empty()) name="Adventurer";
   DeluxeTheme::section(compact?name.c_str():identity.c_str(),false);
   if(ImGui::IsItemHovered()) {
    ImGui::BeginTooltip(); ImGui::PushTextWrapPos(ImGui::GetFontSize()*28.f);
    ImGui::TextUnformatted(name.c_str()); ImGui::Separator();
    ImGui::Text("Race: %s",p.value("race","").c_str());
    ImGui::Text("Class: %s",p.value("class","").c_str());
    if(!title.empty()) ImGui::Text("Title: %s",display_label(title).c_str());
    ImGui::PopTextWrapPos(); ImGui::EndTooltip();
   }
   ImGui::TableNextColumn(); ImGui::BeginDisabled(!can_open); open=ImGui::Button("Details"); ImGui::EndDisabled();
   if(ImGui::IsItemHovered()) ImGui::SetTooltip("Open character details");
   ImGui::EndTable();
  }
  if(ImGui::BeginTable("Resources",2,ImGuiTableFlags_SizingStretchSame)) {
   auto resource=[&](const char *label,const char *current,const char *maximum,ImVec4 fill) {
    const int v=p.value(current,0),m=p.value(maximum,0);
    const auto value=std::to_string(std::max(0,v))+" / "+std::to_string(m);
    meter(label,value,resource_fraction(v,m),fill);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s: %s",label,value.c_str());
   };
   ImGui::TableNextRow(); ImGui::TableNextColumn(); resource("HP","hp","max_hp",ImVec4(.66f,.15f,.19f,1));
   ImGui::TableNextColumn(); resource("SP","sp","max_sp",ImVec4(.14f,.31f,.62f,1));
   ImGui::TableNextRow(); ImGui::TableNextColumn();
   const int food=p.value("food",0),food_max=p.value("food_max",0);
   char food_text[64]; SDL_snprintf(food_text,sizeof(food_text),"%.1f%% (%d)",food_max>0?100.f*food/food_max:0.f,food);
   meter("Food",food_text,resource_fraction(food,food_max),ImVec4(.14f,.42f,.23f,1));
   if(ImGui::IsItemHovered()) ImGui::SetTooltip("Food: %s",food_text);
   ImGui::TableNextColumn();
   const int xp=p.value("experience",0),base=p.value("level_start_experience",0),next=p.value("next_level_experience",0);
   const float progress=next>0?resource_fraction(xp-base,next-base):1.f;
   char xp_text[64]; SDL_snprintf(xp_text,sizeof(xp_text),next>0?"%.0f%%":"MAX",progress*100);
   meter("",xp_text,progress,ImVec4(.56f,.37f,.12f,1),"Lv "+std::to_string(p.value("level",1)));
   if(ImGui::IsItemHovered()) {
    if(next>0) ImGui::SetTooltip("Level %d\nExperience: %d\nNext level: %d\nRemaining: %d",p.value("level",1),xp,next,std::max(0,next-xp));
    else ImGui::SetTooltip("Level %d\nExperience: %d\nMaximum level reached",p.value("level",1),xp);
   }
   ImGui::EndTable();
  }
  StatusEffects::draw(p,open_spells);
  static const char *names[]={"STR","INT","WIS","DEX","CON"};
  if(p.contains("stats") && !p["stats"].empty() && ImGui::BeginTable("Attributes",int(p["stats"].size()),ImGuiTableFlags_SizingStretchSame|ImGuiTableFlags_BordersInnerV)) {
   ImGui::TableNextRow();
   for(size_t i=0;i<p["stats"].size();++i) {
    ImGui::TableNextColumn();
    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,IM_COL32(18,31,34,255));
    const int v=p["stats"][i]; char value[32];
    if(v>18) SDL_snprintf(value,sizeof(value),"18/%02d",v-18); else SDL_snprintf(value,sizeof(value),"%d",v);
    auto centered=[&](const char *text,bool muted) {
     ImGui::SetCursorPosX(ImGui::GetCursorPosX()+std::max(0.f,(ImGui::GetContentRegionAvail().x-ImGui::CalcTextSize(text).x)*.5f));
     if(muted) ImGui::TextDisabled("%s",text); else ImGui::TextUnformatted(text);
    };
    const float left=ImGui::GetCursorPosX();
    centered(i<std::size(names)?names[i]:"Stat",true);
    ImGui::SetCursorPosX(left); centered(value,false);
   }
   ImGui::EndTable();
  }
  if(ImGui::BeginTable("Combat overview",3,ImGuiTableFlags_SizingStretchSame)) {
   ImGui::TableNextRow(); ImGui::TableNextColumn(); metric("Gold",std::to_string(p.value("gold",0)));
   ImGui::TableNextColumn(); metric("Armour",std::to_string(p.value("armour",0)));
   ImGui::TableNextColumn(); const int speed=p.value("speed",0); metric("Speed",(speed>=0?"+":"")+std::to_string(speed));
   ImGui::EndTable();
  }
  if(p.value("extra_moves",0)) ImGui::Text("Extra moves: %+d",p.value("extra_moves",0));
  section("Dungeon");
  const char *dungeon_labels[]={"Depth","Light","Feel",""};
  const std::string dungeon_values[]={std::to_string(p.value("depth",0)),std::to_string(p.value("light",0)),p.value("feeling","—"),display_label(p.value("floor",""))};
  const std::string dungeon_tips[]={"Depth: "+std::to_string(p.value("depth_feet",p.value("depth",0)*50))+" feet","",p.value("feeling_description",""),""};
  // Match metric's label/value spacing, plus the table's cell padding.
  // Use the widest tile so every tile stays on one line at the breakpoint.
  float tile_width=0;
  for(int i=0;i<4;++i) {
   const float content=ImGui::CalcTextSize(dungeon_labels[i]).x+ImGui::CalcTextSize(dungeon_values[i].c_str()).x;
   const float padding=ImGui::GetFontSize()*(*dungeon_labels[i]?1.4f:.8f)+2*ImGui::GetStyle().CellPadding.x+2;
   tile_width=std::max(tile_width,content+padding);
  }
  const int dungeon_columns=ImGui::GetContentRegionAvail().x>=4*tile_width?4:2;
  if(ImGui::BeginTable("Dungeon overview",dungeon_columns,ImGuiTableFlags_SizingStretchSame)) {
   for(int i=0;i<4;++i) {
    ImGui::TableNextColumn(); metric(dungeon_labels[i],dungeon_values[i],dungeon_tips[i]);
   }
   ImGui::EndTable();
  }
  if(p.value("trap_detected",false)) ImGui::TextUnformatted("Trap-detected area");
  if(p.value("recall",0)) ImGui::TextUnformatted("Recall pending");
  if(p.value("descent",0)) ImGui::TextUnformatted("Descent pending");
  if(p.value("resting",0)) ImGui::TextUnformatted("Resting");
  if(p.value("running",0)) ImGui::TextUnformatted("Running");
  if(p.value("repeat",0)) ImGui::Text("Repeating: %d",p.value("repeat",0));
  if(p.value("unignoring",false)) ImGui::TextUnformatted("Showing ignored items");
  if(p.contains("tracked_creature")) {
   const auto &m=p["tracked_creature"];
   section("Tracked creature");
   if(m.value("visible",false)) {
    ImGui::TextWrapped("%s",display_label(m.value("name","")).c_str());
    meter("HP",std::to_string(std::max(0,m.value("hp",0)))+" / "+std::to_string(m.value("max_hp",0)),
     resource_fraction(m.value("hp",0),m.value("max_hp",0)),ImVec4(.56f,.37f,.12f,1));
   } else ImGui::TextDisabled("Out of sight");
  }
  return open;
 }
};
