// The same timeline renders a live engine snapshot and an immutable graveyard.
struct RunJournal {
 bool open=false,newest=true;
 int category=0;
 char search[128]{};
 static bool accepts(const json &entry,int category,const char *query) {
  static const char *kinds[]={"","level","depth","unique","artifact"};
  return category>=0 && category<5 && (category==0 || entry.value("kind","")==kinds[category]) && matches(entry.value("text",""),query);
 }
 void contents(const json &data) {
  if(!data.contains("entries")) { ImGui::TextDisabled("No journal was recorded for this run."); return; }
  ImGui::SetNextItemWidth(std::max(100.f,ImGui::GetContentRegionAvail().x*.6f));
  ImGui::InputTextWithHint("##Journal search","Search milestones",search,sizeof(search));
  if(ImGui::GetContentRegionAvail().x>ImGui::GetFontSize()*32) ImGui::SameLine();
  ImGui::Checkbox("Newest first",&newest);
  ImGui::SetNextItemWidth(ImGui::GetFontSize()*16);
  ImGui::Combo("##Milestone type",&category,"All milestones\0Level-ups\0New depths\0Unique victories\0Artifacts\0");
  ImGui::Spacing();
  ImGui::BeginChild("Journal timeline");
  const auto &entries=data["entries"];
  int shown=0;
  for(size_t index=0;index<entries.size();++index) {
   const auto &entry=entries[newest?entries.size()-1-index:index];
   if(!accepts(entry,category,search)) continue;
   ++shown;
   const auto kind=entry.value("kind","");
   const char *symbol=kind=="level"?"+":kind=="depth"?">":kind=="unique"?"!":kind=="artifact"?"*":kind=="ending"?"X":"@";
   const ImVec4 tint=kind=="artifact"?ImVec4(.92f,.76f,.4f,1):kind=="unique"||kind=="ending"?ImVec4(.91f,.5f,.43f,1):kind=="depth"?ImVec4(.5f,.73f,.89f,1):ImVec4(.5f,.76f,.6f,1);
   std::string text=entry.value("text",""); if(entry.value("lost",false)) text+=" (lost)";
   const float f=ImGui::GetFontSize(),width=ImGui::GetContentRegionAvail().x,wrap=std::max(f,width-f*5);
   const float text_height=ImGui::CalcTextSize(text.c_str(),nullptr,false,wrap).y;
   const std::string metadata="Lv "+std::to_string(entry.value("level",1))+"  /  "+(entry.value("depth",0)==0?"Town":"Depth "+std::to_string(entry.value("depth",0)))+"  /  Turn "+std::to_string(entry.value("turn",0));
   const float meta_height=ImGui::GetFont()->CalcTextSizeA(f*.8f,FLT_MAX,wrap,metadata.c_str()).y;
   const float height=std::max(f*3.6f,text_height+meta_height+f*1.5f);
   const auto at=ImGui::GetCursorScreenPos();
   ImGui::Dummy({width,height});
   if(!ImGui::IsItemVisible()) continue;
   auto *draw=ImGui::GetWindowDrawList();
   draw->AddLine({at.x+f,at.y},{at.x+f,at.y+height+ImGui::GetStyle().ItemSpacing.y},IM_COL32(53,77,72,200));
   draw->AddRectFilled({at.x,at.y+f*.6f},{at.x+f*2,at.y+f*2.6f},IM_COL32(17,30,32,255),3);
   draw->AddRect({at.x,at.y+f*.6f},{at.x+f*2,at.y+f*2.6f},ImGui::GetColorU32(tint),3);
   draw->AddText({at.x+f-ImGui::CalcTextSize(symbol).x*.5f,at.y+f*1.1f},ImGui::GetColorU32(tint),symbol);
   draw->AddRectFilled({at.x+f*2.7f,at.y},{at.x+width,at.y+height},IM_COL32(16,25,29,255),4);
   draw->AddText(ImGui::GetFont(),f,{at.x+f*3.3f,at.y+f*.55f},ImGui::GetColorU32(tint),text.c_str(),nullptr,wrap);
   draw->AddText(ImGui::GetFont(),f*.8f,{at.x+f*3.3f,at.y+text_height+f*1.1f},IM_COL32(139,160,160,255),metadata.c_str(),nullptr,wrap);
  }
  if(!shown) ImGui::TextDisabled("No matching milestones yet.");
  ImGui::EndChild();
 }
 template<class C> bool draw(C &c) {
  if(open) {
   open=false; c.journal_data=nullptr; search[0]=0; category=0;
   c.journal_request=c.send("journal.get"); ImGui::OpenPopup("Run journal");
  }
  const auto size=ImGui::GetMainViewport()->WorkSize;
  ImGui::SetNextWindowSize({std::min(size.x-24,ImGui::GetFontSize()*48),std::min(size.y-24,ImGui::GetFontSize()*35)},ImGuiCond_Appearing);
  bool visible=true,closed=false;
  if(ImGui::BeginPopupModal("Run journal",&visible,ImGuiWindowFlags_NoSavedSettings)) {
   DeluxeTheme::section("Run summary");
   ImGui::BeginChild("Journal body",{0,-ImGui::GetFrameHeightWithSpacing()});
   if(c.journal_data.is_null()) ImGui::TextDisabled("Opening journal...");
   else if(c.journal_data.contains("error")) ImGui::TextWrapped("%s",c.journal_data.value("error","").c_str());
   else contents(c.journal_data);
   ImGui::EndChild();
   if(ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape) || !visible) { ImGui::CloseCurrentPopup(); closed=true; }
   ImGui::EndPopup();
  }
  return closed;
 }
};
