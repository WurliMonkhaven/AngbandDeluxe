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
 static int roster_actions(bool enabled) {
  const float f=ImGui::GetFontSize(),gap=ImGui::GetStyle().ItemSpacing.x;
  const float width=std::max(1.f,(ImGui::GetContentRegionAvail().x-gap)*.5f),height=f*6;
  int action=0;
  for(int i=0;i<2;++i) {
   if(i) ImGui::SameLine();
   ImGui::BeginDisabled(i==1 && !enabled);
   const auto p=ImGui::GetCursorScreenPos();
   if(ImGui::Button(i?"##New character":"##Graveyard",{width,height})) action=i?4:5;
   auto *d=ImGui::GetWindowDrawList();
   const auto ink=ImGui::GetColorU32(i?DeluxeTheme::green():ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
   const ImVec2 c(p.x+width*.5f,p.y+f*2.25f);
   const float stroke=std::max(2.f,f*.13f);
   if(i) {
    d->AddLine({c.x-f*.8f,c.y},{c.x+f*.8f,c.y},ink,stroke*1.5f);
    d->AddLine({c.x,c.y-f*.8f},{c.x,c.y+f*.8f},ink,stroke*1.5f);
   } else {
    // Draw the headstone directly so it works with every selectable font.
    const float r=f*.75f,top=c.y-f*.4f,bottom=c.y+f*.95f;
    d->PathLineTo({c.x-r,bottom}); d->PathLineTo({c.x-r,top});
    d->PathArcTo({c.x,top},r,3.14159265f,6.2831853f,20);
    d->PathLineTo({c.x+r,bottom}); d->PathStroke(ink,ImDrawFlags_Closed,stroke);
    d->AddLine({c.x-f,c.y+f*1.05f},{c.x+f,c.y+f*1.05f},ink,stroke);
    d->AddLine({c.x-f*.27f,c.y-f*.08f},{c.x+f*.27f,c.y-f*.08f},ink,stroke);
    d->AddLine({c.x,c.y-f*.38f},{c.x,c.y+f*.42f},ink,stroke);
   }
   const char *label=i?"New character":"Graveyard";
   const float text_size=std::min(f,f*std::max(1.f,width-12)/std::max(1.f,ImGui::CalcTextSize(label).x));
   const auto text=ImGui::GetFont()->CalcTextSizeA(text_size,FLT_MAX,0,label);
   d->AddText(ImGui::GetFont(),text_size,{p.x+(width-text.x)*.5f,p.y+height-f*1.5f},ImGui::GetColorU32(ImGuiCol_Text),label);
   ImGui::EndDisabled();
  }
  return action;
 }
 // Continue / Rename / Delete / New / Graveyard
 int draw(const json &saves,bool enabled) {
  const float font=ImGui::GetFontSize();
  int action=0;
  const json *chosen=nullptr;
  for(const auto &s:saves) if(s.value("id","")==selected) chosen=&s;
  if(!chosen && !saves.empty()) { selected=saves.front().value("id",""); chosen=&saves.front(); }
  ImGui::BeginChild("Character selection",ImVec2(0,-ImGui::GetTextLineHeightWithSpacing()*2));
  if(saves.empty()) {
   emblem(font*11,ImGui::GetColorU32(DeluxeTheme::green())); ImGui::Spacing();
   ImGui::TextUnformatted("A new name. A new descent.");
   ImGui::TextDisabled("Your adventurers will be waiting here between journeys.");
   ImGui::Spacing(); action=roster_actions(enabled);
  } else {
   const bool wide=ImGui::GetContentRegionAvail().x>font*52;
   if(ImGui::BeginTable("Roster and profile",wide?2:1,ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Roster",ImGuiTableColumnFlags_WidthStretch,.42f);
    if(wide) ImGui::TableSetupColumn("Profile",ImGuiTableColumnFlags_WidthStretch,.58f);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##Character search","Find a character",search,sizeof(search));
    int matches_count=0;
    for(const auto &s:saves) if(matches(s.value("name",s.value("id",""))+" "+s.value("id","")+" "+s.value("identity",s.value("description","")),search)) ++matches_count;
    const float cards_height=matches_count?matches_count*(font*5.6f+2*ImGui::GetStyle().ItemSpacing.y):ImGui::GetTextLineHeightWithSpacing();
    const float available=wide?ImGui::GetContentRegionAvail().y-font*6-ImGui::GetStyle().ItemSpacing.y:font*14;
    ImGui::BeginChild("Character roster",ImVec2(0,std::max(font*2,std::min(cards_height,available))));
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
    ImGui::EndChild();
    if(int picked=roster_actions(enabled)) action=picked;
    ImGui::TableNextColumn();
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
