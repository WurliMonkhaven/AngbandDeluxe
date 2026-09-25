#pragma once
// Draft editor. Native keymap expansion and persistence belong to the backend.
struct KeybindingEditor {
 json catalog=json::array(), keys=json::array(), original=json::array(), bindings=json::array();
 bool loaded=false,listening=false,ignore_text=false;
 int mode=0,active_mode=0,revision=0,command=-1,candidate=0;
 std::string error,capture_error;
 char search[128]{};
 static int function_key(SDL_Keycode key) { return key>=SDLK_F1 && key<=SDLK_F12?132+int(key-SDLK_F1):0; }
 static bool valid(int k) {
  return (k>=132 && k<=143) || (k>=1 && k<=26 && k!=8 && k!=9 && k!=10 && k!=13) ||
   (k>=33 && k<=126 && !(k>='0' && k<='9') && k!='\\' && k!='^');
 }
 static std::string key_name(int k) {
  if(k==9) return "Tab"; if(k==13) return "Enter"; if(k==8) return "Backspace"; if(k==27) return "Escape"; if(k==32) return "Space";
  if(k>=132 && k<=143) return "F"+std::to_string(k-131);
  if(k>=1 && k<=26) return "Ctrl+"+std::string(1,char('A'+k-1));
  return k?std::string(1,char(k)):"";
 }
 void reset() { *this=KeybindingEditor(); }
 void load(const json &data) {
  loaded=true; error=data.value("error",""); if(!error.empty()) return;
  catalog=data.value("commands",json::array()); keys=data.value("keys",json::array());
  bindings=original=data.value("bindings",json::array()); revision=data.value("revision",0); mode=active_mode=data.value("mode",0);
 }
 bool changed() const { return bindings!=original; }
 std::string action_name(int id) const {
  for(const auto &row:catalog) if(row.value("id",-1)==id) return row.value("label","");
  return "Unknown action";
 }
 std::string conflict(int key) const {
  for(const auto &row:bindings) if(row.value("mode",-1)==mode && row.value("key",0)==key) return action_name(row.value("command",-1));
  for(const auto &row:keys) if(row.value("mode",-1)==mode && row.value("key",0)==key)
   return row.value("default_action","Unassigned")+(row.contains("sequence")?": "+row.value("sequence",""):"");
  return "Unassigned";
 }
 void remove(int key) {
  bindings.erase(std::remove_if(bindings.begin(),bindings.end(),[&](const json &row){return row.value("mode",-1)==mode && row.value("key",0)==key;}),bindings.end());
 }
 void assign() {
  if(command<0 || !valid(candidate)) return;
  remove(candidate); bindings.push_back({{"mode",mode},{"key",candidate},{"command",command}});
  command=-1; candidate=0; listening=false;
 }
 void capture(int key) {
  if(!valid(key)) { capture_error="That key is reserved for movement, numbers or prompt controls."; return; }
  candidate=key; listening=false; capture_error.clear();
 }
 bool event(const SDL_Event &e) {
  if(!listening) return false;
  if(e.type==SDL_EVENT_KEY_UP) return true;
  if(e.type==SDL_EVENT_KEY_DOWN) {
   if(e.key.repeat) return true;
   ignore_text=false;
   if(e.key.key==SDLK_ESCAPE) { listening=false; command=-1; candidate=0; return true; }
   if(e.key.mod&(SDL_KMOD_ALT|SDL_KMOD_GUI)) { ignore_text=true; capture_error="Use a letter, symbol, Ctrl+letter, or F1–F12."; return true; }
   if(e.key.scancode>=SDL_SCANCODE_KP_DIVIDE && e.key.scancode<=SDL_SCANCODE_KP_PERIOD) { ignore_text=true; capture_error="The numeric keypad stays available for movement."; return true; }
   if(e.key.mod&SDL_KMOD_CTRL) {
    if(e.key.key>='a' && e.key.key<='z') capture(int(e.key.key-'a'+1));
    else capture_error="Use Ctrl with a letter.";
   } else if(function_key(e.key.key)) {
    if(e.key.mod&SDL_KMOD_SHIFT) capture_error="Use F1–F12 without modifiers.";
    else capture(function_key(e.key.key));
   } else if(e.key.key==SDLK_RETURN || e.key.key==SDLK_TAB || e.key.key==SDLK_BACKSPACE) capture(0);
   return true; // Printable keys arrive through layout-aware text input.
  }
  if(e.type==SDL_EVENT_TEXT_INPUT) {
   if(ignore_text) { ignore_text=false; return true; }
   const char *p=e.text.text; const int key=SDL_StepUTF8(&p,nullptr);
   if(!*p) capture(key); else capture_error="Choose a single key.";
   return true;
  }
  return false;
 }
 void draw() {
  if(!loaded) { ImGui::TextUnformatted("Loading keybindings..."); return; }
  if(!error.empty()) { ImGui::TextWrapped("%s",error.c_str()); return; }
  ImGui::TextWrapped("Shared by your characters. Changes apply on Save and Close. Original keys remain available unless overridden.");
  const char *modes[]={"Original keyset","Roguelike keyset"};
  ImGui::SetNextItemWidth(-1);
  if(ImGui::Combo("##Binding mode",&mode,modes,2)) { command=-1; listening=false; }
  ImGui::TextDisabled("Active keyset: %s",active_mode?"Roguelike":"Original");
  if(ImGui::IsItemHovered()) ImGui::SetTooltip("Choose the active keyset under Angband options. This selector chooses which profile to edit.");
  ImGui::TextWrapped("Letters, symbols, Ctrl+letters and F1–F12. Number keys and the keypad retain their existing roles.");
  if(command>=0) {
   ImGui::Spacing(); DeluxeTheme::section(action_name(command).c_str());
   if(listening) ImGui::TextWrapped("Press your new key. Escape cancels capture.");
   else {
    ImGui::Text("New key: %s",key_name(candidate).c_str());
    ImGui::TextWrapped("Currently: %s",conflict(candidate).c_str());
    if(ImGui::Button("Use this key")) assign();
    ImGui::SameLine(); if(ImGui::Button("Capture again")) { candidate=0; listening=true; capture_error.clear(); }
   }
   if(!capture_error.empty()) ImGui::TextWrapped("%s",capture_error.c_str());
   if(ImGui::Button("Cancel binding")) { command=-1; listening=false; candidate=0; }
   ImGui::Separator();
  }
  ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##Binding search","Search actions",search,sizeof(search));
  for(const char *group:{"Items","Action commands","Manage items","Information","Utility","Movement and other actions"}) {
   bool heading=false;
   for(const auto &row:catalog) {
    const auto label=row.value("label",""); const int id=row.value("id",-1);
    if(row.value("group","")!=group || !matches(label,search)) continue;
    if(!heading) { ImGui::Spacing(); DeluxeTheme::section(group); heading=true; }
    ImGui::PushID(id);
    if(ImGui::BeginTable("Binding row",2,ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Action",ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Bindings",ImGuiTableColumnFlags_WidthFixed,ImGui::GetFontSize()*8.f);
    ImGui::TableNextRow(); ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,IM_COL32(22,35,37,160)); ImGui::TableNextColumn();
    ImGui::TextWrapped("%s",label.c_str());
    const auto default_key=row.contains("original_key")?key_name(row.value(mode?"rogue_key":"original_key",0)):row.value(mode?"rogue":"original","");
    ImGui::TextDisabled("Default: %s",default_key.c_str());
    ImGui::TableNextColumn(); if(ImGui::SmallButton("Add key")) { command=id; candidate=0; listening=true; capture_error.clear(); ImGui::SetScrollY(0); }
    int remove_key=0;
    for(const auto &binding:bindings) if(binding.value("mode",-1)==mode && binding.value("command",-1)==id) {
     const int key=binding.value("key",0); ImGui::PushID(key);
     ImGui::TextColored(DeluxeTheme::green(),"%s",key_name(key).c_str()); ImGui::SameLine();
     if(ImGui::SmallButton("Restore")) remove_key=key;
     if(ImGui::IsItemHovered()) ImGui::SetTooltip("Remove this override and restore the underlying Angband binding.");
     ImGui::PopID();
    }
    if(remove_key) remove(remove_key);
    ImGui::EndTable();
    }
    ImGui::PopID();
   }
  }
  ImGui::Spacing(); ImGui::Separator();
  if(ImGui::Button("Restore this keyset")) {
   bindings.erase(std::remove_if(bindings.begin(),bindings.end(),[&](const json &b){return b.value("mode",-1)==mode;}),bindings.end());
   command=-1; listening=false;
  }
  ImGui::TextWrapped("Restore removes AnybandUI overrides, preserving keymaps loaded from Angband preference files.");
 }
};
