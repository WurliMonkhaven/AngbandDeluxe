// A save roster and a deliberately read-only profile. No character is loaded
// until the player chooses Continue; metadata may be absent on older backends.
struct CharacterSelect {
 std::string selected;
 char search[128]{};
 static ImU32 accent(const std::string &save_name) {
  // Fixed hashing keeps colours stable across launches and platforms. Mix the
  // final bits so similar filenames still receive independently varied hues.
  uint32_t hash=2166136261u;
  for(unsigned char c:save_name) { hash^=c; hash*=16777619u; }
  hash^=hash>>16; hash*=0x7feb352du; hash^=hash>>15;
  hash*=0x846ca68bu; hash^=hash>>16;
  const float hue=float(hash & 0x00ffffffu)/16777216.f;
  // Preserve the saturation and value of the original muted green (128,183,141).
  ImVec4 color(0,0,0,1);
  ImGui::ColorConvertHSVtoRGB(hue,55.f/183.f,183.f/255.f,color.x,color.y,color.z);
  // Keep the filename's hue, but give it enough weight on light surfaces.
  if(DeluxeTheme::light_surface()) {
   color.x*=.52f; color.y*=.52f; color.z*=.52f;
  }
  return ImGui::ColorConvertFloat4ToU32(color);
 }
 static void emblem(float height,ImU32 ink) {
  const auto p=ImGui::GetCursorScreenPos(); const float width=ImGui::GetContentRegionAvail().x;
  auto *draw=ImGui::GetWindowDrawList();
  draw->AddRectFilled(p,ImVec2(p.x+width,p.y+height),ImGui::GetColorU32(ImGuiCol_FrameBg),ImGui::GetStyle().FrameRounding);
  const ImVec2 c(p.x+width*.5f,p.y+height*.5f);
  for(int i=0;i<4;++i) {
   float r=height*(.25f+.065f*i);
   draw->AddRect(ImVec2(c.x-r,c.y-r*.75f),ImVec2(c.x+r,c.y+r*.75f),ImGui::GetColorU32(ImGuiCol_Border),ImGui::GetStyle().FrameRounding);
  }
  draw->AddLine(ImVec2(p.x+12,c.y),ImVec2(c.x-height*.48f,c.y),ink);
  draw->AddLine(ImVec2(c.x+height*.48f,c.y),ImVec2(p.x+width-12,c.y),ink);
  const float size=height*.48f;
  const auto text=ImGui::GetFont()->CalcTextSizeA(size,FLT_MAX,0,"@");
  draw->AddText(ImGui::GetFont(),size,ImVec2(c.x-text.x*.5f,c.y-text.y*.5f),ink,"@");
  ImGui::Dummy(ImVec2(width,height));
 }
 // Continue / Rename / Delete / New / Graveyard
 int draw(const json &saves,bool enabled) {
  const float font=ImGui::GetFontSize();
  ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Characters"); ImGui::SameLine();
  float buttons=ImGui::CalcTextSize("Graveyard").x+ImGui::CalcTextSize("New character").x+4*ImGui::GetStyle().FramePadding.x+ImGui::GetStyle().ItemSpacing.x;
  ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),ImGui::GetWindowWidth()-ImGui::GetStyle().WindowPadding.x-buttons));
  int action=0;
  if(ImGui::Button("Graveyard")) action=5;
  ImGui::SameLine(); ImGui::BeginDisabled(!enabled);
  if(ImGui::Button("New character")) action=4;
  ImGui::EndDisabled(); ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
  const json *chosen=nullptr;
  for(const auto &s:saves) if(s.value("id","")==selected) chosen=&s;
  if(!chosen && !saves.empty()) { selected=saves.front().value("id",""); chosen=&saves.front(); }
  ImGui::BeginChild("Character selection",ImVec2(0,-ImGui::GetTextLineHeightWithSpacing()*2));
  if(saves.empty()) {
   emblem(font*11,ImGui::GetColorU32(DeluxeTheme::green())); ImGui::Spacing();
   ImGui::TextUnformatted("A new name. A new descent.");
   ImGui::TextDisabled("Your adventurers will be waiting here between journeys.");
  } else {
   const bool wide=ImGui::GetContentRegionAvail().x>font*52;
   if(ImGui::BeginTable("Roster and profile",wide?2:1,ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Roster",ImGuiTableColumnFlags_WidthStretch,.42f);
    if(wide) ImGui::TableSetupColumn("Profile",ImGuiTableColumnFlags_WidthStretch,.58f);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##Character search","Find a character",search,sizeof(search));
    ImGui::BeginChild("Character roster",ImVec2(0,wide?0:font*14));
    bool found=false;
    for(const auto &s:saves) {
     const auto id=s.value("id",""),name=s.value("name",id),identity=s.value("identity",s.value("description",""));
     if(!matches(name+" "+id+" "+identity,search)) continue;
     found=true; ImGui::PushID(id.c_str());
     const auto p=ImGui::GetCursorScreenPos(); const float width=ImGui::GetContentRegionAvail().x,h=font*5.6f;
     if(ImGui::Selectable("##Character card",selected==id,ImGuiSelectableFlags_None,ImVec2(width,h))) { selected=id; chosen=&s; }
     auto *draw=ImGui::GetWindowDrawList(); const auto ink=accent(id);
     draw->AddRect(p,ImVec2(p.x+width,p.y+h),selected==id?ink:ImGui::GetColorU32(ImGuiCol_Border),4);
     draw->PushClipRect(ImVec2(p.x+8,p.y+4),ImVec2(p.x+width-8,p.y+h-4),true);
     draw->AddText(ImGui::GetFont(),font*1.8f,ImVec2(p.x+10,p.y+font*1.6f),ink,"@");
     const float x=p.x+font*3.2f;
     draw->AddText(ImGui::GetFont(),font*1.15f,ImVec2(x,p.y+font*.6f),ImGui::GetColorU32(ImGuiCol_Text),name.c_str());
     draw->AddText(ImVec2(x,p.y+font*2),ImGui::GetColorU32(ImGuiCol_TextDisabled),identity.c_str());
     std::string status=s.value("dead",false)?"FALLEN":s.contains("depth")?(s.value("depth",0)==0?"IN TOWN":"DEPTH "+std::to_string(s.value("depth",0))):"SAVED JOURNEY";
     if(s.contains("level")) status+="    /    LEVEL "+std::to_string(s.value("level",0));
     draw->AddText(ImVec2(x,p.y+font*3.7f),ink,status.c_str());
     draw->PopClipRect(); ImGui::Spacing(); ImGui::PopID();
    }
    if(!found) ImGui::TextDisabled("No matching characters.");
    ImGui::EndChild(); ImGui::TableNextColumn();
    ImGui::BeginChild("Character profile");
    if(chosen) {
     const auto &s=*chosen; const auto id=s.value("id",""),name=s.value("name",id),identity=s.value("identity","");
     const bool dead=s.value("dead",false);
     emblem(font*10,accent(id)); ImGui::Spacing();
     ImGui::PushFont(nullptr,font*1.8f); ImGui::TextWrapped("%s",name.c_str()); ImGui::PopFont();
     if(!identity.empty()) ImGui::TextWrapped("%s",identity.c_str());
     else ImGui::TextWrapped("%s",s.value("description","Character details unavailable.").c_str());
     ImGui::Spacing(); ImGui::SeparatorText(dead?"A finished journey":"Your journey awaits");
     if(s.contains("level") && ImGui::BeginTable("Saved milestones",2,ImGuiTableFlags_SizingStretchSame)) {
      ImGui::TableNextColumn(); CharacterOverview::metric("Level",std::to_string(s.value("level",0)));
      ImGui::TableNextColumn(); CharacterOverview::metric("Depth",s.value("depth",0)==0?"Town":std::to_string(s.value("depth",0)));
      ImGui::EndTable();
     }
     ImGui::Spacing(); ImGui::TextDisabled("LAST SAVED"); ImGui::TextWrapped("%s",s.value("last_saved","Unknown").c_str());
     ImGui::Spacing(); ImGui::TextDisabled("SAVE FILE"); ImGui::TextWrapped("%s",id.c_str());
     ImGui::Spacing(); ImGui::Spacing(); ImGui::BeginDisabled(!enabled);
     if(ImGui::Button(dead?"Play Again":"Continue journey",ImVec2(-1,ImGui::GetFrameHeight()*1.5f))) action=1;
     ImGui::Spacing(); if(ImGui::Button("Rename")) action=2;
     ImGui::SameLine(); if(ImGui::Button("Delete")) action=3;
     ImGui::EndDisabled();
    }
    ImGui::EndChild(); ImGui::EndTable();
   }
  }
  ImGui::EndChild(); return action;
 }
};
