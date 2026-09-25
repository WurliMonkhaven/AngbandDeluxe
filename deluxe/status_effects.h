// Wrapping, read-only status badges; severity and descriptions belong to the engine.
struct StatusEffects {
 static std::vector<json> active(const json &player) {
  std::vector<json> effects;
  for(const auto &effect:player.value("statuses",json::array())) {
   if(!effect.value("visible",effect.value("label","")!="FOOD")) continue;
   effects.push_back(effect);
  }
  std::stable_sort(effects.begin(),effects.end(),[](const json &a,const json &b) { return a.value("priority",2)<b.value("priority",2); });
  return effects;
 }
 static std::vector<json> badges(const json &player) {
  auto effects=active(player);
  const int study=player.value("study",0);
  if(study>0) effects.push_back({{"name","Study"},{"kind","study"},{"duration",study},
   {"description","You can learn "+std::to_string(study)+(study==1?" new spell.":" new spells.")+" Open Spells to choose from your books."}});
  return effects;
 }
 static std::string badge_label(const json &effect) {
  const auto name=display_label(effect.value("name",effect.value("label","Effect")));
  return effect.value("kind","")=="study"?name+" \xC2\xB7 "+std::to_string(effect.value("duration",0)):name;
 }
 static float height(const json &player,float width,bool headings=true) {
  const auto effects=badges(player); const auto &style=ImGui::GetStyle();
  const float title=headings?DeluxeTheme::section_height()+style.ItemSpacing.y:0;
  if(effects.empty()) return title+ImGui::GetTextLineHeight();
  width=std::max(1.f,width);
  const float pad=ImGui::GetFontSize()*.4f;
  float used=0,row_height=0,total=0;
  for(const auto &effect:effects) {
   const auto label=badge_label(effect);
   const float badge_width=std::min(width,ImGui::CalcTextSize(label.c_str()).x+3*pad);
   if(used>0 && used+style.ItemSpacing.x+badge_width>width) { total+=row_height+style.ItemSpacing.y; used=0; row_height=0; }
   const float h=ImGui::CalcTextSize(label.c_str(),nullptr,false,std::max(1.f,badge_width-3*pad)).y+pad;
   row_height=std::max(row_height,h); used+=(used>0?style.ItemSpacing.x:0)+badge_width;
  }
  return title+total+row_height;
 }
 static void draw(const json &player,bool *open_spells=nullptr,float study_flash=0) {
  const auto effects=badges(player);
  if(effects.empty()) return;
  ImGui::PushID("Status effects");
  const float left=ImGui::GetCursorScreenPos().x,width=std::max(1.f,ImGui::GetContentRegionAvail().x);
  const float pad=ImGui::GetFontSize()*.4f,gap=ImGui::GetStyle().ItemSpacing.x;
  bool first=true;
  for(const auto &effect:effects) {
   const auto name=display_label(effect.value("name",effect.value("label","Effect")));
   const auto kind=effect.value("kind","neutral");
   const ImVec4 accent=kind=="harm"?ImVec4(1.f,.46f,.38f,1):kind=="mixed"?ImVec4(1.f,.76f,.35f,1):kind=="benefit"?ImVec4(.4f,.88f,.7f,1):ImVec4(.55f,.75f,1.f,1);
   const bool light=DeluxeTheme::light_surface();
   const auto surface=ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
   const auto text_ink=light?ImVec4(accent.x*.42f,accent.y*.42f,accent.z*.42f,1):accent;
   const auto background=light?DeluxeTheme::mix(surface,accent,.16f):ImVec4(accent.x*.16f,accent.y*.16f,accent.z*.16f,1);
   const auto border=light?DeluxeTheme::mix(surface,text_ink,.45f):ImVec4(accent.x,accent.y,accent.z,.5f);
   const auto label=badge_label(effect);
   const float badge_width=std::min(width,ImGui::CalcTextSize(label.c_str()).x+3*pad);
   if(!first && ImGui::GetItemRectMax().x+gap+badge_width<=left+width) ImGui::SameLine();
   const auto a=ImGui::GetCursorScreenPos();
   const float text_width=std::max(1.f,badge_width-3*pad);
   const auto text_size=ImGui::CalcTextSize(label.c_str(),nullptr,false,text_width);
   const ImVec2 size(badge_width,text_size.y+pad);
   const ImVec2 b(a.x+size.x,a.y+size.y);
   auto *draw=ImGui::GetWindowDrawList();
   draw->AddRectFilled(a,b,ImGui::GetColorU32(background),pad*.5f);
   draw->AddRect(a,b,ImGui::GetColorU32(border),pad*.5f);
   draw->AddLine(ImVec2(a.x+pad*.5f,a.y+pad*.45f),ImVec2(a.x+pad*.5f,b.y-pad*.45f),ImGui::GetColorU32(text_ink),2.f);
   if(kind=="study" && study_flash>0) {
    draw->AddRectFilled(a,b,ImGui::GetColorU32(ImVec4(accent.x,accent.y,accent.z,study_flash*.18f)),pad*.5f);
    draw->AddRect(a,b,ImGui::GetColorU32(ImVec4(accent.x,accent.y,accent.z,study_flash)),pad*.5f,0,1.5f);
   }
   draw->PushClipRect(a,b,true);
   draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),ImVec2(a.x+1.5f*pad,a.y+pad*.5f),ImGui::GetColorU32(text_ink),label.c_str(),nullptr,text_width);
   draw->PopClipRect();
   if(kind=="study" && open_spells) {
    if(ImGui::InvisibleButton("Study badge",size)) *open_spells=true;
   } else ImGui::Dummy(size);
   if(ImGui::IsItemHovered()) {
    ImGui::BeginTooltip(); ImGui::PushTextWrapPos(ImGui::GetFontSize()*26);
    ImGui::TextColored(text_ink,"%s (%d)",name.c_str(),effect.value("duration",0));
    ImGui::TextDisabled("%s",kind=="study"?"Learning available":kind=="harm"?"Harmful effect":kind=="mixed"?"Benefits and drawbacks":kind=="benefit"?"Beneficial effect":"Active effect");
    ImGui::Separator(); ImGui::TextWrapped("%s",effect.value("description","Active temporary effect.").c_str());
    ImGui::PopTextWrapPos(); ImGui::EndTooltip();
   }
   first=false;
  }
  ImGui::PopID();
 }
};
