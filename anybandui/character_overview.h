#include "status_effects.h"
// Compact character overview. All values and XP thresholds come from the engine.
struct CharacterOverview {
 static void meter(const char *label,const std::string &value,float fraction,ImVec4 fill,const std::string &badge="",float flash=0,float sweep=0) {
  const bool light=AnybandUITheme::light_surface();
  const auto surface=ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
  const auto empty=light?AnybandUITheme::mix(surface,fill,.07f):ImVec4(fill.x*.22f,fill.y*.22f,fill.z*.22f,1);
  ImGui::PushStyleColor(ImGuiCol_PlotHistogram,light?AnybandUITheme::mix(surface,fill,.35f):fill);
  ImGui::PushStyleColor(ImGuiCol_FrameBg,empty);
  ImGui::ProgressBar(fraction,ImVec2(-1,ImGui::GetFrameHeight()),"");
  const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
  const float pad=ImGui::GetStyle().FramePadding.x;
  const float y=a.y+(b.y-a.y-ImGui::GetFontSize())*.5f;
  auto *draw=ImGui::GetWindowDrawList(); const auto ink=light?ImGui::GetColorU32(ImGuiCol_Text):IM_COL32(235,240,236,255);
  // A lit glass face, a thin bright edge, and fine scale marks below the text.
  const float badge_width=badge.empty()?0.f:std::min(ImGui::CalcTextSize(badge.c_str()).x+2*pad,(b.x-a.x)*.55f);
  const ImVec2 track(a.x+badge_width,a.y);
  // Labels share one continuous fill; the level is not a separate segment.
  const float edge=a.x+(b.x-a.x)*std::clamp(fraction,0.f,1.f);
  if(edge>a.x) {
   const auto top=ImGui::GetColorU32(light?AnybandUITheme::mix(surface,fill,.26f):ImVec4(fill.x*1.15f,fill.y*1.15f,fill.z*1.15f,1));
   const auto bottom=ImGui::GetColorU32(light?AnybandUITheme::mix(surface,fill,.38f):ImVec4(fill.x*.55f,fill.y*.55f,fill.z*.55f,1));
   draw->AddRectFilledMultiColor(a,ImVec2(edge,b.y),top,top,bottom,bottom);
   draw->AddLine(ImVec2(a.x+1,a.y+1),ImVec2(edge,a.y+1),ImGui::GetColorU32(light?AnybandUITheme::mix(surface,fill,.5f):ImVec4(fill.x+.18f,fill.y+.18f,fill.z+.18f,.8f)));
  }
  for(int i=1;i<10;++i) {
   const float x=a.x+(b.x-a.x)*i/10.f;
   draw->AddLine(ImVec2(x,b.y-3),ImVec2(x,b.y-1),light?ImGui::GetColorU32(AnybandUITheme::mix(surface,fill,.35f)):IM_COL32(180,210,200,65));
  }
  draw->AddRect(a,b,ImGui::GetColorU32(ImGuiCol_Border),2);
  if(flash>0) {
   draw->PushClipRect(a,b,true);
   draw->AddRectFilled(a,b,ImGui::GetColorU32(ImVec4(1.f,.61f,.16f,flash*.18f)));
   const float span=(b.x-a.x)*.28f,beam=a.x-span+(b.x-a.x+2*span)*std::min(1.f,sweep*1.7f);
   const auto clear=IM_COL32(255,204,105,0),light=ImGui::GetColorU32(ImVec4(1.f,.8f,.42f,flash*.45f));
   draw->AddRectFilledMultiColor(ImVec2(beam-span,a.y),ImVec2(beam,b.y),clear,light,light,clear);
   draw->AddRectFilledMultiColor(ImVec2(beam,a.y),ImVec2(beam+span,b.y),light,clear,clear,light);
   draw->PopClipRect();
   draw->AddRect(a,b,ImGui::GetColorU32(ImVec4(1.f,.8f,.42f,flash)),2,0,1.5f);
  }
  const float value_width=ImGui::CalcTextSize(value.c_str()).x;
  if(!badge.empty()) {
   const ImVec2 end(track.x,b.y);
   draw->PushClipRect(a,end,true);
   draw->AddText(ImVec2(a.x+pad,y),ink,badge.c_str());
   draw->PopClipRect();
  }
  draw->PushClipRect(track,b,true);
  if(ImGui::CalcTextSize(label).x+value_width+3*pad<b.x-track.x) draw->AddText(ImVec2(track.x+pad,y),ink,label);
  draw->AddText(ImVec2(std::max(track.x+pad,b.x-pad-value_width),y),ink,value.c_str());
  draw->PopClipRect(); ImGui::PopStyleColor(2);
 }
 static void metric(const char *label,const std::string &value,const std::string &tooltip="") {
  // Keep each label/value pair together inside its own quiet, bounded tile.
  const auto a=ImGui::GetCursorScreenPos();
  const float width=ImGui::GetContentRegionAvail().x,pad=ImGui::GetFontSize()*.4f;
  const float label_width=ImGui::CalcTextSize(label).x,value_width=ImGui::CalcTextSize(value.c_str()).x;
  const float gap=*label?ImGui::GetFontSize()*.6f:0.f;
  const bool stacked=*label && label_width+gap+value_width+2*pad>width;
  const float vertical_pad=ImGui::GetFontSize()*.2f;
  const float height=ImGui::GetFontSize()*(stacked?2.f:1.f)+2*vertical_pad;
  const ImVec2 b(a.x+width,a.y+height);
  auto *draw=ImGui::GetWindowDrawList();
  draw->AddRectFilledMultiColor(a,b,ImGui::GetColorU32(ImGuiCol_FrameBg),ImGui::GetColorU32(ImGuiCol_ChildBg),ImGui::GetColorU32(ImGuiCol_ChildBg),ImGui::GetColorU32(ImGuiCol_FrameBg));
  draw->AddRect(a,b,ImGui::GetColorU32(ImGuiCol_Border),2);
  AnybandUITheme::corners(draw,a,b,AnybandUITheme::tint(.5f),pad);
  draw->PushClipRect(a,b,true);
  draw->AddText(ImVec2(a.x+pad,a.y+vertical_pad),ImGui::GetColorU32(ImGuiCol_TextDisabled),label);
  draw->AddText(ImVec2(a.x+pad+(stacked?0:label_width+gap),a.y+vertical_pad+(stacked?ImGui::GetFontSize():0)),ImGui::GetColorU32(ImGuiCol_Text),value.c_str());
  draw->PopClipRect(); ImGui::Dummy(ImVec2(width,height));
  if(ImGui::IsItemHovered()) {
   ImGui::BeginTooltip(); ImGui::PushTextWrapPos(ImGui::GetFontSize()*28.f);
   const auto text=tooltip.empty()?(*label?std::string(label)+": "+value:value):tooltip;
   ImGui::TextUnformatted(text.c_str()); ImGui::PopTextWrapPos(); ImGui::EndTooltip();
  }
 }
 static void section(const char *label) {
  ImGui::Dummy(ImVec2(0,ImGui::GetFontSize()*.55f));
  AnybandUITheme::section(label);
  ImGui::Dummy(ImVec2(0,ImGui::GetFontSize()*.1f));
 }
 static float height(const json &p,float width,bool headings=true) {
  const auto &style=ImGui::GetStyle(); const float font=ImGui::GetFontSize();
  const float row_padding=2*style.CellPadding.y;
  float total=std::max(ImGui::GetFrameHeight(),headings?AnybandUITheme::section_height():ImGui::GetFrameHeight())+row_padding;
  total+=style.ItemSpacing.y+2*(ImGui::GetFrameHeight()+row_padding);
  if(p.contains("stats") && !p["stats"].empty()) total+=style.ItemSpacing.y+ImGui::GetFrameHeight()+row_padding;
  const char *labels[]={"Gold","Armour","Speed"};
  const int speed=p.value("speed",0);
  const std::string values[]={std::to_string(p.value("gold",0)),std::to_string(p.value("armour",0)),(speed>=0?"+":"")+std::to_string(speed)};
  const float cell=width/3-2*style.CellPadding.x; float metric_height=0;
  for(int i=0;i<3;++i) {
   const bool stacked=ImGui::CalcTextSize(labels[i]).x+ImGui::CalcTextSize(values[i].c_str()).x+font*1.4f>cell;
   metric_height=std::max(metric_height,font*(stacked?2.4f:1.4f));
  }
  total+=style.ItemSpacing.y+metric_height+row_padding;
  if(p.value("extra_moves",0)) total+=ImGui::GetTextLineHeightWithSpacing();
  return total;
 }
 static bool draw(const json &p,bool can_open,const LevelFeedback *level_up=nullptr,double now=0,bool headings=true) {
  bool open=false;
  std::string identity=p.value("name","");
  if(!identity.empty()) identity+=" Â· ";
  identity+=p.value("race","")+" "+p.value("class","");
  const auto title=p.value("title","");
  if(!title.empty()) identity+=" Â· "+display_label(title);
  if(ImGui::BeginTable("Character identity",2,ImGuiTableFlags_SizingStretchProp)) {
   ImGui::TableSetupColumn("Identity",ImGuiTableColumnFlags_WidthStretch);
   ImGui::TableSetupColumn("Details",ImGuiTableColumnFlags_WidthFixed,ImGui::CalcTextSize("Details").x+2*ImGui::GetStyle().FramePadding.x);
   ImGui::TableNextRow(); ImGui::TableNextColumn();
   const bool compact=ImGui::CalcTextSize(identity.c_str()).x+ImGui::GetFontSize()*1.3f>ImGui::GetContentRegionAvail().x;
   auto name=p.value("name",""); if(name.empty()) name="Adventurer";
   if(headings) AnybandUITheme::section(compact?name.c_str():identity.c_str(),false);
   else { ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(compact?name.c_str():identity.c_str()); }
   if(ImGui::IsItemHovered()) {
    ImGui::BeginTooltip(); ImGui::PushTextWrapPos(ImGui::GetFontSize()*28.f);
    ImGui::TextUnformatted(name.c_str()); ImGui::Separator();
    ImGui::Text("Race: %s",p.value("race","").c_str());
    ImGui::Text("Class: %s",p.value("class","").c_str());
    if(!title.empty()) ImGui::Text("Title: %s",display_label(title).c_str());
    ImGui::PopTextWrapPos(); ImGui::EndTooltip();
   }
   ImGui::TableNextColumn(); ImGui::BeginDisabled(!can_open); open=ImGui::Button("Details"); ImGui::EndDisabled();
   if(ImGui::IsItemHovered()) ImGui::SetTooltip("Open character details");
   ImGui::EndTable();
  }
  if(ImGui::BeginTable("Resources",2,ImGuiTableFlags_SizingStretchSame)) {
   auto resource=[&](const char *label,const char *current,const char *maximum,ImVec4 fill) {
    const int v=p.value(current,0),m=p.value(maximum,0);
    const auto value=std::to_string(std::max(0,v))+" / "+std::to_string(m);
    meter(label,value,resource_fraction(v,m),fill);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s: %s",label,value.c_str());
   };
   ImGui::TableNextRow(); ImGui::TableNextColumn(); resource("HP","hp","max_hp",ImVec4(.66f,.15f,.19f,1));
   ImGui::TableNextColumn(); resource("SP","sp","max_sp",ImVec4(.14f,.31f,.62f,1));
   ImGui::TableNextRow(); ImGui::TableNextColumn();
   const int food=p.value("food",0),food_max=p.value("food_max",0);
   char food_text[64]; SDL_snprintf(food_text,sizeof(food_text),"%.1f%% (%d)",food_max>0?100.f*food/food_max:0.f,food);
   meter("Food",food_text,resource_fraction(food,food_max),ImVec4(.14f,.42f,.23f,1));
   if(ImGui::IsItemHovered()) ImGui::SetTooltip("Food: %s",food_text);
   ImGui::TableNextColumn();
   const int xp=p.value("experience",0),base=p.value("level_start_experience",0),next=p.value("next_level_experience",0);
   const float progress=next>0?resource_fraction(xp-base,next-base):1.f;
   char xp_text[64]; SDL_snprintf(xp_text,sizeof(xp_text),next>0?"%.0f%%":"MAX",progress*100);
   const float flash=level_up?level_up->intensity(now):0;
   const bool notice=flash>0 && level_up->progress(now)<.75f;
   meter(notice?"LEVEL UP":"",notice?"Level "+std::to_string(level_up->level):xp_text,progress,ImVec4(.56f,.37f,.12f,1),notice?"":"Lv "+std::to_string(p.value("level",1)),flash,level_up?level_up->progress(now):0);
   if(ImGui::IsItemHovered()) {
    if(next>0) ImGui::SetTooltip("Level %d\nExperience: %d\nNext level: %d\nRemaining: %d",p.value("level",1),xp,next,std::max(0,next-xp));
    else ImGui::SetTooltip("Level %d\nExperience: %d\nMaximum level reached",p.value("level",1),xp);
   }
   ImGui::EndTable();
  }
  static const char *names[]={"STR","INT","WIS","DEX","CON"};
  if(p.contains("stats") && !p["stats"].empty()) {
   // Equal cells, with inline label/value pairs and one shared text scale.
   // Wide exceptional stats must not collide with the neighbouring cell.
   std::vector<std::string> values;
   float longest=0;
   for(size_t i=0;i<p["stats"].size();++i) {
    const int v=p["stats"][i]; char value[32];
    if(v>18) SDL_snprintf(value,sizeof(value),"18/%02d",v-18); else SDL_snprintf(value,sizeof(value),"%d",v);
    values.emplace_back(value);
    longest=std::max(longest,ImGui::CalcTextSize(i<std::size(names)?names[i]:"Stat").x+ImGui::CalcTextSize(value).x);
   }
   const float font=ImGui::GetFontSize(),gap=font*.45f;
   const float cell=ImGui::GetContentRegionAvail().x/values.size()-2*ImGui::GetStyle().CellPadding.x;
   const float scale=std::min(1.f,std::max(1.f,cell-2)/(longest+gap));
   if(ImGui::BeginTable("Attributes",int(values.size()),ImGuiTableFlags_SizingStretchSame|ImGuiTableFlags_BordersInnerV)) {
    ImGui::TableNextRow();
    for(size_t i=0;i<values.size();++i) {
     ImGui::TableNextColumn();
     ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,ImGui::GetColorU32(ImGuiCol_FrameBg));
     const char *label=i<std::size(names)?names[i]:"Stat";
     const float width=ImGui::GetContentRegionAvail().x,height=ImGui::GetFrameHeight();
     const float label_width=ImGui::CalcTextSize(label).x*scale;
     const float pair_width=label_width+(gap+ImGui::CalcTextSize(values[i].c_str()).x)*scale;
     const auto a=ImGui::GetCursorScreenPos();
     const ImVec2 text(a.x+std::max(0.f,(width-pair_width)*.5f),a.y+(height-font*scale)*.5f);
     auto *draw=ImGui::GetWindowDrawList();
     draw->PushClipRect(a,ImVec2(a.x+width,a.y+height),true);
     draw->AddText(ImGui::GetFont(),font*scale,text,ImGui::GetColorU32(ImGuiCol_TextDisabled),label);
     draw->AddText(ImGui::GetFont(),font*scale,ImVec2(text.x+label_width+gap*scale,text.y),ImGui::GetColorU32(ImGuiCol_Text),values[i].c_str());
     draw->PopClipRect();
     ImGui::Dummy(ImVec2(width,height));
     if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s: %s",label,values[i].c_str());
    }
    ImGui::EndTable();
   }
  }
  if(ImGui::BeginTable("Combat overview",3,ImGuiTableFlags_SizingStretchSame)) {
   ImGui::TableNextRow(); ImGui::TableNextColumn(); metric("Gold",std::to_string(p.value("gold",0)));
   ImGui::TableNextColumn(); metric("Armour",std::to_string(p.value("armour",0)));
   ImGui::TableNextColumn(); const int speed=p.value("speed",0); metric("Speed",(speed>=0?"+":"")+std::to_string(speed));
   ImGui::EndTable();
  }
  if(p.value("extra_moves",0)) ImGui::Text("Extra moves: %+d",p.value("extra_moves",0));
  return open;
 }
 static float tracked_height(bool headings=true) {
  return (headings?AnybandUITheme::section_height()+ImGui::GetStyle().ItemSpacing.y:0)+ImGui::GetTextLineHeightWithSpacing()+ImGui::GetFrameHeight();
 }
 static void tracked(const json &p,bool headings=true) {
  if(headings) AnybandUITheme::section("Tracked creature",false);
  if(p.contains("tracked_creature")) {
   const auto &m=p["tracked_creature"];
   if(m.value("visible",false)) {
    const auto name=display_label(m.value("name",""));
    ImGui::TextUnformatted(name.c_str());
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",name.c_str());
    meter("HP",std::to_string(std::max(0,m.value("hp",0)))+" / "+std::to_string(m.value("max_hp",0)),
     resource_fraction(m.value("hp",0),m.value("max_hp",0)),ImVec4(.56f,.37f,.12f,1));
    return;
   }
   ImGui::TextDisabled("Out of sight");
  } else ImGui::TextDisabled("No creature tracked");
  ImGui::BeginDisabled();
  ImGui::ProgressBar(0,ImVec2(-1,ImGui::GetFrameHeight()),"HP  -- / --");
  ImGui::EndDisabled();
 }
 static int dungeon_columns(const json &p,float width) {
  const char *labels[]={"Depth","Light","Feel",""};
  const std::string values[]={std::to_string(p.value("depth",0)),std::to_string(p.value("light",0)),p.value("feeling","—"),display_label(p.value("floor",""))};
  float tile_width=0;
  for(int i=0;i<4;++i) tile_width=std::max(tile_width,ImGui::CalcTextSize(labels[i]).x+ImGui::CalcTextSize(values[i].c_str()).x+ImGui::GetFontSize()*(*labels[i]?1.4f:.8f)+2*ImGui::GetStyle().CellPadding.x+2);
  return width>=4*tile_width?4:2;
 }
 static float dungeon_height(const json &p,float width,bool headings=true) {
  const int columns=dungeon_columns(p,width);
  const char *labels[]={"Depth","Light","Feel",""};
  const std::string values[]={std::to_string(p.value("depth",0)),std::to_string(p.value("light",0)),p.value("feeling","—"),display_label(p.value("floor",""))};
  const auto &style=ImGui::GetStyle();
  const float cell=width/columns-2*style.CellPadding.x;
  float height=headings?AnybandUITheme::section_height()+style.ItemSpacing.y:0;
  for(int row=0;row<4;row+=columns) {
   float line=0;
   for(int i=row;i<row+columns;++i) {
    const bool stacked=*labels[i] && ImGui::CalcTextSize(labels[i]).x+ImGui::CalcTextSize(values[i].c_str()).x+ImGui::GetFontSize()*1.4f>cell;
    line=std::max(line,ImGui::GetFontSize()*(stacked?2.4f:1.4f));
   }
   height+=line+2*style.CellPadding.y;
  }
  for(const char *key:{"trap_detected","recall","descent","resting","running","repeat","unignoring"})
   if(p.contains(key) && (p[key].is_boolean()?p[key].get<bool>():p[key].is_number() && p[key].get<int>()!=0)) height+=ImGui::GetTextLineHeightWithSpacing();
  // The table already includes its bottom cell padding; the panel supplies
  // equal outer padding. Extra item spacing here makes the bottom look heavy.
  return height;
 }
 static void dungeon(const json &p,bool headings=true) {
  if(headings) AnybandUITheme::section("Dungeon",false);
  const char *dungeon_labels[]={"Depth","Light","Feel",""};
  const std::string dungeon_values[]={std::to_string(p.value("depth",0)),std::to_string(p.value("light",0)),p.value("feeling","â€”"),display_label(p.value("floor",""))};
  const std::string dungeon_tips[]={"Depth: "+std::to_string(p.value("depth_feet",p.value("depth",0)*50))+" feet","",p.value("feeling_description",""),""};
  const int columns=dungeon_columns(p,ImGui::GetContentRegionAvail().x);
  if(ImGui::BeginTable("Dungeon overview",columns,ImGuiTableFlags_SizingStretchSame)) {
   for(int i=0;i<4;++i) {
    ImGui::TableNextColumn(); metric(dungeon_labels[i],dungeon_values[i],dungeon_tips[i]);
   }
   ImGui::EndTable();
  }
  if(p.value("trap_detected",false)) ImGui::TextUnformatted("Trap-detected area");
  if(p.value("recall",0)) ImGui::TextUnformatted("Recall pending");
  if(p.value("descent",0)) ImGui::TextUnformatted("Descent pending");
  if(p.value("resting",0)) ImGui::TextUnformatted("Resting");
  if(p.value("running",0)) ImGui::TextUnformatted("Running");
  if(p.value("repeat",0)) ImGui::Text("Repeating: %d",p.value("repeat",0));
  if(p.value("unignoring",false)) ImGui::TextUnformatted("Showing ignored items");
 }

};
