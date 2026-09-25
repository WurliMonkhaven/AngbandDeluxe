#include <set>
// Native knowledge navigation. Records and recall text come from the backend.
struct KnowledgeBrowser {
 bool request_open=false,known_only=true;
 std::string category="creatures",group;
 int selected=-1;
 char search[128]{};
 void load(Connection &c,const char *next) {
  category=next; group.clear(); search[0]=0; selected=-1;
  c.knowledge_list=nullptr; c.knowledge_detail=nullptr; c.knowledge_detail_request.clear();
  c.knowledge_list_request=c.send("knowledge.list",{{"category",category}});
 }
 void open(Connection &c) { request_open=true; load(c,category.c_str()); }
 void select(Connection &c,int id) {
  selected=id; c.knowledge_detail=nullptr;
  c.knowledge_detail_request=c.send("knowledge.get",{{"category",category},{"id",id}});
 }
 static bool matches(std::string text,std::string query) {
  const auto lower=[](unsigned char c) { return char(std::tolower(c)); };
  std::transform(text.begin(),text.end(),text.begin(),lower);
  std::transform(query.begin(),query.end(),query.begin(),lower);
  return text.find(query)!=std::string::npos;
 }
 void contents(Connection &c) {
  if(ImGui::BeginTabBar("Knowledge categories")) {
   const char *ids[]={"creatures","items","artifacts","terrain"};
   const char *titles[]={"Creatures","Items","Artifacts","Terrain"};
   for(int i=0;i<4;++i) if(ImGui::BeginTabItem(titles[i])) {
    if(category!=ids[i]) load(c,ids[i]);
    ImGui::EndTabItem();
   }
   ImGui::EndTabBar();
  }
  ImGui::Checkbox("Known only",&known_only);
  if(ImGui::IsItemHovered()) ImGui::SetTooltip("Hide item types whose identity you have not learned.");
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x*.55f);
  ImGui::InputTextWithHint("##Knowledge search","Search by name or category",search,sizeof(search)); ImGui::SameLine();
  ImGui::SetNextItemWidth(-1);
  if(ImGui::BeginCombo("##Knowledge group",group.empty()?"All categories":display_label(group).c_str())) {
   if(ImGui::Selectable("All categories",group.empty())) group.clear();
   std::set<std::string> groups;
   if(c.knowledge_list.is_object()) for(const auto &row:c.knowledge_list.value("entries",json::array())) groups.insert(row.value("group","Other"));
   for(const auto &name:groups) if(ImGui::Selectable(display_label(name).c_str(),group==name)) group=name;
   ImGui::EndCombo();
  }
  if(!ImGui::BeginTable("Knowledge panes",2,ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV)) return;
  ImGui::TableSetupColumn("Entries",ImGuiTableColumnFlags_WidthStretch,.38f);
  ImGui::TableSetupColumn("Details",ImGuiTableColumnFlags_WidthStretch,.62f);
  ImGui::TableNextColumn();
  ImGui::BeginChild("Knowledge entries");
  if(c.knowledge_list.is_null()) ImGui::TextDisabled("Loading entries...");
  else if(c.knowledge_list.contains("error")) ImGui::TextWrapped("%s",c.knowledge_list.value("error","").c_str());
  else {
   auto entries=c.knowledge_list.value("entries",json::array());
   std::vector<const json *> visible;
   for(const auto &row:entries) if((!known_only || row.value("known",true)) && (group.empty() || row.value("group","")==group) && matches(row.value("name","")+" "+row.value("group",""),search)) visible.push_back(&row);
   ImGui::TextDisabled("%zu entries",visible.size()); ImGui::Separator();
   if(visible.empty()) ImGui::TextWrapped(entries.empty()?"No entries discovered yet. Your knowledge grows as you explore.":"No matching entries.");
   bool selection_visible=false;
   for(const auto *row:visible) if(row->value("id",-1)==selected) selection_visible=true;
   if(!selection_visible) { selected=-1; c.knowledge_detail=nullptr; c.knowledge_detail_request.clear(); if(!visible.empty()) select(c,visible.front()->value("id",-1)); }
   ImGuiListClipper clipper; clipper.Begin(int(visible.size()));
   while(clipper.Step()) for(int i=clipper.DisplayStart;i<clipper.DisplayEnd;++i) {
    const auto &row=*visible[i]; const int id=row.value("id",-1); ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_Text,ui_color(row.value("color",1)));
    if(ImGui::Selectable(row.value("name","").c_str(),selected==id)) select(c,id);
    ImGui::PopStyleColor();
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s",row.value("name","").c_str(),display_label(row.value("group","")).c_str());
    ImGui::PopID();
   }
  }
  ImGui::EndChild(); ImGui::TableNextColumn();
  ImGui::BeginChild("Knowledge detail");
  const auto &detail=c.knowledge_detail;
  if(detail.is_null()) ImGui::TextDisabled(selected<0?"Select an entry to explore.":"Loading details...");
  else if(detail.contains("error")) ImGui::TextWrapped("%s",detail.value("error","").c_str());
  else {
   details(detail);
  }
  ImGui::EndChild(); ImGui::EndTable();
 }
 static void details(const json &detail) {
   ImGui::PushStyleColor(ImGuiCol_Text,ui_color(detail.value("color",1)));
   ImGui::TextWrapped("%s",detail.value("name","").c_str()); ImGui::Separator(); ImGui::PopStyleColor();
   ImGui::TextDisabled("%s",display_label(detail.value("group","")).c_str()); ImGui::Spacing();
   const auto stats=detail.value("stats",json::array());
   if(!stats.empty() && ImGui::BeginTable("Knowledge stats",int(stats.size()),ImGuiTableFlags_SizingStretchSame|ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_RowBg)) {
    ImGui::TableNextRow();
    for(const auto &stat:stats) { ImGui::TableNextColumn(); ImGui::TextDisabled("%s",stat.value("label","").c_str()); ImGui::Text("%d",stat.value("value",0)); }
    ImGui::EndTable(); ImGui::Spacing();
   }
   ImGui::PushID(detail.value("id",-1));
   const auto sections=detail.value("description_sections",json::array());
   if(sections.empty()) ImGui::TextWrapped("No further information is known yet.");
   for(const auto &section:sections) if(ImGui::CollapsingHeader(section.value("title","").c_str(),ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Spacing(); ImGui::TextWrapped("%s",section.value("text","").c_str()); ImGui::Spacing();
   }
   ImGui::PopID();
 }
 bool draw(Connection &c) {
  if(request_open) { ImGui::OpenPopup("Knowledge"); request_open=false; }
  const auto screen=ImGui::GetIO().DisplaySize;
  ImGui::SetNextWindowSize(ImVec2(std::min(screen.x*.94f,ImGui::GetFontSize()*76),screen.y*.86f),ImGuiCond_Appearing);
  ImGui::SetNextWindowPos(ImVec2(screen.x*.5f,screen.y*.5f),ImGuiCond_Appearing,ImVec2(.5f,.5f));
  bool show=true,closed=false;
  if(ImGui::BeginPopupModal("Knowledge",&show,ImGuiWindowFlags_NoSavedSettings)) {
   ImGui::BeginChild("Knowledge body",ImVec2(0,-ImGui::GetFrameHeightWithSpacing())); contents(c); ImGui::EndChild();
   ImGui::Separator();
   ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),ImGui::GetWindowWidth()-ImGui::GetStyle().WindowPadding.x-ImGui::CalcTextSize("Close").x-2*ImGui::GetStyle().FramePadding.x));
   if(ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape) || !c.connected) { ImGui::CloseCurrentPopup(); closed=true; }
   ImGui::EndPopup();
  }
  return closed || !show;
 }
};
