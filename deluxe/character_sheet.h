// Native, read-only character sheet. Values and descriptions belong to the engine.
struct CharacterSheet {
 static void rows(const json &sheet,const char *group) {
  DeluxeTheme::section(group);
  ImGui::PushID(group);
  if(ImGui::BeginTable("Values",2,ImGuiTableFlags_SizingStretchProp|ImGuiTableFlags_RowBg)) {
   ImGui::TableSetupColumn("Label",ImGuiTableColumnFlags_WidthStretch,1.2f);
   ImGui::TableSetupColumn("Value",ImGuiTableColumnFlags_WidthStretch,1.f);
   for(const auto &row:sheet.value("rows",json::array())) if(row.value("group","")==group) {
    ImGui::TableNextRow(); ImGui::TableNextColumn();
    ImGui::TextUnformatted(row.value("label","").c_str()); ImGui::TableNextColumn();
    ImGui::PushStyleColor(ImGuiCol_Text,color(row.value("color",1)));
    ImGui::TextWrapped("%s",row.value("value","").c_str()); ImGui::PopStyleColor();
   }
   ImGui::EndTable();
  }
  ImGui::PopID();
 }
 static void attributes(const json &sheet) {
  DeluxeTheme::section("Attributes");
  if(ImGui::BeginTable("Stat breakdown",7,ImGuiTableFlags_SizingStretchSame|ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerV)) {
   for(const char *label:{"Stat","Base","Race","Class","Gear","Best","Current"}) ImGui::TableSetupColumn(label);
   ImGui::TableHeadersRow();
   for(const auto &row:sheet.value("attributes",json::array())) {
    ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(row.value("label","").c_str());
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",row.value("sustained",false)?"Sustained":"Not sustained");
    ImGui::TableNextColumn(); ImGui::TextUnformatted(row.value("base","").c_str());
    for(const char *key:{"race","class","equipment"}) { ImGui::TableNextColumn(); ImGui::Text("%+d",row.value(key,0)); }
    for(const char *key:{"best","current"}) { ImGui::TableNextColumn(); ImGui::TextUnformatted(row.value(key,"").c_str()); }
   }
   ImGui::EndTable();
  }
 }
 static void contents(const json &sheet) {
  if(ImGui::BeginTabBar("Character pages")) {
   if(ImGui::BeginTabItem("Overview")) {
    ImGui::BeginChild("Overview scroll");
    rows(sheet,"Identity"); attributes(sheet); rows(sheet,"Progression");
    ImGui::EndChild(); ImGui::EndTabItem();
   }
   if(ImGui::BeginTabItem("Combat & skills")) {
    ImGui::BeginChild("Combat scroll"); rows(sheet,"Combat"); rows(sheet,"Skills");
    ImGui::EndChild(); ImGui::EndTabItem();
   }
   if(ImGui::BeginTabItem("Resistances & abilities")) {
    ImGui::BeginChild("Defences scroll"); DeluxeTheme::section("Resistances");
    if(ImGui::BeginTable("Resistances",2,ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchSame)) {
     for(const auto &row:sheet.value("resistances",json::array())) {
      ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(row.value("label","").c_str());
      ImGui::TableNextColumn(); const int level=row.value("level",0);
      ImGui::PushStyleColor(ImGuiCol_Text,level<0?ImVec4(1,.4f,.35f,1):level>0?ImVec4(.45f,.9f,.6f,1):ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      ImGui::TextUnformatted(row.value("value","").c_str()); ImGui::PopStyleColor();
     }
     ImGui::EndTable();
    }
    DeluxeTheme::section("Abilities & sustains");
    const auto abilities=sheet.value("abilities",json::array());
    if(abilities.empty()) ImGui::TextDisabled("No known abilities.");
    for(const auto &row:abilities) {
     ImGui::BulletText("%s",display_label(row.value("label","")).c_str());
     if(ImGui::IsItemHovered() && !row.value("description","").empty()) ImGui::SetTooltip("%s",row.value("description","").c_str());
    }
    ImGui::EndChild(); ImGui::EndTabItem();
   }
   if(ImGui::BeginTabItem("Background")) {
    ImGui::BeginChild("Background scroll"); DeluxeTheme::section("History");
    ImGui::TextWrapped("%s",sheet.value("history","").c_str()); rows(sheet,"Background");
    ImGui::EndChild(); ImGui::EndTabItem();
   }
   ImGui::EndTabBar();
  }
 }
 static bool draw(const json &player,bool request) {
  if(request) ImGui::OpenPopup("Character details");
  const auto display=ImGui::GetIO().DisplaySize;
  ImGui::SetNextWindowSize(ImVec2(std::min(display.x*.92f,ImGui::GetFontSize()*54),display.y*.85f),ImGuiCond_Appearing);
  ImGui::SetNextWindowPos(ImVec2(display.x*.5f,display.y*.5f),ImGuiCond_Appearing,ImVec2(.5f,.5f));
  bool closed=false,show=true;
  if(ImGui::BeginPopupModal("Character details",&show,ImGuiWindowFlags_NoSavedSettings)) {
   ImGui::BeginChild("Sheet body",ImVec2(0,-ImGui::GetFrameHeightWithSpacing()));
   if(player.contains("character_sheet")) contents(player["character_sheet"]);
   else ImGui::TextDisabled("Character details unavailable.");
   ImGui::EndChild();
   if(ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape) || !show) { ImGui::CloseCurrentPopup(); closed=true; }
   ImGui::EndPopup();
  }
  return closed || !show;
 }
};
