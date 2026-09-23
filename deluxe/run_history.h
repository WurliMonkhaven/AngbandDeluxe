// Local, read-only post-mortems. Separate files survive save deletion/renaming.
struct DeathTransition {
 static constexpr double duration=.12; // Snap shut; only a few frames of phosphor decay.
 double started=-1;
 bool enabled=false;
 void start(double now,bool animate) { started=now; enabled=animate; }
 float progress(double now) const { return started<0 || !enabled ? -1.f : float(std::clamp((now-started)/duration,0.,1.)); }
 bool finished(double now) const { return started>=0 && (!enabled || now-started>=duration); }
};
struct RunHistory {
 fs::path directory;
 json records=json::array(), current=nullptr;
 std::string error;
 bool browsing=false, archived=false;
 int selected_item=-1;
 char search[128]{};
 static bool valid(const json &r) {
  return r.is_object() && r.value("schema",0)==1 && r.contains("player") && r["player"].is_object()
   && r.contains("items") && r["items"].is_array() && r.contains("messages") && r["messages"].is_array();
 }
 void load() {
  records=json::array(); error.clear();
  try {
   if(!fs::exists(directory)) return;
   for(const auto &file:fs::directory_iterator(directory)) if(file.path().extension()==".json") {
    try {
     if(file.file_size()>4*1024*1024) throw std::runtime_error("Oversized record");
     std::ifstream in(file.path()); json r; in>>r;
     if(!valid(r)) throw std::runtime_error("Invalid record");
     records.push_back(std::move(r));
    } catch(...) { error="Some run records could not be read. Their files have been left untouched."; }
   }
   std::sort(records.begin(),records.end(),[](const json &a,const json &b){return a.value("archive_id","")>b.value("archive_id","");});
  } catch(...) { error="The graveyard could not be read."; }
 }
 bool save() {
  try {
   fs::create_directories(directory);
   const auto filename=directory/(current.at("archive_id").get<std::string>()+".json");
   const auto temp=fs::path(filename.string()+".tmp");
   std::ofstream out(temp,std::ios::binary); out<<current.dump(2); out.close();
   if(!out || !SDL_RenamePath(temp.string().c_str(),filename.string().c_str())) throw std::runtime_error("Write failed");
   error.clear(); return true;
  } catch(...) { error="This run could not be added to the graveyard. Its summary is still available here."; return false; }
 }
 void begin(json report) {
  current=std::move(report); current["schema"]=1;
  current["archive_id"]=std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
  selected_item=-1; archived=false; browsing=false; save();
 }
 // 1 returns to menu/list; 2 starts creation with the previous race/class.
 int draw(bool can_leave=true) {
  if(current.is_null()) {
   ImGui::SeparatorText("Graveyard");
   ImGui::TextWrapped("The lives and last moments of your adventurers.");
   if(ImGui::Button("Back to characters")) { browsing=false; return 1; }
   ImGui::SameLine(); ImGui::SetNextItemWidth(ImGui::GetFontSize()*22);
   ImGui::InputTextWithHint("##Run search","Search characters",search,sizeof(search));
   if(!error.empty()) ImGui::TextWrapped("%s",error.c_str());
   ImGui::BeginChild("Past runs");
   if(records.empty()) ImGui::TextDisabled("Your completed runs will appear here.");
   if(ImGui::BeginTable("Runs",4,ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Character",ImGuiTableColumnFlags_WidthStretch,2);
    ImGui::TableSetupColumn("Fate",ImGuiTableColumnFlags_WidthStretch,2);
    ImGui::TableSetupColumn("Depth",ImGuiTableColumnFlags_WidthStretch,.6f);
    ImGui::TableSetupColumn("Ended",ImGuiTableColumnFlags_WidthStretch,1.3f); ImGui::TableHeadersRow();
    for(const auto &r:records) {
     const auto &p=r["player"];
     const std::string label=p.value("name","Unnamed")+" - "+p.value("race","")+" "+p.value("class","");
     if(!matches(label+" "+r.value("cause",""),search)) continue;
     ImGui::PushID(r.value("archive_id","").c_str()); ImGui::TableNextRow(); ImGui::TableNextColumn();
     if(ImGui::Selectable(label.c_str(),false,ImGuiSelectableFlags_SpanAllColumns)) { current=r; archived=true; selected_item=-1; }
     ImGui::TableNextColumn(); ImGui::TextWrapped("%s",r.value("cause","").c_str());
     ImGui::TableNextColumn(); ImGui::Text("%d",r.value("max_depth",0));
     ImGui::TableNextColumn(); ImGui::TextUnformatted(r.value("ended","").c_str()); ImGui::PopID();
    }
    ImGui::EndTable();
   }
   ImGui::EndChild(); return 0;
  }
  const auto &p=current.at("player");
  ImGui::TextColored(ImVec4(.9f,.68f,.35f,1),"%s",current.value("winner",false)?"A LEGEND REMEMBERED":current.value("retired",false)?"JOURNEY'S END":"HERE ENDS THE TALE");
  ImGui::SetWindowFontScale(1.5f); ImGui::TextWrapped("%s",p.value("name","Unnamed").c_str()); ImGui::SetWindowFontScale(1);
  ImGui::TextWrapped("%s %s  |  Level %d  |  %s",p.value("race","").c_str(),p.value("class","").c_str(),p.value("level",0),current.value("ended","").c_str());
  ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
  ImGui::TextWrapped("%s%s",current.value("winner",false)||current.value("retired",false)?"":"Killed by ",current.value("cause","").c_str());
  int columns=ImGui::GetContentRegionAvail().x>ImGui::GetFontSize()*42?4:2;
  if(ImGui::BeginTable("Run milestones",columns,ImGuiTableFlags_SizingStretchSame)) {
   ImGui::TableNextColumn(); CharacterOverview::metric("Deepest",std::to_string(current.value("max_depth",0))+" ("+std::to_string(current.value("max_depth",0)*50)+" ft)");
   ImGui::TableNextColumn(); CharacterOverview::metric("Turns",std::to_string(current.value("turns",0)));
   ImGui::TableNextColumn(); CharacterOverview::metric("Score",std::to_string(current.value("score",0)),"Angband's calculated points. High-score eligibility remains governed by the engine.");
   ImGui::TableNextColumn(); CharacterOverview::metric("Gold",std::to_string(p.value("gold",0))); ImGui::EndTable();
  }
  if(!error.empty() && !archived) { ImGui::TextWrapped("%s",error.c_str()); if(ImGui::Button("Retry archiving")) save(); }
  ImGui::BeginChild("Run details",ImVec2(0,-ImGui::GetFrameHeightWithSpacing()*2));
  if(ImGui::BeginTabBar("Post-mortem pages")) {
   if(ImGui::BeginTabItem("Final moments")) {
    ImGui::BeginChild("Final messages");
    const auto &messages=current.at("messages");
    // Most recent first: the fatal message stays visible without scrolling.
    int count=0;
    for(const auto &m:messages) {
     if(count++==0) ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,.65f,.45f,1));
     ImGui::TextWrapped("%s%s",m.value("text","").c_str(),m.value("count",1)>1?(" (x"+std::to_string(m.value("count",1))+")").c_str():"");
     if(count==1) { ImGui::PopStyleColor(); ImGui::Spacing(); ImGui::Separator(); }
    }
    ImGui::EndChild(); ImGui::EndTabItem();
   }
   if(ImGui::BeginTabItem("Final belongings")) {
    if(ImGui::BeginTable("Final belongings split",2,ImGuiTableFlags_Resizable)) {
     ImGui::TableSetupColumn("Items",ImGuiTableColumnFlags_WidthStretch,1.7f);
     ImGui::TableSetupColumn("Inspection",ImGuiTableColumnFlags_WidthStretch,1.f);
     ImGui::TableNextColumn(); ImGui::BeginChild("Belongings list");
     if(ImGui::BeginTable("Final inventory",3,ImGuiTableFlags_RowBg|ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV)) {
      ImGui::TableSetupColumn("Name",ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("Location",ImGuiTableColumnFlags_WidthFixed,ImGui::GetFontSize()*7);
      ImGui::TableSetupColumn("Qty",ImGuiTableColumnFlags_WidthFixed,ImGui::GetFontSize()*3);
      ImGui::TableHeadersRow();
      int i=0;
      for(const auto &item:current.at("items")) {
       ImGui::PushID(i); ImGui::TableNextRow(); ImGui::TableNextColumn();
       ImGui::PushStyleColor(ImGuiCol_Text,color(item.value("name_color",1)));
       const auto label=item.value("label","");
       if(ImGui::Selectable(label.c_str(),selected_item==i,ImGuiSelectableFlags_SpanAllColumns)) selected_item=i;
       if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",label.c_str());
       ImGui::PopStyleColor(); ImGui::TableNextColumn();
       auto location=item.value("location","");
       if(!location.empty()) location[0]=char(std::toupper(static_cast<unsigned char>(location[0])));
       ImGui::TextUnformatted(location.c_str()); ImGui::TableNextColumn();
       ImGui::Text("%d",item.value("quantity",1)); ImGui::PopID(); ++i;
      }
      ImGui::EndTable();
     }
     ImGui::EndChild(); ImGui::TableNextColumn(); ImGui::BeginChild("Final inspection");
     if(selected_item>=0 && selected_item<int(current["items"].size())) ItemDescription::draw(current["items"][selected_item]);
     else ImGui::TextWrapped("Select an item to inspect its final, identified properties.");
     ImGui::EndChild(); ImGui::EndTable();
    }
    ImGui::EndTabItem();
   }
   if(ImGui::BeginTabItem("Character sheet")) {
    if(p.contains("character_sheet")) CharacterSheet::contents(p["character_sheet"]);
    ImGui::EndTabItem();
   }
   ImGui::EndTabBar();
  }
  ImGui::EndChild();
  ImGui::BeginDisabled(!can_leave);
  int action=0;
  if(ImGui::Button(archived?"Back to graveyard":"Main menu")) action=1;
  ImGui::SameLine(); if(ImGui::Button("Play again")) action=2;
  ImGui::EndDisabled();
  if(!can_leave) { ImGui::SameLine(); ImGui::TextDisabled("Finishing the game save..."); }
  return action;
 }
};
