// Engine-supplied spell details shared by the browser and selection prompts.
struct SpellPanel {
 std::string book_name, selected, prompt_selected;

 static const json *find(const json &spells,const std::string &id) {
  for(const auto &s:spells) if(s.value("id","")==id) return &s;
  return nullptr;
 }
 static void description(const json &spell) {
  ImGui::TextWrapped("%s",spell.value("label","").c_str());
  ImGui::Text("Level %d · Mana %d · Failure %d%%",spell.value("level",0),spell.value("mana",0),spell.value("failure",0));
  ImGui::TextUnformatted(spell.value("status","").c_str());
  if(spell.value("low_mana",false) && spell.value("can_cast",false))
   ImGui::TextColored(ImVec4(1.f,.65f,.3f,1),"Not enough mana — confirmation required.");
  ImGui::Spacing(); ImGui::TextWrapped("%s",spell.value("description","").c_str());
  const auto info=spell.value("info","");
  if(!info.empty()) { ImGui::Spacing(); ImGui::TextWrapped("%s",info.c_str()); }
 }
 static std::string list(const json &spells,std::string &selection,float height,bool shortcuts=false,Quickbar *bar=nullptr,const json *book=nullptr) {
  std::string activated;
  if(!find(spells,selection)) selection=spells.empty()?"":spells.front().value("id","");
  if(ImGui::BeginTable("Spells",4,ImGuiTableFlags_Resizable|ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY,ImVec2(0,height))) {
   ImGui::TableSetupColumn("Spell",ImGuiTableColumnFlags_WidthStretch,3);
   ImGui::TableSetupColumn("Mana",ImGuiTableColumnFlags_WidthFixed,ImGui::CalcTextSize("Mana").x);
   ImGui::TableSetupColumn("Fail",ImGuiTableColumnFlags_WidthFixed,ImGui::CalcTextSize("100%").x);
   ImGui::TableSetupColumn("State",ImGuiTableColumnFlags_WidthStretch,1.3f);
   ImGui::TableSetupScrollFreeze(0,1); ImGui::TableHeadersRow();
   for(const auto &s:spells) {
    const auto id=s.value("id","");
    auto label=s.value("label","");
    if(shortcuts && s.contains("shortcut")) label=s.value("shortcut","")+"  "+label;
    ImGui::PushID(id.c_str()); ImGui::TableNextRow(); ImGui::TableNextColumn();
    const bool cast=s.value("can_cast",false), study=s.value("can_study",false);
    ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(cast||study?ImGuiCol_Text:ImGuiCol_TextDisabled));
    if(DeluxeTheme::table_choice(label.c_str(),selection==id,ImGuiSelectableFlags_SpanAllColumns|ImGuiSelectableFlags_AllowDoubleClick)) {
     selection=id; if(ImGui::IsMouseDoubleClicked(0)) activated=id;
    }
    if(bar && book && Quickbar::carried(*book)) bar->drag_source(Quickbar::spell_binding(*book,s));
    if(bar && book && ImGui::BeginPopupContextItem("Spell actions")) { bar->assign_menu(Quickbar::spell_binding(*book,s)); ImGui::EndPopup(); }
    if(ImGui::IsItemHovered() && !ImGui::GetDragDropPayload()) ImGui::SetTooltip("%s",s.value("label","").c_str());
    ImGui::TableNextColumn(); ImGui::Text("%d",s.value("mana",0));
    ImGui::TableNextColumn(); ImGui::Text("%d%%",s.value("failure",0));
    ImGui::TableNextColumn(); ImGui::TextUnformatted(cast?"Castable":study?"Learnable":s.value("status","").c_str());
    ImGui::PopStyleColor(); ImGui::PopID();
   }
   ImGui::EndTable();
  }
  return activated;
 }
 bool draw(Connection &c,Quickbar *bar=nullptr) {
  std::vector<const json*> books;
  for(const auto &o:c.state.at("items")) if(o.value("book_available",false) && o.contains("spells")) books.push_back(&o);
  if(books.empty()) { ImGui::TextWrapped("No readable spellbooks in your pack or on this tile."); return false; }
  const int new_spells=c.state.at("player").value("new_spells",0);
  if(new_spells>0) ImGui::Text("Available to learn: %d",new_spells);
  const json *book=books.front();
  for(const auto *candidate:books) if(candidate->value("label","")==book_name) book=candidate;
  book_name=book->value("label","");
  ImGui::SetNextItemWidth(-1);
  if(ImGui::BeginCombo("##Spellbook",book_name.c_str())) {
   for(const auto *candidate:books) {
    ImGui::PushID(candidate->value("id","").c_str());
    if(ImGui::Selectable(candidate->value("label","").c_str(),candidate==book)) {
     book=candidate; book_name=book->value("label",""); selected.clear();
    }
    ImGui::PopID();
   }
   ImGui::EndCombo();
  }
  const auto &spells=book->at("spells");
  list(spells,selected,(ImGui::GetTextLineHeight()+2*ImGui::GetStyle().CellPadding.y)*float(std::clamp(int(spells.size())+1,2,10)),false,bar,book);
  const auto *spell=find(spells,selected);
  bool acted=false;
  if(spell) {
   ImGui::BeginDisabled(!c.ready() || !spell->value("can_cast",false));
   if(ImGui::Button("Cast")) { c.command("core.cast",book->value("id",""),selected); acted=true; }
   ImGui::EndDisabled(); ImGui::SameLine();
   const bool choose=book->value("choose_spells",true);
   bool learnable=spell->value("can_study",false);
   if(!choose) for(const auto &s:spells) learnable|=s.value("can_study",false);
   ImGui::BeginDisabled(!c.ready() || !learnable);
   if(ImGui::Button(choose?"Study":"Study book")) { c.command("core.study",book->value("id",""),choose?selected:""); acted=true; }
   ImGui::EndDisabled();
   if(!choose && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Your class learns a random eligible spell from this book.");
   ImGui::Separator(); description(*spell);
  }
  return acted;
 }
 bool prompt(Connection &c,bool fresh) {
  const auto &choices=c.prompt.at("choices");
  const bool browse=c.prompt.value("browse",false);
  if(fresh) prompt_selected.clear();
  std::string answer=list(choices,prompt_selected,ImGui::GetTextLineHeightWithSpacing()*9,true);
  int selected_index=0;
  for(int i=0;i<int(choices.size());++i) if(choices[i].value("id","")==prompt_selected) selected_index=i;
  if(ImGui::IsKeyPressed(ImGuiKey_UpArrow)) selected_index=std::max(0,selected_index-1);
  if(ImGui::IsKeyPressed(ImGuiKey_DownArrow)) selected_index=std::min(int(choices.size())-1,selected_index+1);
  if(!choices.empty()) prompt_selected=choices[selected_index].value("id","");
  for(auto ch:ImGui::GetIO().InputQueueCharacters) for(const auto &s:choices) {
   auto key=s.value("shortcut","");
   if(key.size()==1 && ch==key[0]) { prompt_selected=s.value("id",""); if(!browse) answer=prompt_selected; }
  }
  ImGui::BeginChild("Spell description",ImVec2(0,ImGui::GetTextLineHeightWithSpacing()*7));
  if(const auto *s=find(choices,prompt_selected)) description(*s);
  ImGui::EndChild();
  if(browse) {
   if(ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) { c.answer(nullptr); return true; }
  } else {
   if(ImGui::Button("Choose") || ImGui::IsKeyPressed(ImGuiKey_Enter)) answer=prompt_selected;
   ImGui::SameLine();
   if(ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) { c.answer(nullptr); return true; }
   if(!answer.empty()) { c.answer(answer); return true; }
  }
  return false;
 }
};
