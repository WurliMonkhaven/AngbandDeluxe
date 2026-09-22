// Headless checks: no native window, GPU or computer-control automation.
#include "client.cpp"
#include <iostream>
#include <stdexcept>
#include <chrono>
static void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
int main(int argc,char **argv) {
 try {
  check(argc==2,"Pass an unused settings-file path");
  const fs::path path=argv[1];
  check(!fs::exists(path) && !fs::exists(path.string()+".tmp"),"Test file already exists");
  Connection connection; UI ui{connection}; ui.settings_path=path.string();
  // Legacy preferences acquire sensible defaults.
  { std::ofstream out(path); out<<R"({"scale":1.25,"game_fraction":0.65})"; }
  ui.load_settings(); check(ui.scale==1.25f && !ui.fullscreen && ui.crt==0,"Legacy settings");
  check(ui.crt_strength==1,"Existing settings should default to Classic");
  ui.begin_settings(); ui.draft_scale=1.5f; ui.draft_crt=2; ui.draft_fullscreen=true;
  ui.draft_crt_strength=3;
  check(ui.scale==1.25f && ui.crt==0 && !ui.fullscreen,"Draft changed live settings");
  ui.begin_settings(); // Reopening after Cancel discards the draft.
  check(ui.draft_scale==1.25f && ui.draft_crt==0 && !ui.draft_fullscreen,"Draft was retained");
  check(ui.draft_crt_strength==1 && ui.crt_strength==1,"Cancelled strength was applied");
  ui.draft_scale=1.5f; ui.draft_crt=1;
  check(ui.apply_settings(nullptr),"Save settings");
  UI loaded{connection}; loaded.settings_path=path.string(); loaded.load_settings();
  check(loaded.scale==1.5f && loaded.crt==1 && !loaded.fullscreen,"Saved values");
  loaded.begin_settings(); loaded.draft_crt=2;
  check(loaded.apply_settings(nullptr),"Replace settings file");
  ui.load_settings(); check(ui.crt==2,"Full CRT persistence");
  for(int strength=0;strength<4;++strength) {
   loaded.begin_settings(); loaded.draft_crt_strength=strength;
   check(loaded.apply_settings(nullptr),"Save CRT strength");
   ui.load_settings(); check(ui.crt_strength==strength,"Strength did not persist");
  }
  loaded.settings_path=(path/"missing"/"settings.json").string();
  loaded.begin_settings(); loaded.draft_scale=.75f;
  check(!loaded.apply_settings(nullptr) && loaded.scale==1.5f,"Failed save changed active settings");
  // Exercise draw-list generation and clip bounds without rendering a window.
  ImGui::CreateContext(); auto &io=ImGui::GetIO(); io.IniFilename=nullptr; io.DisplaySize=ImVec2(1600,1000); io.DeltaTime=1.f/60;
  io.BackendFlags|=ImGuiBackendFlags_RendererHasVtxOffset;
  unsigned char *pixels; int w,h; io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
  ImGui::NewFrame(); auto *draw=ImGui::GetForegroundDrawList();
  crt_effect(draw,ImVec2(20,30),ImVec2(400,300));
  check(draw->VtxBuffer.Size>0,"CRT effect emitted no geometry");
  for(const auto &v:draw->VtxBuffer) check(v.pos.x>=20 && v.pos.x<=420 && v.pos.y>=30 && v.pos.y<=330,"CRT escaped selected surface");
  auto plain=crt_phosphor(*draw);
  check(plain->IdxBuffer.Size==draw->IdxBuffer.Size,"Solid backgrounds acquired a halo");
  plain.reset();
  draw->PushClipRect(ImVec2(20,30),ImVec2(1400,950));
  const std::string row(80,'@');
  for(int y=0;y<34;++y) draw->AddText(ImVec2(30,40+y*20.f),IM_COL32_WHITE,row.c_str());
  draw->PopClipRect();
  const auto start=std::chrono::steady_clock::now();
  auto glowing=crt_phosphor(*draw);
  std::cout<<"CRT geometry for 2,720 glyphs: "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<" ms\n";
  check(glowing->IdxBuffer.Size>draw->IdxBuffer.Size,"Missing text glow geometry");
  bool red=false,cyan=false;
  for(const auto &v:glowing->VtxBuffer) {
   red|=(v.col&~IM_COL32_A_MASK)==(IM_COL32(255,0,0,0));
   cyan|=(v.col&~IM_COL32_A_MASK)==(IM_COL32(0,255,255,0));
  }
  check(red&&cyan,"Missing chromatic fringes");
  for(const auto &command:glowing->CmdBuffer) {
   check(command.IdxOffset+command.ElemCount<=unsigned(glowing->IdxBuffer.Size),"Invalid draw range");
   for(unsigned i=0;i<command.ElemCount;++i)
    check(command.VtxOffset+glowing->IdxBuffer[command.IdxOffset+i]<unsigned(glowing->VtxBuffer.Size),"Invalid vertex offset");
  }
  glowing.reset();
  {
  ImDrawList single(ImGui::GetDrawListSharedData()); single._ResetForNewFrame(); single.Flags=draw->Flags;
  single.PushClipRect(ImVec2(0,0),ImVec2(200,200)); single.PushTexture(io.Fonts->TexRef);
  single.AddText(ImVec2(50,50),IM_COL32_WHITE,"H");
  single.PopTexture(); single.PopClipRect();
  check(single.IdxBuffer.Size==6,"Expected one glyph quad");
  float glyph_top=single.VtxBuffer[0].pos.y,glyph_bottom=glyph_top;
  for(const auto &v:single.VtxBuffer) { glyph_top=std::min(glyph_top,v.pos.y); glyph_bottom=std::max(glyph_bottom,v.pos.y); }
  for(int strength=0;strength<4;++strength) {
   auto readable=crt_phosphor(single,strength);
   for(const auto &v:readable->VtxBuffer)
    check(v.pos.y==glyph_top || v.pos.y==glyph_bottom,"Glow introduced vertical glyph ghosts");
   for(int k=0;k<6;++k) {
    const auto &original=single.VtxBuffer[single.IdxBuffer[k]];
    const auto &core=readable->VtxBuffer[readable->VtxBuffer.Size-6+k];
    check(core.pos.x==original.pos.x && core.pos.y==original.pos.y && core.col==original.col,"Sharp glyph core was not drawn last");
   }
   ImDrawList first(ImGui::GetDrawListSharedData()),later(ImGui::GetDrawListSharedData());
   first._ResetForNewFrame(); later._ResetForNewFrame();
   crt_effect(&first,ImVec2(20,30),ImVec2(400,300),strength,0);
   crt_effect(&later,ImVec2(20,30),ImVec2(400,300),strength,4.5);
   check(first.VtxBuffer.Size==later.VtxBuffer.Size,"Hum animation changes geometry size");
   bool changed=false,bright_bar=false;
   for(int i=0;i<first.VtxBuffer.Size;++i) {
    const auto &v=first.VtxBuffer[i];
    check(v.pos.x>=20 && v.pos.x<=420 && v.pos.y>=30 && v.pos.y<=330,"Preset escaped surface");
    changed|=v.col!=later.VtxBuffer[i].col;
    bright_bar|=(v.col&~IM_COL32_A_MASK)==IM_COL32(90,120,110,0) && ((v.col>>IM_COL32_A_SHIFT)&255)>=10;
   }
   check(changed,"Hum bar did not roll");
   check(bright_bar,"Hum bar cannot lift dark background");
   auto preset=crt_phosphor(*draw,strength);
   for(const auto &command:preset->CmdBuffer) for(unsigned i=0;i<command.ElemCount;++i)
    check(command.VtxOffset+preset->IdxBuffer[command.IdxOffset+i]<unsigned(preset->VtxBuffer.Size),"Preset index overflow");
  }
  }
  ImGui::EndFrame(); ImGui::DestroyContext();
  fs::remove(path);
  std::cout<<"Graphics settings and CRT geometry checks passed\n";
  return 0;
 } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
