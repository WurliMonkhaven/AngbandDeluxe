#include "status_effects.h"
// Compact character overview. All values and XP thresholds come from the engine.
struct CharacterOverview {
 static void meter(const char *label,const std::string &value,float fraction,ImVec4 fill) {
  ImGui::PushStyleColor(ImGuiCol_PlotHistogram,fill);
  ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(fill.x*.22f,fill.y*.22f,fill.z*.22f,1));
  ImGui::ProgressBar(fraction,ImVec2(-1,ImGui::GetFrameHeight()),"");
  const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
  const float pad=ImGui::GetStyle().FramePadding.x;
  const float y=a.y+(b.y-a.y-ImGui::GetFontSize())*.5f;
  auto *draw=ImGui::GetWindowDrawList(); const auto ink=ImGui::GetColorU32(ImGuiCol_Text);
  const float value_width=ImGui::CalcTextSize(value.c_str()).x;
  draw->PushClipRect(a,b,true);
  if(ImGui::CalcTextSize(label).x+value_width+3*pad<b.x-a.x) draw->AddText(ImVec2(a.x+pad,y),ink,label);
  draw->AddText(ImVec2(std::max(a.x+pad,b.x-pad-value_width),y),ink,value.c_str());
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
  draw->AddRectFilled(a,b,ImGui::GetColorU32(ImVec4(.18f,.21f,.25f,.35f)),pad*.5f);
  draw->AddRect(a,b,ImGui::GetColorU32(ImVec4(.40f,.45f,.52f,.25f)),pad*.5f);
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
  ImGui::SeparatorText(label);
  ImGui::Dummy(ImVec2(0,ImGui::GetFontSize()*.1f));
 }
 static bool draw(const json &p,bool can_open) {
  bool open=false;
  std::string identity=p.value("name","");
  if(!identity.empty()) identity+=" · ";
  identity+=p.value("race","")+" "+p.value("class","")+" · Lv "+std::to_string(p.value("level",1));
  const auto title=p.value("title","");
  if(!title.empty()) identity+=" · "+display_label(title);
  if(ImGui::BeginTable("Character identity",2,ImGuiTableFlags_SizingStretchProp)) {
   ImGui::TableSetupColumn("Identity",ImGuiTableColumnFlags_WidthStretch);
   ImGui::TableSetupColumn("Details",ImGuiTableColumnFlags_WidthFixed,ImGui::CalcTextSize("Details").x+2*ImGui::GetStyle().FramePadding.x);
   ImGui::TableNextRow(); ImGui::TableNextColumn();
   if(ImGui::CalcTextSize(identity.c_str()).x+2*ImGui::GetStyle().SeparatorTextPadding.x<=ImGui::GetContentRegionAvail().x)
    ImGui::SeparatorText(identity.c_str());
   else { ImGui::TextWrapped("%s",identity.c_str()); ImGui::Separator(); }
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
   char xp_text[64]; SDL_snprintf(xp_text,sizeof(xp_text),next>0?"%.0f%%":"Max level",progress*100);
   meter("XP",xp_text,progress,ImVec4(.56f,.37f,.12f,1));
   if(ImGui::IsItemHovered()) {
    if(next>0) ImGui::SetTooltip("Experience: %d\nNext level: %d\nRemaining: %d",xp,next,std::max(0,next-xp));
    else ImGui::SetTooltip("Experience: %d\nMaximum level reached",xp);
   }
   ImGui::EndTable();
  }
  StatusEffects::draw(p);
  static const char *names[]={"STR","INT","WIS","DEX","CON"};
  if(p.contains("stats") && !p["stats"].empty() && ImGui::BeginTable("Attributes",int(p["stats"].size()),ImGuiTableFlags_SizingStretchSame|ImGuiTableFlags_BordersInnerV)) {
   ImGui::TableNextRow();
   for(size_t i=0;i<p["stats"].size();++i) {
    ImGui::TableNextColumn();
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
  if(p.value("study",0)) ImGui::Text("Spells to learn: %d",p.value("study",0));
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
