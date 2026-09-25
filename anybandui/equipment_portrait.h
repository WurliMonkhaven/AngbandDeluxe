// A read-only body diagram. Slot names and occupied items come from the engine;
// only the humanoid layout is presentation data. Unknown slots remain accessible.
struct EquipmentPortrait {
 struct Anchor { const char *slot; const char *label; bool right; int row; float x,y; };
 // Coordinates refer to the shared ASCII character grid, not the panel width.
 inline static const Anchor anchors[]={
  {"head","Head",false,0,8,1},{"neck","Neck",false,1,9,3},
  {"body","Body",false,2,7,6},{"weapon","Weapon",false,3,3,8},
  {"right hand","Right ring",false,4,3,8.6f},{"feet","Feet",false,5,7,16},
  {"back","Cloak",true,0,14,4},{"shooting","Ranged",true,1,15,5},
  {"arm","Shield",true,2,17,6.5f},{"hands","Hands",true,3,17,8},
  {"left hand","Left ring",true,4,17,8.6f},{"light","Light",true,5,13,9}
 };
 static const json *item_at(const json &items,const std::string &slot) {
  for(const auto &item:items) if(item.value("location","")==slot) return &item;
  return nullptr;
 }
 static void draw(const json &items,const json &slots,const std::string &save) {
  const float f=ImGui::GetFontSize(),width=ImGui::GetContentRegionAvail().x;
  auto *storage=ImGui::GetStateStorage(); const auto selection=ImGui::GetID("Portrait selection");
  int selected=storage->GetInt(selection,-1);
  const auto ink=CharacterSelect::accent(save);
  AnybandUITheme::section("Equipment");
  int occupied=0; for(const auto &slot:slots) if(slot.value("occupied",false)) ++occupied;
  ImGui::TextDisabled("%d / %d slots occupied",occupied,int(slots.size()));
  const bool diagram=width>=f*34;
  const auto origin=ImGui::GetCursorScreenPos();
  const float height=f*28,card_w=width*.29f,card_h=f*4.1f;
  auto *draw=ImGui::GetWindowDrawList();
  // One fixed-width canvas keeps the boots, legs and torso aligned. Measuring
  // each row separately subtly shifted short rows sideways in the old figure.
  const float middle=width-2*(card_w+f);
  const float glyph_ratio=ImGui::CalcTextSize("M").x/f;
  const float font=std::min(f*1.08f,middle/(21*glyph_ratio));
  const float glyph=font*glyph_ratio,line=font*1.07f;
  const ImVec2 figure(origin.x+(width-21*glyph)*.5f,origin.y+(height-17*line)*.5f);
  if(diagram) {
   draw->AddRectFilled(origin,{origin.x+width,origin.y+height},ImGui::GetColorU32(ImGuiCol_ChildBg),5);
   const float left=origin.x+card_w+f,right=origin.x+width-card_w-f;
   for(float y=origin.y+f;y<origin.y+height-f;y+=f) draw->AddLine({left,y},{right,y},IM_COL32(37,62,60,40));
   // Deliberately plain ASCII: all supported fonts can render this figure.
   const char *body[]={"        .---.        ","        |o o|        ","        '---'        ","      ___| |___      ","     /   | |   \\     ","    / /| : : |\\ \\    ","   / / | : : | \\ \\   ","  |_|  |=====|  |_|  ","  [ ]  | : : |  [ ]  ","       /-----\\       ","       | | | |       ","       | | | |       ","       | | | |       ","       | | | |       ","       | | | |       ","     __|_| |_|__     ","    |____| |____|    "};
   for(int i=0;i<17;++i) {
    const ImVec2 at(figure.x,figure.y+i*line);
    draw->AddText(ImGui::GetFont(),font,at,ink,body[i]);
   }
   ImGui::Dummy({width,height});
  }
  const Anchor *trace=nullptr; ImVec2 trace_at; float trace_width=0; bool trace_hover=false;
  for(size_t i=0;i<slots.size();++i) {
   const auto slot=slots[i].value("label",""); const auto *item=item_at(items,slot);
   const Anchor *anchor=nullptr;
   for(const auto &a:anchors) if(slot==a.slot) { anchor=&a; break; }
   ImGui::PushID(int(i));
   const auto after=ImGui::GetCursorScreenPos();
   const bool positioned=diagram && anchor;
   ImVec2 at=after; float w=width;
   if(positioned) {
    at={origin.x+(anchor->right?width-card_w-f*.35f:f*.35f),origin.y+f*.65f+anchor->row*f*4.45f}; w=card_w;
    ImGui::SetCursorScreenPos(at);
   }
   const bool clicked=ImGui::InvisibleButton("Slot",{w,card_h});
   if(clicked) { selected=int(i); storage->SetInt(selection,selected); }
   const bool hover=ImGui::IsItemHovered(),active=selected==int(i);
   // Trace only the inspected/hovered slot. Twelve permanent callouts obscured
   // the figure, especially the ring connection crossing to the opposite hand.
   if(positioned && (hover || (active && !trace_hover))) {
    trace=anchor; trace_at=at; trace_width=w; trace_hover=hover;
   }
   draw->AddRectFilled(at,{at.x+w,at.y+card_h},ImGui::GetColorU32(active?ImGuiCol_HeaderActive:hover?ImGuiCol_HeaderHovered:ImGuiCol_FrameBg),4);
   draw->AddRect(at,{at.x+w,at.y+card_h},active||hover?ink:IM_COL32(43,63,66,255),4);
   if(item) draw->AddRectFilled({at.x,at.y+f*.5f},{at.x+2,at.y+card_h-f*.5f},ink);
   const auto title=anchor?std::string(anchor->label):display_label(slot);
   draw->AddText({at.x+f*.55f,at.y+f*.35f},item?ink:ImGui::GetColorU32(ImGuiCol_TextDisabled),title.c_str());
   std::string name=item?item->value("label",""):"Empty";
   const float available=w-f*1.1f;
   if(ImGui::CalcTextSize(name.c_str(),nullptr,false,available).y>f*2.01f) {
    while(!name.empty() && ImGui::CalcTextSize((name+"...").c_str(),nullptr,false,available).y>f*2.01f) {
     size_t end=name.size()-1;
     while(end>0 && (static_cast<unsigned char>(name[end])&0xc0)==0x80) --end;
     name.resize(end);
    }
    name+="...";
   }
   draw->AddText(ImGui::GetFont(),f,{at.x+f*.55f,at.y+f*1.65f},item?ImGui::GetColorU32(ui_color(item->value("name_color",1))):ImGui::GetColorU32(ImGuiCol_TextDisabled),name.c_str(),nullptr,available);
   if(hover) {
    ImGui::BeginTooltip(); ImGui::PushTextWrapPos(f*30);
    ImGui::TextUnformatted(title.c_str());
    ImGui::TextUnformatted(item?item->value("label","").c_str():"Nothing equipped.");
    if(item && !item->value("inscription","").empty()) ImGui::TextWrapped("%s",item->value("inscription","").c_str());
    ImGui::PopTextWrapPos(); ImGui::EndTooltip();
   }
   if(positioned) ImGui::SetCursorScreenPos(after);
   ImGui::PopID();
  }
  if(trace) {
   const ImVec2 node(figure.x+(trace->x+.5f)*glyph,figure.y+(trace->y+.5f)*line);
   const float edge=trace->right?trace_at.x:trace_at.x+trace_width;
   const float elbow=edge+(trace->right?-1:1)*f*.8f;
   const auto wire=ImGui::ColorConvertU32ToFloat4(ink);
   const auto tint=ImGui::GetColorU32(ImVec4(wire.x,wire.y,wire.z,trace_hover?.85f:.5f));
   draw->AddLine({edge,trace_at.y+card_h*.5f},{elbow,trace_at.y+card_h*.5f},tint);
   draw->AddLine({elbow,trace_at.y+card_h*.5f},{elbow,node.y},tint);
   draw->AddLine({elbow,node.y},node,tint);
   draw->AddCircle(node,std::max(3.f,f*.2f),tint,0,1.5f);
  }
  ImGui::Spacing(); AnybandUITheme::section("Inspection");
  if(selected<0 || selected>=int(slots.size())) ImGui::TextDisabled("Select a slot to inspect your equipment.");
  else {
   const auto slot=slots[selected].value("label",""); const auto *item=item_at(items,slot);
   if(item) {
    ImGui::PushTextWrapPos();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui_color(item->value("name_color",1))),"%s",item->value("label","").c_str());
    ImGui::PopTextWrapPos();
    ItemDescription::draw(*item);
   } else ImGui::TextDisabled("%s: nothing equipped.",display_label(slot).c_str());
  }
 }
};
