// Native preference panels. Changes are revision-checked by the engine.
struct ItemRules {
 bool open=false,auto_open=false;
 std::string auto_item,auto_revision,auto_label;
 char auto_text[80]{};
 static void inscription_help() {
  ImGui::TextDisabled("Inscription tags (?)");
  if(ImGui::IsItemHovered()) {
   ImGui::BeginTooltip();
   ImGui::TextUnformatted(R"(!d  Confirm before dropping
!q  Confirm before quaffing
!*  Confirm before any action; protect from broad ignore rules
!k  Protect from broad ignore rules
@q1 Assign quaff shortcut 1)");
   ImGui::EndTooltip();
  }
 }
 void context(Connection &c,const json &item) {
  if(c.capabilities.value("item.rules",0)<1 || !item.contains("preferences")) return;
  const auto &p=item["preferences"]; const auto id=item.value("id","");
  ImGui::Separator();
  auto set=[&](const char *op,bool enabled) {
   c.target("item.preferences",{{"revision",c.state.value("revision","")},{"item",id},{"operation",op},{"enabled",enabled}});
  };
  if(ImGui::MenuItem(p.value("item_ignored",false)?"Unignore this item":"Ignore this item")) set("ignore_item",!p.value("item_ignored",false));
  if(p.value("kind_allowed",false)) {
   const auto label=std::string(p.value("kind_ignored",false)?"Unignore item kind: ":"Ignore item kind: ")+p.value("kind_label","");
   if(ImGui::MenuItem(label.c_str())) set("ignore_kind",!p.value("kind_ignored",false));
  }
  if(ImGui::MenuItem("Auto-inscribe this kind...")) {
   auto_item=id; auto_revision=c.state.value("revision",""); auto_label=p.value("kind_label","");
   SDL_strlcpy(auto_text,p.value("autoinscription","").c_str(),sizeof(auto_text)); auto_open=true;
  }
 }
 bool draw(Connection &c) {
  bool closed=false;
  if(open) { ImGui::OpenPopup("Item rules"); open=false; c.item_rules=nullptr; }
  ImGui::SetNextWindowSize(ImVec2(std::min(ImGui::GetIO().DisplaySize.x*.9f,ImGui::GetFontSize()*48),ImGui::GetIO().DisplaySize.y*.7f),ImGuiCond_Appearing);
  if(ImGui::BeginPopupModal("Item rules",nullptr,ImGuiWindowFlags_NoSavedSettings)) {
   if(c.item_rules.is_null() && c.connected) { c.item_rules=json::object(); c.send("item.rules.list"); }
   ImGui::TextWrapped("Configured ignore rules and auto-inscriptions. Removing a rule leaves other matching rules in place.");
   ImGui::BeginChild("Rules list",ImVec2(0,-ImGui::GetFrameHeightWithSpacing()));
   if(!c.item_rules.contains("rules")) ImGui::TextDisabled("Loading...");
   else {
    const auto &rules=c.item_rules["rules"];
    if(rules.empty()) ImGui::TextDisabled("No configured rules.");
    for(const auto &r:rules) {
     ImGui::PushID(r.value("id","").c_str());
     DeluxeTheme::section(display_label(r.value("type","")).c_str());
     ImGui::TextWrapped("%s",r.value("label","").c_str());
     ImGui::TextWrapped("%s",r.value("value","").c_str());
     ImGui::BeginDisabled(!c.ready());
     if(ImGui::Button("Remove rule")) c.target("item.rules.clear",{{"revision",c.item_rules.value("revision","")},{"rule",r.value("id","")}});
     ImGui::EndDisabled(); ImGui::PopID();
    }
   }
   ImGui::EndChild();
   if(ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) { ImGui::CloseCurrentPopup(); closed=true; }
   ImGui::EndPopup();
  }
  if(auto_open) { ImGui::OpenPopup("Auto-inscription"); auto_open=false; }
  ImGui::SetNextWindowSize(ImVec2(std::min(ImGui::GetIO().DisplaySize.x*.9f,ImGui::GetFontSize()*40),0),ImGuiCond_Appearing);
  if(ImGui::BeginPopupModal("Auto-inscription",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
   ImGui::TextWrapped("%s",auto_label.c_str());
   ImGui::TextWrapped("Applies to this item kind using Angband's normal rules. Existing manual inscriptions are preserved. Leave blank to remove the rule.");
   inscription_help();
   if(ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
   ImGui::InputText("Inscription",auto_text,sizeof(auto_text));
   const bool current=c.ready() && auto_revision==c.state.value("revision","");
   if(!current) ImGui::TextWrapped("The game state changed. Reopen this editor to apply changes.");
   ImGui::BeginDisabled(!current);
   if(ImGui::Button("Save")) {
    c.target("item.preferences",{{"revision",auto_revision},{"item",auto_item},{"operation","autoinscribe"},{"text",auto_text}});
    ImGui::CloseCurrentPopup(); closed=true;
   }
   ImGui::EndDisabled(); ImGui::SameLine();
   if(ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) { ImGui::CloseCurrentPopup(); closed=true; }
   ImGui::EndPopup();
  }
  return closed;
 }
};
