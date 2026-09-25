#pragma once
// One visual vocabulary for native panels. Decorations are bounded geometry,
// not windows or input targets; the normal GPU/CRT pipeline renders them.
struct ThemeSettings {
 bool custom=false, decorations=true, invert_dungeon=false;
 float rounding=5;
 ImVec4 background{.027f,.043f,.052f,1},surface{.065f,.10f,.115f,1},text{.85f,.89f,.86f,1},accent{.50f,.72f,.57f,1};
 static json rgba(ImVec4 v) { return json::array({v.x,v.y,v.z}); }
 json serialize() const { return {{"invert_dungeon",invert_dungeon},{"custom",custom},{"decorations",decorations},{"rounding",rounding},{"background",rgba(background)},{"surface",rgba(surface)},{"text",rgba(text)},{"accent",rgba(accent)}}; }
 void load(const json &j) {
  if(!j.is_object()) return;
  if(j.contains("invert_dungeon") && j["invert_dungeon"].is_boolean()) invert_dungeon=j["invert_dungeon"];
  if(j.contains("custom") && j["custom"].is_boolean()) custom=j["custom"];
  if(j.contains("decorations") && j["decorations"].is_boolean()) decorations=j["decorations"];
  if(j.contains("rounding") && j["rounding"].is_number()) rounding=std::clamp(j["rounding"].get<float>(),0.f,16.f);
  auto read=[&](const char *key,ImVec4 &v) { if(!j.contains(key)) return; const auto &a=j[key];
   if(a.is_array() && a.size()==3 && a[0].is_number() && a[1].is_number() && a[2].is_number())
    v={std::clamp(a[0].get<float>(),0.f,1.f),std::clamp(a[1].get<float>(),0.f,1.f),std::clamp(a[2].get<float>(),0.f,1.f),1}; };
  read("background",background); read("surface",surface); read("text",text); read("accent",accent);
 }
 void preset(int n) {
  *this=ThemeSettings{}; if(n==0) return; custom=true;
  if(n==1) { background={.055f,.06f,.075f,1}; surface={.12f,.13f,.16f,1}; accent={.61f,.68f,.85f,1}; }
  if(n==2) { background={.90f,.91f,.89f,1}; surface={.98f,.98f,.95f,1}; text={.12f,.17f,.18f,1}; accent={.13f,.36f,.32f,1}; rounding=8; }
  if(n==3) { background={.065f,.045f,.025f,1}; surface={.14f,.10f,.055f,1}; text={.94f,.85f,.65f,1}; accent={.91f,.62f,.26f,1}; rounding=0; }
  if(n==4) { background={.025f,.045f,.09f,1}; surface={.055f,.105f,.17f,1}; text={.81f,.9f,.96f,1}; accent={.35f,.76f,.88f,1}; rounding=7; }
 }
};
struct DeluxeTheme {
 inline static ThemeSettings current{};
 static ImU32 dungeon_colour(ImU32 ink) { return current.invert_dungeon?ink ^ IM_COL32(255,255,255,0):ink; }
 static ImVec4 green() { return current.accent; }
 static ImVec4 mix(ImVec4 a,ImVec4 b,float t) { return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,1}; }
 static ImU32 tint(float alpha) { auto a=green(); a.w=alpha; return ImGui::GetColorU32(a); }
 static void configure(const ThemeSettings &theme) {
  current=theme; auto &s=ImGui::GetStyle();
  s.WindowRounding=theme.rounding; s.ChildRounding=theme.rounding*.6f; s.PopupRounding=theme.rounding*.8f;
  s.FrameRounding=s.TabRounding=s.GrabRounding=s.ScrollbarRounding=theme.rounding*.4f;
  if(!theme.custom) return;
  auto *c=s.Colors; const auto bg=theme.background,surface=theme.surface,fg=theme.text,accent=theme.accent;
  c[ImGuiCol_Text]=fg; c[ImGuiCol_TextDisabled]=mix(surface,fg,.62f);
  c[ImGuiCol_WindowBg]=bg; c[ImGuiCol_ChildBg]=mix(bg,surface,.3f); c[ImGuiCol_PopupBg]=surface;
  c[ImGuiCol_Border]=mix(surface,fg,.28f); c[ImGuiCol_Separator]=c[ImGuiCol_Border];
  for(auto id:{ImGuiCol_FrameBg,ImGuiCol_MenuBarBg,ImGuiCol_Tab,ImGuiCol_TabDimmed,ImGuiCol_TableHeaderBg,ImGuiCol_ScrollbarBg}) c[id]=surface;
  for(auto id:{ImGuiCol_Button,ImGuiCol_Header,ImGuiCol_TitleBg,ImGuiCol_TitleBgCollapsed}) c[id]=mix(surface,accent,.17f);
  for(auto id:{ImGuiCol_ButtonHovered,ImGuiCol_HeaderHovered,ImGuiCol_FrameBgHovered,ImGuiCol_TabHovered,ImGuiCol_ScrollbarGrabHovered}) c[id]=mix(surface,accent,.32f);
  for(auto id:{ImGuiCol_ButtonActive,ImGuiCol_HeaderActive,ImGuiCol_FrameBgActive,ImGuiCol_TabSelected,ImGuiCol_TabDimmedSelected,ImGuiCol_TitleBgActive}) c[id]=mix(surface,accent,.24f);
  for(auto id:{ImGuiCol_CheckMark,ImGuiCol_SliderGrab,ImGuiCol_SliderGrabActive,ImGuiCol_SeparatorHovered,ImGuiCol_SeparatorActive,ImGuiCol_TabSelectedOverline,ImGuiCol_TabDimmedSelectedOverline,ImGuiCol_NavCursor,ImGuiCol_ResizeGripHovered,ImGuiCol_ResizeGripActive,ImGuiCol_ScrollbarGrabActive}) c[id]=accent;
  c[ImGuiCol_ScrollbarGrab]=mix(surface,fg,.3f); c[ImGuiCol_ResizeGrip]=mix(surface,accent,.4f);
  c[ImGuiCol_TableBorderStrong]=c[ImGuiCol_Border]; c[ImGuiCol_TableBorderLight]=mix(surface,fg,.15f);
  c[ImGuiCol_TableRowBgAlt]=mix(bg,surface,.65f); c[ImGuiCol_TextSelectedBg]=mix(surface,accent,.35f);
 }
 static void editor(ThemeSettings &theme) {
  if(ImGui::BeginCombo("Preset","Choose a theme...")) {
   const char *names[]={"Terminal (original)","Dark / Graphite","Light / Paper","Amber terminal","Midnight / Ice"};
   for(int i=0;i<5;++i) if(ImGui::Selectable(names[i])) theme.preset(i);
   ImGui::EndCombo();
  }
  bool changed=false;
  changed|=ImGui::ColorEdit3("Background",&theme.background.x);
  changed|=ImGui::ColorEdit3("Surfaces",&theme.surface.x);
  changed|=ImGui::ColorEdit3("Text",&theme.text.x);
  changed|=ImGui::ColorEdit3("Accent",&theme.accent.x);
  theme.custom|=changed;
  ImGui::SliderFloat("Corner rounding",&theme.rounding,0,16,"%.0f px");
  ImGui::Checkbox("Decorative accents",&theme.decorations);
  ImGui::Checkbox("Invert Dungeon Colours",&theme.invert_dungeon);
  if(ImGui::IsItemHovered()) ImGui::SetTooltip("Invert the dungeon background and glyph colours, including the fallback terminal.");
  ImGui::TextDisabled("Preview only until Save and Close.");
  const auto saved=ImGui::GetStyle(); const auto previous=current;
  configure(theme);
  ImGui::BeginChild("Theme preview",{0,ImGui::GetFontSize()*8},ImGuiChildFlags_Borders);
  section("Adventurer"); ImGui::TextUnformatted("A new chapter awaits.");
  ImGui::TextDisabled("Secondary information"); ImGui::Button("Explore"); ImGui::SameLine();
  bool checked=true; ImGui::Checkbox("Ready",&checked);
  ImGui::EndChild(); ImGui::GetStyle()=saved; current=previous;
 }
 static void apply() {
  current=ThemeSettings{};
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
  if(current.decorations) d->AddRectFilledMultiColor(a,ImVec2(a.x+w,a.y+h),tint(.12f),tint(0),tint(0),tint(.05f));
  d->AddLine(ImVec2(a.x,a.y+h),ImVec2(a.x+w,a.y+h),ImGui::GetColorU32(ImGuiCol_Separator));
  d->AddLine(ImVec2(a.x,a.y+f*.3f),ImVec2(a.x,a.y+h-f*.3f),ink,2);
  d->AddText(ImGui::GetFont(),f,ImVec2(a.x+inset,a.y+f*.3f),ink,label,nullptr,wrap_label?wrap:0.f);
  // Short ruled end-stop, kept away from the title even when it wraps.
  if(current.decorations && text.y<=f && text.x+inset+f*3<w)
   for(int i=0;i<3;++i) d->AddLine(ImVec2(a.x+w-f*(.3f+i*.35f),a.y+h*.35f),ImVec2(a.x+w-f*(.3f+i*.35f),a.y+h*.65f),tint(.6f));
  d->PopClipRect(); ImGui::Dummy(ImVec2(w,h));
 }
 static void corners(ImDrawList *d,ImVec2 a,ImVec2 b,ImU32 ink,float length,float rounding=0) {
  if(!current.decorations) return;
  length=std::min(length,std::min(b.x-a.x,b.y-a.y)*.25f);
  for(int x=0;x<2;++x) for(int y=0;y<2;++y) {
   const ImVec2 p(x?b.x:a.x,y?b.y:a.y);
   const float sx=x?-1.f:1.f,sy=y?-1.f:1.f;
   const float r=std::min(rounding,length);
   if(r>0) {
    const ImVec2 center(p.x+sx*r,p.y+sy*r);
    d->PathLineTo({p.x+sx*length,p.y});
    for(int i=0;i<=6;++i) {
     const float angle=i*(3.14159265f*.5f/6);
     d->PathLineTo({center.x-sx*r*std::sin(angle),center.y-sy*r*std::cos(angle)});
    }
    d->PathLineTo({p.x,p.y+sy*length}); d->PathStroke(ink,0,1);
   } else {
    d->AddLine(p,ImVec2(p.x+sx*length,p.y),ink);
    d->AddLine(p,ImVec2(p.x,p.y+sy*length),ink);
   }
  }
 }
 static void panel() {
  if(!current.decorations) return;
  auto *d=ImGui::GetWindowDrawList(); const auto a=ImGui::GetWindowPos(),size=ImGui::GetWindowSize();
  const ImVec2 b(a.x+size.x,a.y+size.y);
  d->AddRectFilledMultiColor(a,ImVec2(b.x,std::min(b.y,a.y+ImGui::GetFontSize()*7)),tint(.06f),tint(0),tint(0),tint(0));
  corners(d,ImVec2(a.x+1,a.y+1),ImVec2(b.x-1,b.y-1),tint(.35f),ImGui::GetFontSize()*.7f);
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
