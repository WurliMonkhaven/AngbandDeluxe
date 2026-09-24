// Read-only view of the connection's existing, bounded message history.
struct MessageHistory {
 bool open=false;
 char search[256]{};
 int filter=0;
 static std::string group(const json &m) {
  return m.value("system",false)?"system":m.value("group","other");
 }
 static bool accepts(const json &m,int filter,const char *query) {
  static const char *groups[]={"","combat","loot","system","other"};
  const auto text=m.value("text","");
  return text.find_first_not_of(" \t\r\n")!=std::string::npos &&
   (filter==0 || (filter>0 && filter<5 && group(m)==groups[filter])) && matches(text,query);
 }
 template<class C> bool draw(C &c) {
  if(open) { ImGui::OpenPopup("Message history"); open=false; }
  ImGui::SetNextWindowSize(ImVec2(std::min(ImGui::GetMainViewport()->WorkSize.x-24,ImGui::GetFontSize()*62),
   std::min(ImGui::GetMainViewport()->WorkSize.y-24,ImGui::GetFontSize()*34)),ImGuiCond_Appearing);
  bool visible=true,closed=false;
  if(ImGui::BeginPopupModal("Message history",&visible,ImGuiWindowFlags_NoSavedSettings)) {
   ImGui::SetNextItemWidth(std::max(100.f,ImGui::GetContentRegionAvail().x-ImGui::GetFontSize()*14));
   ImGui::InputTextWithHint("##history-search","Search message history",search,sizeof(search));
   ImGui::SameLine();
   if(ImGui::Button("Clear search")) search[0]=0;
   ImGui::SetNextItemWidth(ImGui::GetFontSize()*13);
   ImGui::Combo("Filter",&filter,"All messages\0Combat\0Loot\0System\0Other\0");
   std::vector<const json*> rows;
   for(const auto &m:c.messages) if(accepts(m,filter,search)) rows.push_back(&m);
   ImGui::SameLine(); ImGui::TextDisabled("%d shown · newest first",int(rows.size()));
   ImGui::TextDisabled("Recent messages from this session. Untyped game messages appear in Other.");
   if(ImGui::BeginChild("History entries",ImVec2(0,-ImGui::GetFrameHeightWithSpacing()),ImGuiChildFlags_Borders)) {
    if(rows.empty()) ImGui::TextDisabled("No matching messages.");
    if(ImGui::BeginTable("History rows",2,ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchProp)) {
     ImGui::TableSetupColumn("Type",ImGuiTableColumnFlags_WidthFixed,ImGui::GetFontSize()*5);
     ImGui::TableSetupColumn("Message",ImGuiTableColumnFlags_WidthStretch);
     for(const auto *m:rows) {
      const auto category=group(*m);
      const ImVec4 accent=category=="combat"?ImVec4(1,.62f,.48f,1):category=="loot"?ImVec4(.92f,.77f,.4f,1):category=="system"?ImVec4(.55f,.77f,1,1):ImVec4(.6f,.64f,.68f,1);
      ImGui::TableNextRow(); ImGui::TableNextColumn();
      ImGui::TextColored(accent,"%s",display_label(category).c_str()); ImGui::TableNextColumn();
      auto text=m->value("text",""); const int count=m->value("count",1);
      if(count>1) text+=" (x"+std::to_string(count)+")";
      ImGui::TextWrapped("%s",text.c_str());
     }
     ImGui::EndTable();
    }
   } ImGui::EndChild();
   if(ImGui::Button("Copy results")) {
    std::string text;
    for(const auto *m:rows) { text+=m->value("text",""); if(m->value("count",1)>1) text+=" (x"+std::to_string(m->value("count",1))+")"; text+='\n'; }
    ImGui::SetClipboardText(text.c_str());
   }
   ImGui::SameLine();
   if(ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape) || !visible) { ImGui::CloseCurrentPopup(); closed=true; }
   ImGui::EndPopup();
  }
  return closed;
 }
};
