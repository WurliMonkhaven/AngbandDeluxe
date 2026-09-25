// Native presentation of engine-owned creation data and commands.
struct BirthPanel {
 bool initialized=false, history_edited=false;
 int step=0;
 char name[128]{}, history[4097]{};
 static void act(Connection &c,const char *action,json args=json::object()) {
  if(c.busy || !c.connected) return;
  args["revision"]=c.state.value("revision",""); args["action"]=action;
  c.send("birth.action",args); c.busy=true;
 }
 static const json &chosen(const json &b,const char *list,const char *field) {
  for(const auto &v:b.at(list)) if(v["id"]==b[field]) return v;
  return b.at(list).front();
 }
 static void ability_card(const json &ability) {
  const auto title=ability.value("name",""),description=ability.value("description","");
  const float pad=ImGui::GetFontSize()*.5f;
  const float width=std::max(1.f,ImGui::GetContentRegionAvail().x-2*pad);
  const float title_height=ImGui::CalcTextSize(title.c_str(),nullptr,false,width).y;
  const float text_height=description.empty()?0:ImGui::CalcTextSize(description.c_str(),nullptr,false,width).y;
  const float gap=ImGui::GetStyle().ItemSpacing.y;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(pad,pad));
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,4.f);
  ImGui::PushStyleColor(ImGuiCol_ChildBg,ImVec4(.09f,.12f,.16f,1));
  ImGui::PushStyleColor(ImGuiCol_Border,ImVec4(.20f,.27f,.34f,1));
  if(ImGui::BeginChild("Ability",ImVec2(0,title_height+text_height+2*pad+2*gap+2),ImGuiChildFlags_Borders|ImGuiChildFlags_AlwaysUseWindowPadding,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse)) {
   ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.65f,.82f,1,1));
   ImGui::TextWrapped("%s",title.c_str()); ImGui::PopStyleColor();
   ImGui::Separator();
   if(!description.empty()) ImGui::TextWrapped("%s",description.c_str());
  }
  ImGui::EndChild(); ImGui::PopStyleColor(2); ImGui::PopStyleVar(2);
 }
 static void choices(Connection &c,const json &b,const char *list,const char *field) {
  const auto &selected=chosen(b,list,field);
  ImGui::SetNextItemWidth(-1);
  if(ImGui::BeginCombo("##Choice",selected.value("name","").c_str())) {
   for(const auto &v:b.at(list)) if(ImGui::Selectable(v.value("name","").c_str(),v["id"]==selected["id"])) act(c,field,{{"choice",v["id"]}});
   ImGui::EndCombo();
  }
  ImGui::Spacing();
  if(ImGui::BeginTable("Modifiers",5,ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_SizingStretchSame)) {
   const char *stats[]={"STR","INT","WIS","DEX","CON"};
   for(int i=0;i<5;++i) { ImGui::TableNextColumn(); ImGui::TextDisabled("%s",stats[i]); ImGui::Text("%+d",selected["modifiers"][i].get<int>()); }
   ImGui::EndTable();
  }
  ImGui::Spacing(); AnybandUITheme::section("Traits");
  const int columns=ImGui::GetContentRegionAvail().x>ImGui::GetFontSize()*32?3:2;
  if(ImGui::BeginTable("Trait summary",columns,ImGuiTableFlags_SizingStretchSame)) {
   const int hp=selected.value("hit_die",0),xp=selected.value("experience",0);
   ImGui::TableNextColumn(); CharacterOverview::metric("Hit die",(hp>=0?"+":"")+std::to_string(hp));
   ImGui::TableNextColumn(); CharacterOverview::metric("XP modifier",(xp>=0?"+":"")+std::to_string(xp)+"%");
   if(selected.contains("infravision")) { ImGui::TableNextColumn(); CharacterOverview::metric("Infravision",std::to_string(selected.value("infravision",0))+" ft"); }
   if(selected.contains("spellcasting")) { ImGui::TableNextColumn(); CharacterOverview::metric("Magic",selected.value("spellcasting",false)?"Yes":"No",selected.value("spellcasting",false)?"Can learn spells.":"Does not cast spells."); }
   ImGui::EndTable();
  }
  if(!selected.at("abilities").empty()) {
   ImGui::Spacing(); AnybandUITheme::section("Abilities");
   int index=0;
   for(const auto &ability:selected.at("abilities")) {
    ImGui::PushID(index++); ability_card(ability); ImGui::PopID();
   }
  }
 }
 static void stats(Connection &c,const json &b,bool editable) {
  if(ImGui::BeginTable(editable?"Point allocation":"Attribute preview",editable?7:5,ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchProp)) {
   for(const char *heading:{"Stat","Base","Race","Class","Total"}) ImGui::TableSetupColumn(heading);
   if(editable) { ImGui::TableSetupColumn("Spent"); ImGui::TableSetupColumn("Adjust",ImGuiTableColumnFlags_WidthFixed,2*(ImGui::CalcTextSize("+").x+2*ImGui::GetStyle().FramePadding.x)+ImGui::GetStyle().ItemSpacing.x); }
   ImGui::TableHeadersRow();
   for(const auto &v:b.at("stats")) {
    ImGui::PushID(v["id"].get<int>()); ImGui::TableNextRow();
    ImGui::TableNextColumn(); ImGui::TextUnformatted(v.value("name","").c_str());
    ImGui::TableNextColumn(); ImGui::Text("%d",v.value("base",0));
    ImGui::TableNextColumn(); ImGui::Text("%+d",v.value("race",0));
    ImGui::TableNextColumn(); ImGui::Text("%+d",v.value("class",0));
    ImGui::TableNextColumn(); ImGui::TextUnformatted(v.value("total","").c_str());
    if(editable) {
     ImGui::TableNextColumn(); ImGui::Text("%d",v.value("spent",0));
     ImGui::TableNextColumn(); ImGui::BeginDisabled(!v.value("can_sell",false));
     if(ImGui::SmallButton("-")) act(c,"sell",{{"choice",v["id"]}});
     ImGui::EndDisabled(); ImGui::SameLine(); ImGui::BeginDisabled(!v.value("can_buy",false));
     if(ImGui::SmallButton("+")) act(c,"buy",{{"choice",v["id"]}});
     if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      if(v.value("base",0)>=18) ImGui::SetTooltip("Base stat is at its maximum.");
      else ImGui::SetTooltip("Costs %d points. %d remaining.%s",v.value("cost",0),b.value("points_left",0),v.value("can_buy",false)?"":" Reduce another stat to free points.");
     }
     ImGui::EndDisabled();
    }
    ImGui::PopID();
   }
   ImGui::EndTable();
  }
 }
 void draw(Connection &c) {
  const auto &b=c.state.at("birth");
  if(!initialized) {
   initialized=true; step=b.value("quickstart",false)?-1:0; history_edited=false;
   SDL_strlcpy(name,b.value("name","").empty()?c.character_save.c_str():b.value("name","").c_str(),sizeof(name));
  }
  if(!history_edited) SDL_strlcpy(history,b.value("history","").c_str(),sizeof(history));
  if(step==-1) {
   AnybandUITheme::section("Play again");
   ImGui::BeginChild("Previous character",ImVec2(0,-ImGui::GetFrameHeightWithSpacing()*2));
   ImGui::TextWrapped("%s",name);
   ImGui::TextWrapped("%s %s",chosen(b,"races","race").value("name","").c_str(),chosen(b,"classes","class").value("name","").c_str());
   ImGui::Spacing();
   ImGui::TextWrapped("Reuse this character's starting choices or create a new character.");
   ImGui::Spacing(); AnybandUITheme::section("Starting attributes");
   stats(c,b,false);
   ImGui::Spacing(); ImGui::TextWrapped("%s",history);
   ImGui::Spacing(); AnybandUITheme::section("New character");
   ImGui::BeginDisabled(c.busy || !c.connected);
   if(ImGui::Button("Use previous character")) act(c,"accept",{{"name",name},{"history",history}});
   ImGui::TextWrapped("Begin at level 1 with the same race, class and starting attributes.");
   ImGui::Spacing();
   if(ImGui::Button("Change name / background")) step=3;
   ImGui::TextWrapped("Keep the character's starting build and give them a new identity.");
   ImGui::Spacing();
   if(ImGui::Button("Create a different character")) { act(c,"suggest"); step=0; }
   ImGui::TextWrapped("Choose race, class, attributes and identity in character creation.");
   ImGui::Spacing();
   if(ImGui::CollapsingHeader("Birth options")) for(const auto &opt:b.at("options")) {
    bool enabled=opt.value("value",false);
    if(ImGui::Checkbox(opt.value("description","").c_str(),&enabled)) act(c,"option",{{"option",opt["id"]},{"value",enabled}});
   }
   ImGui::EndDisabled(); ImGui::EndChild();
   if(!c.menu_error.empty()) ImGui::TextWrapped("%s",c.menu_error.c_str());
   ImGui::Separator(); ImGui::BeginDisabled(c.busy || !c.connected);
   if(ImGui::Button("Return to main menu")) { c.return_to_menu=true; c.send("birth.cancel",{{"revision",c.state.value("revision","")}}); c.busy=true; }
   ImGui::EndDisabled(); return;
  }
  const char *steps[]={"Race","Class","Attributes","Identity","Review"};
  AnybandUITheme::section("Create a character");
  ImGui::Text("Step %d of 5 - %s",step+1,steps[step]);
  ImGui::ProgressBar(float(step+1)/5,ImVec2(-1,4),"");
  ImGui::Spacing();
  const float footer=ImGui::GetFrameHeightWithSpacing()*2;
  ImGui::BeginChild("Birth contents",ImVec2(0,-footer));
  ImGui::BeginDisabled(c.busy || !c.connected);
  const bool wide=ImGui::GetContentRegionAvail().x>ImGui::GetFontSize()*46;
  if(ImGui::BeginTable("Birth layout",wide?2:1,ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV)) {
   ImGui::TableSetupColumn("Choices",ImGuiTableColumnFlags_WidthStretch,.62f);
   if(wide) ImGui::TableSetupColumn("Preview",ImGuiTableColumnFlags_WidthStretch,.38f);
   ImGui::TableNextColumn();
   AnybandUITheme::section(steps[step]);
   if(step<2) {
    if(step==0 && b.value("quickstart",false) && ImGui::Button("Use previous character")) { act(c,"quickstart"); step=4; }
    choices(c,b,step==0?"races":"classes",step==0?"race":"class");
   }
   else if(step==2) {
    bool rolled=b.value("rolled",false);
    if(ImGui::RadioButton("Point buy",!rolled) && rolled) act(c,"suggest");
    ImGui::SameLine(); if(ImGui::RadioButton("Random rolls",rolled) && !rolled) act(c,"roll");
    if(!rolled) {
     ImGui::Text("Points remaining: %d",b.value("points_left",0));
     ImGui::TextWrapped(b.value("points_left",0)==0?"All points are allocated. Use - to free points, or Reset points to start from scratch.":"Use + to spend points and - to refund them. Race and class bonuses are added automatically.");
     stats(c,b,true);
     if(ImGui::Button("Suggested allocation")) act(c,"suggest"); ImGui::SameLine();
     if(ImGui::Button("Reset points")) act(c,"reset");
    } else {
     stats(c,b,false);
     if(ImGui::Button("Roll again")) act(c,"roll"); ImGui::SameLine();
     ImGui::BeginDisabled(!b.value("previous_roll",false));
     if(ImGui::Button("Previous roll")) act(c,"previous"); ImGui::EndDisabled();
    }
   } else if(step==3) {
    ImGui::TextUnformatted("Name");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##Name",name,std::min(sizeof(name),size_t(b.value("name_max",31)+1)));
    ImGui::TextUnformatted("Background");
    if(ImGui::InputTextMultiline("##History",history,sizeof(history),ImVec2(-1,ImGui::GetTextLineHeight()*8),ImGuiInputTextFlags_WordWrap)) history_edited=true;
    if(ImGui::Button("Restore generated background")) { history_edited=false; SDL_strlcpy(history,b.value("history","").c_str(),sizeof(history)); }
   } else {
    ImGui::TextWrapped("%s - %s %s",name,chosen(b,"races","race").value("name","").c_str(),chosen(b,"classes","class").value("name","").c_str());
    ImGui::TextWrapped("%s",history);

   }
   ImGui::Spacing();
   if(step<2) {
    const auto &selected=chosen(b,step==0?"races":"classes",step==0?"race":"class");
  if(selected.contains("skills") && ImGui::CollapsingHeader("Skill modifiers")) {
   if(ImGui::BeginTable("Skills",2,ImGuiTableFlags_RowBg)) {
    for(const auto &skill:selected["skills"]) { ImGui::TableNextColumn(); ImGui::TextUnformatted(skill.value("name","").c_str()); ImGui::TableNextColumn(); ImGui::Text("%+d",skill.value("value",0)); }
    ImGui::EndTable();
   }
  }
   }
   if(ImGui::CollapsingHeader("Birth options")) for(const auto &opt:b.at("options")) {
    bool enabled=opt.value("value",false);
    if(ImGui::Checkbox(opt.value("description","").c_str(),&enabled)) act(c,"option",{{"option",opt["id"]},{"value",enabled}});
   }
   ImGui::TableNextColumn(); AnybandUITheme::section("Character preview");
   ImGui::TextWrapped("%s",name);
   ImGui::TextWrapped("%s %s",chosen(b,"races","race").value("name","").c_str(),chosen(b,"classes","class").value("name","").c_str());
   ImGui::Spacing(); ImGui::Text("HP %d    SP %d",b.value("hp",0),b.value("sp",0));
   ImGui::Text("Starting funds: %d gold",b.value("gold",0));
   ImGui::TextWrapped("Starting equipment is deducted from these funds.");
   stats(c,b,false); ImGui::Spacing(); ImGui::TextWrapped("%s",history);
   ImGui::EndTable();
  }
  ImGui::EndDisabled(); ImGui::EndChild();
  if(!c.menu_error.empty()) ImGui::TextWrapped("%s",c.menu_error.c_str());
  ImGui::Separator(); ImGui::BeginDisabled(c.busy || !c.connected);
  if(ImGui::Button("Return to main menu")) { c.return_to_menu=true; c.send("birth.cancel",{{"revision",c.state.value("revision","")}}); c.busy=true; }
  const float nav_width=ImGui::CalcTextSize("Back").x+ImGui::CalcTextSize(step<4?"Next":"Start game").x+4*ImGui::GetStyle().FramePadding.x+ImGui::GetStyle().ItemSpacing.x;
  ImGui::SameLine();
  const float right=ImGui::GetCursorPosX()+ImGui::GetContentRegionAvail().x;
  if(ImGui::GetContentRegionAvail().x<nav_width) ImGui::NewLine();
  ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),right-nav_width));
  ImGui::BeginDisabled(step==0); if(ImGui::Button("Back")) --step; ImGui::EndDisabled(); ImGui::SameLine();
  if(step<4) { if(ImGui::Button("Next")) ++step; }
  else { ImGui::BeginDisabled(name[0]==0); if(ImGui::Button("Start game")) act(c,"accept",{{"name",name},{"history",history}}); ImGui::EndDisabled(); }
  ImGui::EndDisabled();
 }
};
