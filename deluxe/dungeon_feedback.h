// Non-interactive overlays; the engine retains all input and travel decisions.
struct DungeonFeedback {
 static void route(const Connection &c,ImDrawList *draw,ImVec2 origin,ImVec2 size,float cw,float ch,int ox,int oy,bool hovered,int x,int y) {
  draw->PushClipRect(origin,ImVec2(origin.x+size.x,origin.y+size.y),true);
  const auto point=[&](int wx,int wy) { return ImVec2(origin.x+(wx-ox+.5f)*cw,origin.y+(wy-oy+.5f)*ch); };
  if(hovered && c.route.value("context","")==c.state.value("context","") && c.route.value("x",-1)==x && c.route.value("y",-1)==y && c.route.contains("path")) {
   bool has_previous=c.state.contains("player");
   ImVec2 previous=has_previous?point(c.state["player"].value("x",0),c.state["player"].value("y",0)):ImVec2();
   for(const auto &p:c.route["path"]) {
    const auto at=point(p[0],p[1]);
    if(has_previous) draw->AddLine(previous,at,IM_COL32(240,185,85,210),std::max(1.5f,std::min(cw,ch)*.12f));
    previous=at; has_previous=true;
   }
  }
  draw->PopClipRect();
 }
 static void ribbon(const Connection &c,ImDrawList *draw,ImVec2 start,ImVec2 area,bool click_to_continue) {
  if(!c.state.value("message_pending",false)) return;
  const float font=ImGui::GetFontSize(),pad=font*.65f;
  const float width=std::min(area.x-2*pad,font*33.f);
  if(width<font*8) return;
  const float h=font*5.2f;
  const ImVec2 a(start.x+(area.x-width)/2,start.y+area.y-h-pad),b(a.x+width,a.y+h);
  const float pulse=.65f+.35f*float(std::sin(ImGui::GetTime()*3));
  const auto amber=IM_COL32(245,188,90,255);
  draw->AddRect(ImVec2(start.x+2,start.y+2),ImVec2(start.x+area.x-2,start.y+area.y-2),IM_COL32(230,160,55,int(65+65*pulse)),3,0,2);
  // Geometry-only hover detection: no ImGui item/window may intercept game clicks.
  const bool hovered=ImGui::IsMouseHoveringRect(a,b);
  auto *storage=ImGui::GetStateStorage();
  const auto fade_id=ImGui::GetID("Dungeon ribbon opacity");
  const float target=hovered?.06f:1.f;
  const float opacity=target+(storage->GetFloat(fade_id,target)-target)*std::exp(-ImGui::GetIO().DeltaTime*22.f);
  storage->SetFloat(fade_id,opacity);
  const int first_vertex=draw->VtxBuffer.Size;
  draw->AddRectFilled(a,b,IM_COL32(8,12,18,250),6);
  draw->AddRect(a,b,IM_COL32(225,165,70,int(130+90*pulse)),6,0,1.5f);
  const ImVec2 text(a.x+pad,a.y+pad);
  draw->PushClipRect(ImVec2(a.x+pad,a.y+pad),ImVec2(b.x-pad,b.y-pad),true);
  // A small pause emblem and breathing dots remain legible under CRT bloom.
  for(int i=0;i<2;++i) draw->AddRectFilled(ImVec2(text.x+i*font*.3f,text.y+font*.15f),ImVec2(text.x+i*font*.3f+font*.13f,text.y+font*.85f),amber);
  draw->AddText(ImVec2(text.x+font,text.y),amber,"Messages waiting");
  for(int i=0;i<3;++i) draw->AddCircleFilled(ImVec2(b.x-pad-font*(1.1f-.35f*i),text.y+font*.5f),font*.08f,IM_COL32(245,188,90,int(90+120*(.5+.5*std::sin(ImGui::GetTime()*3-i)))));
  if(!c.previous_messages.empty()) {
   const auto excerpt=c.previous_messages.front().value("text","");
   const ImVec4 clip(text.x,text.y+font*1.35f,b.x-pad,text.y+font*3.35f);
   draw->AddText(ImGui::GetFont(),font,ImVec2(text.x,text.y+font*1.35f),IM_COL32(225,220,210,255),excerpt.c_str(),nullptr,width-2*pad,&clip);
  }
  draw->AddText(ImVec2(text.x,b.y-pad-font),IM_COL32(185,180,165,255),click_to_continue?"Click to continue":"Continue with your usual key");
  draw->PopClipRect();
  // Fade the background, text and accents together, leaving the viewport cue intact.
  for(int i=first_vertex;i<draw->VtxBuffer.Size;++i) {
   auto &color=draw->VtxBuffer[i].col;
   const auto alpha=ImU32(float((color>>IM_COL32_A_SHIFT)&255)*opacity);
   color=(color&~IM_COL32_A_MASK)|(alpha<<IM_COL32_A_SHIFT);
  }
 }
};
