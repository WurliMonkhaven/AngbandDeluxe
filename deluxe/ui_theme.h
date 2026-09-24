#pragma once
// One visual vocabulary for native panels. Decorations are bounded geometry,
// not windows or input targets; the normal GPU/CRT pipeline renders them.
struct DeluxeTheme {
 static ImVec4 green() { return ImVec4(.50f,.72f,.57f,1); }
 static void apply() {
  ImGui::StyleColorsDark();
  auto &s=ImGui::GetStyle();
  s.WindowPadding=ImVec2(10,10); s.FramePadding=ImVec2(7,4);
  s.ItemSpacing=ImVec2(7,5); s.ItemInnerSpacing=ImVec2(5,4);
  s.CellPadding=ImVec2(6,4);
  s.WindowRounding=5; s.ChildRounding=3; s.FrameRounding=2;
  s.PopupRounding=4; s.TabRounding=2; s.GrabRounding=2;
  s.ScrollbarRounding=2; s.ScrollbarSize=12;
  s.WindowBorderSize=1; s.ChildBorderSize=1; s.FrameBorderSize=1;
  s.TabBorderSize=0; s.SeparatorTextBorderSize=1;
  s.SeparatorTextPadding=ImVec2(8,5);
  auto *c=s.Colors;
  c[ImGuiCol_Text]=ImVec4(.85f,.89f,.86f,1);
  c[ImGuiCol_TextDisabled]=ImVec4(.47f,.56f,.55f,1);
  c[ImGuiCol_WindowBg]=ImVec4(.027f,.043f,.052f,1);
  c[ImGuiCol_ChildBg]=ImVec4(.035f,.053f,.063f,1);
  c[ImGuiCol_PopupBg]=ImVec4(.055f,.079f,.09f,.99f);
  c[ImGuiCol_Border]=ImVec4(.18f,.27f,.27f,.8f);
  c[ImGuiCol_BorderShadow]=ImVec4(0,0,0,0);
  c[ImGuiCol_FrameBg]=ImVec4(.065f,.10f,.115f,1);
  c[ImGuiCol_FrameBgHovered]=ImVec4(.10f,.18f,.18f,1);
  c[ImGuiCol_FrameBgActive]=ImVec4(.13f,.23f,.21f,1);
  c[ImGuiCol_TitleBg]=ImVec4(.055f,.10f,.105f,1);
  c[ImGuiCol_TitleBgActive]=ImVec4(.095f,.19f,.16f,1);
  c[ImGuiCol_TitleBgCollapsed]=c[ImGuiCol_TitleBg];
  c[ImGuiCol_MenuBarBg]=c[ImGuiCol_FrameBg];
  c[ImGuiCol_CheckMark]=green(); c[ImGuiCol_SliderGrab]=green();
  c[ImGuiCol_SliderGrabActive]=ImVec4(.69f,.9f,.72f,1);
  c[ImGuiCol_Button]=ImVec4(.105f,.20f,.18f,1);
  c[ImGuiCol_ButtonHovered]=ImVec4(.17f,.32f,.25f,1);
  c[ImGuiCol_ButtonActive]=ImVec4(.24f,.43f,.32f,1);
  c[ImGuiCol_Header]=ImVec4(.105f,.19f,.18f,1);
  c[ImGuiCol_HeaderHovered]=ImVec4(.15f,.28f,.23f,1);
  c[ImGuiCol_HeaderActive]=ImVec4(.20f,.35f,.28f,1);
  c[ImGuiCol_Tab]=ImVec4(.06f,.10f,.115f,1);
  c[ImGuiCol_TabHovered]=ImVec4(.16f,.28f,.23f,1);
  c[ImGuiCol_TabSelected]=ImVec4(.13f,.24f,.195f,1);
  c[ImGuiCol_TabSelectedOverline]=green();
  c[ImGuiCol_TabDimmed]=c[ImGuiCol_Tab];
  c[ImGuiCol_TabDimmedSelected]=c[ImGuiCol_TabSelected];
  c[ImGuiCol_TabDimmedSelectedOverline]=green();
  c[ImGuiCol_Separator]=ImVec4(.17f,.27f,.265f,1);
  c[ImGuiCol_SeparatorHovered]=green(); c[ImGuiCol_SeparatorActive]=green();
  c[ImGuiCol_ResizeGrip]=ImVec4(.30f,.46f,.38f,.35f);
  c[ImGuiCol_ResizeGripHovered]=green(); c[ImGuiCol_ResizeGripActive]=green();
  c[ImGuiCol_ScrollbarBg]=ImVec4(.025f,.04f,.05f,.5f);
  c[ImGuiCol_ScrollbarGrab]=ImVec4(.20f,.31f,.29f,1);
  c[ImGuiCol_ScrollbarGrabHovered]=ImVec4(.30f,.45f,.37f,1);
  c[ImGuiCol_ScrollbarGrabActive]=green();
  c[ImGuiCol_TableHeaderBg]=ImVec4(.075f,.125f,.13f,1);
  c[ImGuiCol_TableBorderStrong]=ImVec4(.18f,.27f,.27f,1);
  c[ImGuiCol_TableBorderLight]=ImVec4(.13f,.20f,.21f,.6f);
  c[ImGuiCol_TableRowBg]=ImVec4(0,0,0,0);
  c[ImGuiCol_TableRowBgAlt]=ImVec4(.25f,.42f,.37f,.065f);
  c[ImGuiCol_TextSelectedBg]=ImVec4(.26f,.49f,.38f,.6f);
  c[ImGuiCol_NavCursor]=green();
  c[ImGuiCol_ModalWindowDimBg]=ImVec4(.008f,.016f,.02f,.78f);
 }
 static void section(const char *label,bool wrap_label=true) {
  const float f=ImGui::GetFontSize(),w=std::max(1.f,ImGui::GetContentRegionAvail().x);
  const auto a=ImGui::GetCursorScreenPos();
  const float inset=f*.9f,wrap=std::max(1.f,w-inset-f*.4f);
  const auto text=ImGui::CalcTextSize(label,nullptr,false,wrap_label?wrap:0.f);
  const float h=text.y+f*.6f;
  auto *d=ImGui::GetWindowDrawList(); const auto ink=ImGui::GetColorU32(green());
  d->PushClipRect(a,ImVec2(a.x+w,a.y+h),true);
  d->AddRectFilledMultiColor(a,ImVec2(a.x+w,a.y+h),IM_COL32(28,48,43,180),IM_COL32(13,24,28,20),IM_COL32(13,24,28,20),IM_COL32(20,34,33,120));
  d->AddLine(ImVec2(a.x,a.y+h),ImVec2(a.x+w,a.y+h),ImGui::GetColorU32(ImGuiCol_Separator));
  d->AddLine(ImVec2(a.x,a.y+f*.3f),ImVec2(a.x,a.y+h-f*.3f),ink,2);
  d->AddText(ImGui::GetFont(),f,ImVec2(a.x+inset,a.y+f*.3f),ink,label,nullptr,wrap_label?wrap:0.f);
  // Short ruled end-stop, kept away from the title even when it wraps.
  if(text.y<=f && text.x+inset+f*3<w)
   for(int i=0;i<3;++i) d->AddLine(ImVec2(a.x+w-f*(.3f+i*.35f),a.y+h*.35f),ImVec2(a.x+w-f*(.3f+i*.35f),a.y+h*.65f),IM_COL32(72,102,87,180));
  d->PopClipRect(); ImGui::Dummy(ImVec2(w,h));
 }
 static void corners(ImDrawList *d,ImVec2 a,ImVec2 b,ImU32 ink,float length) {
  length=std::min(length,std::min(b.x-a.x,b.y-a.y)*.25f);
  for(int x=0;x<2;++x) for(int y=0;y<2;++y) {
   const ImVec2 p(x?b.x:a.x,y?b.y:a.y);
   d->AddLine(p,ImVec2(p.x+(x?-length:length),p.y),ink);
   d->AddLine(p,ImVec2(p.x,p.y+(y?-length:length)),ink);
  }
 }
 static void panel() {
  auto *d=ImGui::GetWindowDrawList(); const auto a=ImGui::GetWindowPos(),size=ImGui::GetWindowSize();
  const ImVec2 b(a.x+size.x,a.y+size.y);
  d->AddRectFilledMultiColor(a,ImVec2(b.x,std::min(b.y,a.y+ImGui::GetFontSize()*7)),IM_COL32(26,44,43,85),IM_COL32(12,22,29,0),IM_COL32(12,22,29,0),IM_COL32(12,22,29,0));
  corners(d,ImVec2(a.x+1,a.y+1),ImVec2(b.x-1,b.y-1),IM_COL32(51,79,69,170),ImGui::GetFontSize()*.7f);
 }
 // The hit target spans the row, but the name belongs to this column only.
 static bool table_choice(const char *label,bool selected,ImGuiSelectableFlags flags=ImGuiSelectableFlags_SpanAllColumns,const char *badge="",float row_height=0) {
  const auto a=ImGui::GetCursorScreenPos(); const float width=ImGui::GetContentRegionAvail().x;
  const std::string id=std::string("##row-")+label;
  const float height=std::max(ImGui::GetTextLineHeight(),row_height);
  const ImVec2 text_pos(a.x,a.y+(height-ImGui::GetTextLineHeight())*.5f);
  const bool activated=ImGui::Selectable(id.c_str(),selected,flags,ImVec2(0,height));
  auto *d=ImGui::GetWindowDrawList();
  const float f=ImGui::GetFontSize(),badge_font=f*.75f;
  const float badge_width=*badge?ImGui::GetFont()->CalcTextSizeA(badge_font,FLT_MAX,0,badge).x+f*.5f:0;
  d->PushClipRect(a,ImVec2(a.x+std::max(1.f,width-badge_width),a.y+height),true);
  d->AddText(text_pos,ImGui::GetColorU32(ImGuiCol_Text),label); d->PopClipRect();
  if(*badge) {
   d->PushClipRect(a,ImVec2(a.x+std::max(1.f,width),a.y+height),true);
   d->AddText(ImGui::GetFont(),badge_font,ImVec2(a.x+std::max(0.f,width-badge_width)+f*.3f,text_pos.y+f*.15f),IM_COL32(156,211,162,255),badge);
   d->PopClipRect();
  }
  return activated;
 }
};
