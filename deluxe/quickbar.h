// Persistent intents are resolved against the current engine snapshot before use.
struct Quickbar {
 json profiles=json::object();
 std::string profile;
 bool dirty=false;
 int editing_slot=-1, draft_icon=0;
 bool open_customize=false;
 float last_slot_width=100.f;
 std::string editing_profile;
 char draft_text[65]{};
 float draft_color[3]{1,1,1};
 static bool carried(const json &item) {
  const auto loc=item.value("location","");
  return loc!="Floor" && loc!="Store" && loc!="Home" && !item.value("binding_key","").empty();
 }
 static bool item_choices(const json &item) { return carried(item) && item.contains("actions") && !item["actions"].empty(); }
 static bool spell_choices(const json &item) { return carried(item) && item.value("book_available",false) && item.contains("spells") && !item["spells"].empty(); }
 static ImU32 appearance_color(const json &b) {
  if(b.contains("custom_color")) { const auto &v=b["custom_color"]; return ImGui::ColorConvertFloat4ToU32(ImVec4(v[0].get<float>(),v[1].get<float>(),v[2].get<float>(),1)); }
  return color(b.value("color",14));
 }
 void begin_customize(int slot) {
  editing_slot=slot; editing_profile=profile;
  const auto &b=slots()[slot];
  const auto style=b.value("icon_style","automatic");
  draft_icon=style=="text"?1:style=="potion"?2:style=="scroll"?3:style=="wand"?4:0;
  const auto text=b.value("custom_text","");
  SDL_strlcpy(draft_text,text.c_str(),sizeof(draft_text));
  const auto rgb=ImGui::ColorConvertU32ToFloat4(appearance_color(b));
  draft_color[0]=rgb.x; draft_color[1]=rgb.y; draft_color[2]=rgb.z;
 }
 json appearance_draft() const {
  static const char *styles[]={"automatic","text","potion","scroll","wand"};
  return {{"icon_style",styles[draft_icon]},{"custom_text",draft_text},{"custom_color",json::array({draft_color[0],draft_color[1],draft_color[2]})}};
 }
 void save_customize() {
  if(editing_profile!=profile || editing_slot<0 || slots()[editing_slot].is_null()) return;
  slots()[editing_slot].update(appearance_draft()); dirty=true;
 }
 bool customize_window() {
  if(open_customize) { ImGui::OpenPopup("Customize quickbar slot"); open_customize=false; }
  bool closed=false;
  ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize()*24,0),ImGuiCond_Appearing);
  if(ImGui::BeginPopupModal("Customize quickbar slot",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
   ImGui::Text("Slot %d",(editing_slot+1)%10);
   int mode=draft_icon<2?draft_icon:2;
   if(ImGui::Combo("Appearance",&mode,"Automatic\0Custom text\0Icon\0")) draft_icon=mode;
   if(mode==2) {
    const char *names[]={"Potion","Scroll / book","Wand"};
    const char *styles[]={"potion","scroll","wand"};
    const float tile=ImGui::GetFontSize()*2.5f;
    for(int i=0;i<3;++i) {
     if(i) ImGui::SameLine();
     ImGui::PushID(i); const auto a=ImGui::GetCursorScreenPos();
     if(ImGui::InvisibleButton("Choose icon",ImVec2(tile,tile))) draft_icon=i+2;
     auto *draw=ImGui::GetWindowDrawList();
     draw->AddRectFilled(a,ImVec2(a.x+tile,a.y+tile),ImGui::GetColorU32(draft_icon==i+2?ImGuiCol_ButtonActive:ImGuiCol_Button),3);
     icon(draw,ImVec2(a.x+tile*.2f,a.y+tile*.2f),tile*.6f,json{{"icon_style",styles[i]}},ImGui::GetColorU32(ImGuiCol_Text));
     if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",names[i]);
     ImGui::PopID();
    }
    ImGui::TextDisabled("%s",names[draft_icon-2]);
   }
   if(draft_icon==1) { ImGui::InputText("Text / symbol",draft_text,sizeof(draft_text)); ImGui::TextDisabled("Text wraps to fit the slot."); }
   ImGui::ColorEdit3("Colour",draft_color);
   auto preview=slots()[editing_slot]; preview.update(appearance_draft());
   ImGui::TextUnformatted("Preview");
   const auto pos=ImGui::GetCursorScreenPos(); const float w=std::min(last_slot_width,ImGui::GetContentRegionAvail().x),h=height()-ImGui::GetStyle().ItemSpacing.y;
   ImGui::Dummy(ImVec2(w,h));
   auto *draw=ImGui::GetWindowDrawList();
   draw->AddRectFilled(pos,ImVec2(pos.x+w,pos.y+h),ImGui::GetColorU32(ImGuiCol_FrameBg),3);
   content(draw,pos,w,h,preview,appearance_color(preview));
   draw->AddText(ImVec2(pos.x+3,pos.y+2),ImGui::GetColorU32(ImGuiCol_TextDisabled),std::to_string((editing_slot+1)%10).c_str());
   ImGui::Spacing();
   if(ImGui::Button("Reset appearance")) {
    draft_icon=0; draft_text[0]=0;
    const auto rgb=ImGui::ColorConvertU32ToFloat4(color(slots()[editing_slot].value("color",14)));
    draft_color[0]=rgb.x; draft_color[1]=rgb.y; draft_color[2]=rgb.z;
   }
   ImGui::Separator();
   if(ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) { ImGui::CloseCurrentPopup(); closed=true; }
   ImGui::SameLine();
   if(ImGui::Button("Save and Close")) { save_customize(); ImGui::CloseCurrentPopup(); closed=true; }
   ImGui::EndPopup();
  }
  return closed;
 }
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
    for(const char *field:{"key","spell","spell_name","category","icon_style","custom_text"}) if(b.contains(field) && !b[field].is_string()) valid=false;
    if(b.contains("color") && !b["color"].is_number_integer()) valid=false;
    if(b.contains("custom_color")) {
     const auto &rgb=b["custom_color"];
     if(!rgb.is_array() || rgb.size()!=3) valid=false;
     else for(const auto &channel:rgb) if(!channel.is_number() || channel.get<float>()<0 || channel.get<float>()>1) valid=false;
    }
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
   if(!usable) r.reason=!found?(type=="spell"?"Spellbook no longer carried.":"Item no longer carried."):type=="spell"?"This spell cannot currently be cast.":"This action is not currently available for the item.";
  }
  if(r.reason.empty() && (!c.ready() || !normal_play(c))) r.reason="Finish the current interaction first.";
  return r;
 }
 static json tooltip_details(const json &binding,const Connection &c,const Action &action) {
  json result={{"title",binding.is_null()?"Empty slot":binding.value("label","")},
   {"facts",json::array()},{"description",""},{"reason",action.reason},{"warning",""}};
  if(binding.is_null()) return result;
  if(binding.value("type","")=="command" && binding.value("command","")=="core.fire" && c.state.contains("items")) {
   bool ammunition=false;
   for(const auto &item:c.state["items"]) if(carried(item))
    for(const auto &command:item.value("actions",json::array())) if(command=="core.fire") ammunition=true;
   if(!ammunition) result["warning"]="No carried ammunition is currently eligible for firing. Angband may still offer ammunition on the ground.";
  }
  const json *chosen=nullptr;
  if(c.state.contains("items")) for(const auto &item:c.state["items"]) {
   if(!carried(item) || binding.value("key","").empty() || item.value("binding_key","")!=binding.value("key","")) continue;
   if(!chosen || item.value("id","")==action.item) chosen=&item;
   if(item.value("id","")==action.item) break;
  }
  if(!chosen) return result;
  auto &facts=result["facts"];
  if(binding.value("type","")=="spell") {
   for(const auto &spell:chosen->value("spells",json::array()))
    if(spell.value("id","")==binding.value("spell","") && spell.value("label","")==binding.value("spell_name","")) {
     result["title"]=spell.value("label","");
     facts.push_back("Mana "+std::to_string(spell.value("mana",0))+"   Failure "+std::to_string(spell.value("failure",0))+"%");
     facts.push_back("Level "+std::to_string(spell.value("level",0))+"   "+spell.value("status",""));
     result["description"]=spell.value("description","");
     const auto info=spell.value("info",""); if(!info.empty()) facts.push_back(info);
     if(!spell.value("can_cast",false)) {
      const auto status=spell.value("status","");
      if(!spell.value("cast_reason","").empty()) result["reason"]=spell["cast_reason"];
      else if(status=="Forgotten") result["reason"]="This spell has been forgotten.";
      else if(status=="Unknown") result["reason"]="This spell has not been learned.";
      else if(status=="Difficult") result["reason"]="Your level is too low for this spell.";
      else if(status=="Illegible") result["reason"]="You cannot learn this spell.";
     } else if(spell.value("low_mana",false)) result["warning"]="Not enough mana — Angband will ask whether to attempt it anyway.";
     break;
    }
  } else {
   result["title"]=std::string(action_label(binding.value("command","")))+": "+chosen->value("label","");
   facts.push_back("Quantity "+std::to_string(chosen->value("quantity",0))+"   "+chosen->value("location",""));
   if(chosen->contains("charges")) facts.push_back("Charges "+std::to_string(chosen->value("charges",0))+" (this stack)");
   if(binding.value("command","")=="core.use" && chosen->contains("charges") && chosen->value("charges",0)==0)
    result["warning"]="No charges remaining in this stack.";
   if(chosen->contains("charging")) {
    const int charging=chosen->value("charging",0);
    facts.push_back(charging?"Recharging: "+std::to_string(charging):"Fully charged");
    if(binding.value("command","")=="core.use" && charging>=chosen->value("quantity",1)) result["warning"]="This item is still recharging.";
   }
   for(const auto &section:chosen->value("description_sections",json::array()))
    if(section.value("id","")=="use") { result["description"]=section.value("text",""); break; }
   if(result["description"]=="") result["description"]=chosen->value("description","");
   const auto inscription=chosen->value("inscription",""); if(!inscription.empty()) facts.push_back("Inscription: "+inscription);
  }
  return result;
 }
 static void tooltip(const json &binding,const Connection &c,const Action &action,int slot) {
  const auto details=tooltip_details(binding,c,action);
  ImGui::BeginTooltip();
  const float width=std::min(ImGui::GetFontSize()*30,ImGui::GetMainViewport()->WorkSize.x-32);
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+width);
  ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.65f,.82f,1,1));
  ImGui::TextWrapped("%s",details.value("title","").c_str()); ImGui::PopStyleColor();
  ImGui::TextDisabled("Quickbar · %d",(slot+1)%10);
  if(!details["facts"].empty()) {
   ImGui::Separator();
   for(const auto &fact:details["facts"]) ImGui::TextWrapped("%s",fact.get_ref<const std::string&>().c_str());
  }
  auto description=details.value("description","");
  if(!description.empty()) {
   // Keep long equipment lore from producing a tooltip taller than the screen.
   bool shortened=description.size()>600;
   if(shortened) { size_t end=description.rfind(' ',600); description.resize(end==std::string::npos?600:end); description+="..."; }
   ImGui::Separator(); ImGui::TextWrapped("%s",description.c_str());
   if(shortened) ImGui::TextDisabled("%s",binding.value("type","")=="spell"?"Full description in Spells.":"Full description in Inventory.");
  }
  for(const char *field:{"reason","warning"}) {
   const auto text=details.value(field,"");
   if(!text.empty()) { ImGui::Separator(); ImGui::TextWrapped("%s",text.c_str()); }
  }
  ImGui::Separator();
  if(action.reason.empty()) ImGui::TextWrapped("Click or press the top-row number to activate.");
  ImGui::TextDisabled("Right-click to customize or assign.");
  ImGui::PopTextWrapPos(); ImGui::EndTooltip();
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
  if(!item_choices(item)) return;
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
  bool has_items=false,has_spells=false;
  if(c.state.contains("items")) for(const auto &item:c.state["items"]) { has_items|=item_choices(item); has_spells|=spell_choices(item); }
  if(has_items && ImGui::BeginMenu("Items")) {
   for(const auto &item:c.state.at("items")) {
    if(!item_choices(item)) continue;
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
  if(has_spells && ImGui::BeginMenu("Spells")) {
   for(const auto &book:c.state.value("items",json::array())) if(spell_choices(book))
    for(const auto &spell:book.value("spells",json::array())) {
     ImGui::PushID(book.value("binding_key","").c_str()); ImGui::PushID(spell.value("id","").c_str());
     if(ImGui::MenuItem(spell.value("label","").c_str())) { s[slot]=spell_binding(book,spell); dirty=true; }
     ImGui::PopID(); ImGui::PopID();
    }
   ImGui::EndMenu();
  }
  if(!c.commands.empty() && ImGui::BeginMenu("Commands")) {
   for(const auto &cmd:c.commands) if(ImGui::MenuItem(cmd.value("label","").c_str())) { s[slot]=command_binding(cmd); dirty=true; }
   ImGui::EndMenu();
  }
 }
 static float height() { return ImGui::GetFontSize()*3.2f+ImGui::GetStyle().ItemSpacing.y; }
 struct TextLayout { float font_size=0; std::vector<std::string> lines; };
 static TextLayout text_layout(const std::string &text,float width,float height) {
  TextLayout result;
  if(text.empty() || width<=0 || height<=0) return result;
  auto *font=ImGui::GetFont();
  // Prefer normal-sized words, wrapping before reducing the font size.
  for(float scale=1.f;scale>=.2f;scale-=.025f) {
   result.font_size=ImGui::GetFontSize()*scale; result.lines.clear();
   const char *p=text.c_str(),*end=p+text.size();
   while(p<end) {
    const char *next=font->CalcWordWrapPosition(result.font_size,p,end,width);
    if(next==p) { next=p; size_t left=size_t(end-p); SDL_StepUTF8(&next,&left); }
    result.lines.emplace_back(p,next); p=next;
    while(p<end && (*p==' ' || *p=='\n' || *p=='\r')) ++p;
   }
   if(result.lines.size()*result.font_size<=height) break;
  }
  return result;
 }
 static void content(ImDrawList *draw,ImVec2 a,float width,float h,const json &binding,ImU32 ink) {
  if(binding.value("icon_style","")=="text") {
   const float padding=std::max(3.f,ImGui::GetFontSize()*.2f);
   const float top=ImGui::GetFontSize()+3, bottom=binding.value("type","")=="command"?padding:ImGui::GetFontSize()+3;
   const float w=std::max(1.f,width-2*padding),available=std::max(1.f,h-top-bottom);
   auto layout=text_layout(binding.value("custom_text",""),w,available);
   float y=a.y+top+(available-layout.lines.size()*layout.font_size)/2;
   draw->PushClipRect(ImVec2(a.x+padding,a.y+top),ImVec2(a.x+width-padding,a.y+h-bottom),true);
   for(const auto &line:layout.lines) {
    float measure=ImGui::GetFont()->CalcTextSizeA(layout.font_size,FLT_MAX,0,line.c_str()).x;
    draw->AddText(ImGui::GetFont(),layout.font_size,ImVec2(a.x+(width-measure)/2,y),ink,line.c_str()); y+=layout.font_size;
   }
   draw->PopClipRect();
  } else {
   const float size=std::min(width*.5f,h*.5f);
   icon(draw,ImVec2(a.x+(width-size)/2,a.y+h*.23f),size,binding,ink);
  }
 }
 static void icon(ImDrawList *draw,ImVec2 a,float size,const json &binding,ImU32 ink) {
  const auto style=binding.value("icon_style","automatic");
  const auto category=style=="automatic"?binding.value("category",""):style;
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
  last_slot_width=width;
  for(int i=0;i<10;++i) {
   if(i) ImGui::SameLine(0,gap);
   ImGui::PushID(i); const auto a=ImGui::GetCursorScreenPos();
   const auto action=resolve(s[i],c); const bool usable=action.reason.empty();
   if(ImGui::InvisibleButton("Slot",ImVec2(width,h)) && usable) activated=i;
   const bool hovered=ImGui::IsItemHovered();
   auto *draw=ImGui::GetWindowDrawList();
   draw->AddRectFilled(a,ImVec2(a.x+width,a.y+h),ImGui::GetColorU32(hovered?ImVec4(.20f,.28f,.38f,1):ImVec4(.08f,.12f,.17f,1)),3);
   draw->AddRect(a,ImVec2(a.x+width,a.y+h),ImGui::GetColorU32(usable?ImVec4(.35f,.55f,.72f,1):ImVec4(.22f,.25f,.29f,1)),3);
   const auto ink=usable?appearance_color(s[i]):ImGui::GetColorU32(ImGuiCol_TextDisabled);
   if(!s[i].is_null()) {
    content(draw,a,width,h,s[i],ink);
    if(s[i].value("type","")!="command") {
     const auto count=std::to_string(action.amount)+(action.mana?" MP":"");
     const auto measure=ImGui::CalcTextSize(count.c_str());
     draw->AddText(ImVec2(a.x+std::max(2.f,width-measure.x-3),a.y+h-ImGui::GetFontSize()-2),ink,count.c_str());
    }
   }
   const auto number=std::to_string((i+1)%10); draw->AddText(ImVec2(a.x+3,a.y+2),ImGui::GetColorU32(ImGuiCol_TextDisabled),number.c_str());
   if(hovered) tooltip(s[i],c,action,i);
   if(ImGui::BeginPopupContextItem("Slot menu")) {
    choose_binding(c,i);
    if(!s[i].is_null()) { ImGui::Separator(); if(ImGui::MenuItem("Customize")) { begin_customize(i); open_customize=true; } if(ImGui::MenuItem("Clear slot")) { s[i]=nullptr; dirty=true; } }
    ImGui::EndPopup();
   }
   ImGui::PopID();
  }
  return activated;
 }
};
