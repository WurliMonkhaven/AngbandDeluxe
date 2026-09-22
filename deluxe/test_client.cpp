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
  ui.draft_crt_strength=3; ui.draft_hum_bar=true;
  check(!ui.hum_bar,"Hum bar draft applied immediately");
  check(ui.scale==1.25f && ui.crt==0 && !ui.fullscreen,"Draft changed live settings");
  ui.begin_settings(); // Reopening after Cancel discards the draft.
  check(ui.draft_scale==1.25f && ui.draft_crt==0 && !ui.draft_fullscreen,"Draft was retained");
  check(ui.draft_crt_strength==1 && ui.crt_strength==1,"Cancelled strength was applied");
  check(!ui.draft_hum_bar && !ui.hum_bar,"Cancelled hum bar was applied");
  ui.draft_scale=1.5f; ui.draft_crt=1; ui.draft_hum_bar=true;
  check(ui.apply_settings(nullptr),"Save settings");
  UI loaded{connection}; loaded.settings_path=path.string(); loaded.load_settings();
  check(loaded.scale==1.5f && loaded.crt==1 && !loaded.fullscreen,"Saved values");
  check(loaded.hum_bar,"Hum bar did not persist");
  loaded.begin_settings(); loaded.draft_crt=2; loaded.draft_hum_bar=false;
  check(loaded.apply_settings(nullptr),"Replace settings file");
  ui.load_settings(); check(!ui.hum_bar,"Hum bar off did not persist"); check(ui.crt==2,"Full CRT persistence");
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
  const auto curve_start=std::chrono::steady_clock::now();
  auto curved=crt_curve(*glowing,CrtCurve(ImVec2(0,0),io.DisplaySize,3));
  std::cout<<"Barrel geometry: "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-curve_start).count()<<" ms\n";
  for(const auto &cmd:curved->CmdBuffer) for(unsigned i=0;i<cmd.ElemCount;++i)
   check(cmd.VtxOffset+curved->IdxBuffer[cmd.IdxOffset+i]<unsigned(curved->VtxBuffer.Size),"Curved index overflow");
  curved.reset(); glowing.reset();
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
    bright_bar|=(v.col&~IM_COL32_A_MASK)==IM_COL32(150,150,150,0) && ((v.col>>IM_COL32_A_SHIFT)&255)>=5;
   }
   check(changed,"Hum bar did not roll");
   check(bright_bar,"Hum bar cannot lift dark background");
   ImDrawList off(ImGui::GetDrawListSharedData()),off_later(ImGui::GetDrawListSharedData());
   off._ResetForNewFrame(); off_later._ResetForNewFrame();
   crt_effect(&off,ImVec2(20,30),ImVec2(400,300),strength,0,false);
   crt_effect(&off_later,ImVec2(20,30),ImVec2(400,300),strength,4.5,false);
   check(off.VtxBuffer.Size<first.VtxBuffer.Size,"Disabled hum still emitted geometry");
   for(int i=0;i<off.VtxBuffer.Size;++i) check(off.VtxBuffer[i].col==off_later.VtxBuffer[i].col,"Disabled hum still animates");
   CrtCurve curve(ImVec2(20,30),ImVec2(400,300),strength);
   for(int y=0;y<=10;++y) for(int x=0;x<=10;++x) {
    const ImVec2 p(20+x*40.f,30+y*30.f); const auto mapped=curve.map(p),inverse=curve.map(mapped,true);
    check(std::abs(inverse.x-p.x)<.001f && std::abs(inverse.y-p.y)<.001f,"Curved mouse target mismatch");
   }
   const auto corner=curve.map(ImVec2(20,30));
   check(corner.x>20 && corner.y>30,"Missing barrel curvature");
   ImDrawList clipped(ImGui::GetDrawListSharedData()); clipped._ResetForNewFrame(); clipped.Flags=draw->Flags;
   clipped.PushClipRect(ImVec2(40,50),ImVec2(380,310));
   clipped.AddRectFilled(ImVec2(0,0),ImVec2(500,400),IM_COL32_WHITE);
   clipped.PopClipRect();
   auto bowed=crt_curve(clipped,curve);
   check(bowed->IdxBuffer.Size>6,"Large surface was not subdivided for curvature");
   for(const auto &v:bowed->VtxBuffer) {
    const auto p=curve.map(v.pos,true);
    check(p.x>=39.99f && p.x<=380.01f && p.y>=49.99f && p.y<=310.01f,"Curvature leaked past scroll clipping");
   }
   auto curved_single=crt_curve(single,curve);
   for(const auto &v:curved_single->VtxBuffer) {
    const auto original=curve.map(v.pos,true);
    check(original.x>=49.99f && original.y>=49.99f,"Curve introduced extra glyph copies");
   }
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
