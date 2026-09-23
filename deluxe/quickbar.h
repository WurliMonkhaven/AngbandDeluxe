// Persistent intents are resolved against the current engine snapshot before use.
struct Quickbar {
 json profiles=json::object();
 std::string profile;
 bool dirty=false;
 struct Action {
  std::string command,item,spell,reason;
  int amount=0; bool mana=false;
 };
 static const char *action_label(const std::string &id) {
  return id=="core.browse"?"Browse spells":id=="core.wield"?"Wield / wear":id=="core.use"?"Use":id=="core.quaff"?"Quaff":id=="core.read"?"Read":id=="core.eat"?"Eat":id=="core.fire"?"Fire":id=="core.throw"?"Throw":id=="core.takeoff"?"Take off":id=="core.drop"?"Drop":id=="core.inscribe"?"Inscribe":"Action";
 }
 json &slots() {
  if(!profiles.is_object()) profiles=json::object();
  auto &s=profiles[profile];
  if(!s.is_array() || s.size()!=10) s=json::array({nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr});
  return s;
 }
 void sync_saves(json &changes) {
  for(const auto &change:changes) {
   const auto from=change.value("save",""),to=change.value("name","");
   if(!profiles.contains(from)) continue;
   if(!to.empty()) profiles[to]=profiles[from];
   profiles.erase(from); dirty=true;
  }
  changes=json::array();
 }
 void load(const json &value) {
  profiles=json::object();
  if(!value.is_object()) return;
  for(auto it=value.begin();it!=value.end();++it) if(it.value().is_array() && it.value().size()==10) {
   bool valid=true;
   for(const auto &b:it.value()) if(!b.is_null() && (!b.is_object() || !b.contains("type") || !b["type"].is_string() || !b.contains("label") || !b["label"].is_string() || !b.contains("command") || !b["command"].is_string())) valid=false;
   for(const auto &b:it.value()) if(b.is_object()) {
    for(const char *field:{"key","spell","spell_name","category"}) if(b.contains(field) && !b[field].is_string()) valid=false;
    if(b.contains("color") && !b["color"].is_number_integer()) valid=false;
   }
   if(valid) profiles[it.key()]=it.value();
  }
 }
 static json item_binding(const json &item,const std::string &command) {
  return {{"type","item"},{"key",item.value("binding_key","")},{"command",command},
   {"label",std::string(action_label(command))+": "+item.value("label","")},{"category",item.value("category","")},{"color",item.value("name_color",1)}};
 }
 static json spell_binding(const json &book,const json &spell) {
  return {{"type","spell"},{"key",book.value("binding_key","")},{"spell",spell.value("id","")},
   {"spell_name",spell.value("label","")},{"command","core.cast"},{"label",spell.value("label","")},{"category","spell"},{"color",14}};
 }
 static json command_binding(const json &command) {
  return {{"type","command"},{"command",command.value("id","")},{"label",command.value("label","")},{"category","command"},{"color",14}};
 }
 static bool normal_play(const Connection &c) {
  return c.state.value("phase","")=="playing" && c.state.value("readiness","")=="ready" &&
   c.prompt.empty() && c.pending_prompt.empty() && !c.state.value("message_pending",false) &&
   !c.state.contains("targeting") && !c.state.value("aiming",false) && !c.state.value("direction_prompt",false);
 }
 static Action resolve(const json &binding,const Connection &c) {
  Action r;
  if(binding.is_null()) { r.reason="Right-click to assign an action."; return r; }
  r.command=binding.value("command","");
  const auto type=binding.value("type","");
  if(type=="command") {
   bool found=false; for(const auto &cmd:c.commands) found|=cmd.value("id","")==r.command;
   if(!found) r.reason="This command is unavailable.";
  } else {
   bool found=false,usable=false;
   if(!c.state.contains("items")) { r.reason="No belongings available."; return r; }
   for(const auto &item:c.state.at("items")) {
    const auto location=item.value("location","");
    if(location=="Floor" || location=="Store" || location=="Home" || binding.value("key","").empty() || item.value("binding_key","")!=binding.value("key","")) continue;
    found=true;
    if(type=="spell") {
     if(!item.value("book_available",false) || !item.contains("spells")) continue;
     for(const auto &spell:item.at("spells")) if(spell.value("id","")==binding.value("spell","") && spell.value("label","")==binding.value("spell_name","")) {
      r.amount=spell.value("mana",0); r.mana=true;
      if(spell.value("can_cast",false)) { r.item=item.value("id",""); r.spell=spell.value("id",""); usable=true; }
     }
    } else {
     bool allowed=false;
     if(item.contains("actions")) for(const auto &action:item.at("actions")) allowed|=action==r.command;
     if(allowed) { if(!usable) r.item=item.value("id",""); usable=true; r.amount+=item.value("quantity",0); }
    }
   }
   if(!usable) r.reason=!found?"No matching item in your belongings.":type=="spell"?"This spell cannot currently be cast.":"This action is not currently available for the item.";
  }
  if(r.reason.empty() && (!c.ready() || !normal_play(c))) r.reason="Finish the current interaction first.";
  return r;
 }
 void slot_choices(const json &binding) {
  auto &s=slots();
  for(int i=0;i<10;++i) {
   std::string label=std::to_string((i+1)%10)+": "+(s[i].is_null()?"Empty":s[i].value("label",""));
   ImGui::PushID(i); if(ImGui::MenuItem(label.c_str())) { s[i]=binding; dirty=true; } ImGui::PopID();
  }
 }
 void assign_menu(const json &binding) {
  if(ImGui::BeginMenu("Assign to quickbar")) { slot_choices(binding); ImGui::EndMenu(); }
 }
 void item_menu(const json &item) {
  if(item.value("binding_key","").empty()) return;
  if(ImGui::BeginMenu("Assign to quickbar")) {
   for(const auto &action:item.value("actions",json::array())) {
    const auto cmd=action.get<std::string>();
    if(ImGui::BeginMenu(action_label(cmd))) { slot_choices(item_binding(item,cmd)); ImGui::EndMenu(); }
   }
   ImGui::EndMenu();
  }
 }
 void choose_binding(Connection &c,int slot) {
  auto &s=slots();
  if(ImGui::BeginMenu("Items")) {
   for(const auto &item:c.state.at("items")) {
    const auto loc=item.value("location","");
    if(loc=="Floor" || loc=="Store" || loc=="Home" || item.value("binding_key","").empty()) continue;
    ImGui::PushID(item.value("id","").c_str());
    if(ImGui::BeginMenu(item.value("label","").c_str())) {
     for(const auto &action:item.value("actions",json::array())) if(ImGui::MenuItem(action_label(action.get<std::string>()))) {
      s[slot]=item_binding(item,action.get<std::string>()); dirty=true;
     }
     ImGui::EndMenu();
    }
    ImGui::PopID();
   }
   ImGui::EndMenu();
  }
  if(ImGui::BeginMenu("Spells")) {
   for(const auto &book:c.state.value("items",json::array())) if(book.value("book_available",false))
    for(const auto &spell:book.value("spells",json::array())) {
     ImGui::PushID(book.value("binding_key","").c_str()); ImGui::PushID(spell.value("id","").c_str());
     if(ImGui::MenuItem(spell.value("label","").c_str())) { s[slot]=spell_binding(book,spell); dirty=true; }
     ImGui::PopID(); ImGui::PopID();
    }
   ImGui::EndMenu();
  }
  if(ImGui::BeginMenu("Commands")) {
   for(const auto &cmd:c.commands) if(ImGui::MenuItem(cmd.value("label","").c_str())) { s[slot]=command_binding(cmd); dirty=true; }
   ImGui::EndMenu();
  }
 }
 static float height() { return ImGui::GetFontSize()*3.2f+ImGui::GetStyle().ItemSpacing.y; }
 static void icon(ImDrawList *draw,ImVec2 a,float size,const json &binding,ImU32 ink) {
  const auto category=binding.value("category","");
  const float x=a.x,y=a.y,s=size;
  if(category.find("potion")!=std::string::npos) {
   draw->AddRect(ImVec2(x+s*.35f,y),ImVec2(x+s*.65f,y+s*.3f),ink,1,0,2);
   draw->AddCircle(ImVec2(x+s*.5f,y+s*.62f),s*.34f,ink,16,2);
   draw->AddLine(ImVec2(x+s*.22f,y+s*.65f),ImVec2(x+s*.78f,y+s*.65f),ink,2);
  } else if(category.find("scroll")!=std::string::npos || category.find("book")!=std::string::npos) {
   draw->AddRect(ImVec2(x+s*.15f,y),ImVec2(x+s*.85f,y+s),ink,2,0,2);
   for(int i=1;i<4;++i) draw->AddLine(ImVec2(x+s*.3f,y+s*i*.2f),ImVec2(x+s*.7f,y+s*i*.2f),ink,1);
  } else if(category.find("wand")!=std::string::npos || category.find("rod")!=std::string::npos || category.find("staff")!=std::string::npos) {
   draw->AddLine(ImVec2(x+s*.2f,y+s*.85f),ImVec2(x+s*.8f,y+s*.15f),ink,3);
   draw->AddCircle(ImVec2(x+s*.8f,y+s*.15f),s*.1f,ink,8,1);
  } else {
   const auto label=binding.value("label","");
   std::string mono;
   bool word=true;
   for(unsigned char ch:label) { if(ch==' ' || ch==':') word=true; else if(word && std::isalpha(ch)) { mono+=char(std::toupper(ch)); word=false; if(mono.size()==2) break; } }
   if(mono.empty()) mono="?";
   const auto text=ImGui::CalcTextSize(mono.c_str()); draw->AddText(ImVec2(x+(s-text.x)/2,y+(s-text.y)/2),ink,mono.c_str());
  }
 }
 int draw(Connection &c) {
  int activated=-1; auto &s=slots();
  const float gap=std::min(4.f,ImGui::GetContentRegionAvail().x/100.f);
  const float width=std::max(1.f,(ImGui::GetContentRegionAvail().x-gap*9)/10.f);
  const float h=height()-ImGui::GetStyle().ItemSpacing.y;
  for(int i=0;i<10;++i) {
   if(i) ImGui::SameLine(0,gap);
   ImGui::PushID(i); const auto a=ImGui::GetCursorScreenPos();
   const auto action=resolve(s[i],c); const bool usable=action.reason.empty();
   if(ImGui::InvisibleButton("Slot",ImVec2(width,h)) && usable) activated=i;
   const bool hovered=ImGui::IsItemHovered();
   auto *draw=ImGui::GetWindowDrawList();
   draw->AddRectFilled(a,ImVec2(a.x+width,a.y+h),ImGui::GetColorU32(hovered?ImVec4(.20f,.28f,.38f,1):ImVec4(.08f,.12f,.17f,1)),3);
   draw->AddRect(a,ImVec2(a.x+width,a.y+h),ImGui::GetColorU32(usable?ImVec4(.35f,.55f,.72f,1):ImVec4(.22f,.25f,.29f,1)),3);
   const auto ink=usable?color(s[i].value("color",14)):ImGui::GetColorU32(ImGuiCol_TextDisabled);
   if(!s[i].is_null()) {
    const float size=std::min(width*.5f,h*.5f);
    icon(draw,ImVec2(a.x+(width-size)/2,a.y+h*.23f),size,s[i],ink);
    if(s[i].value("type","")!="command") {
     const auto count=std::to_string(action.amount)+(action.mana?" MP":"");
     const auto measure=ImGui::CalcTextSize(count.c_str());
     draw->AddText(ImVec2(a.x+std::max(2.f,width-measure.x-3),a.y+h-ImGui::GetFontSize()-2),ink,count.c_str());
    }
   }
   const auto number=std::to_string((i+1)%10); draw->AddText(ImVec2(a.x+3,a.y+2),ImGui::GetColorU32(ImGuiCol_TextDisabled),number.c_str());
   if(hovered) {
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(s[i].is_null()?"Empty slot":s[i].value("label","").c_str());
    if(!action.reason.empty()) ImGui::TextWrapped("%s",action.reason.c_str());
    if(usable) ImGui::TextUnformatted("Click or press the top-row number to activate.");
    ImGui::TextDisabled("Right-click to assign, replace or clear."); ImGui::EndTooltip();
   }
   if(ImGui::BeginPopupContextItem("Slot menu")) {
    choose_binding(c,i);
    if(!s[i].is_null()) { ImGui::Separator(); if(ImGui::MenuItem("Clear slot")) { s[i]=nullptr; dirty=true; } }
    ImGui::EndPopup();
   }
   ImGui::PopID();
  }
  return activated;
 }
};
