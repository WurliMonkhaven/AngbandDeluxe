// Offscreen GPU readback tests: no window and no desktop/computer automation.
#include "crt_renderer.h"
#include "imgui_gpu_range.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <chrono>
#include <deque>
static void check(bool value,const char *message) { if(!value) throw std::runtime_error(std::string(message)+": "+SDL_GetError()); }
int main(int argc,char **argv) {
 try {
  check(SDL_Init(SDL_INIT_VIDEO),"SDL init");
  auto *gpu=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL|SDL_GPU_SHADERFORMAT_SPIRV|SDL_GPU_SHADERFORMAT_MSL|SDL_GPU_SHADERFORMAT_METALLIB,argc<3,argc>1?argv[1]:nullptr);
  check(gpu,"GPU device");
  ImGui::CreateContext(); auto &io=ImGui::GetIO(); io.IniFilename=nullptr;
  io.DisplaySize=ImVec2(256,128); io.DeltaTime=1.f/60;
  ImGui_ImplSDLGPU3_InitInfo init{}; init.Device=gpu; init.ColorTargetFormat=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM; init.MSAASamples=SDL_GPU_SAMPLECOUNT_1;
  check(ImGui_ImplSDLGPU3_Init(&init),"ImGui GPU init");
  CrtRenderer renderer; const bool initialized=renderer.initialize(gpu,init.ColorTargetFormat); check(initialized,renderer.error().c_str());
  SDL_GPUTextureCreateInfo texture{}; texture.type=SDL_GPU_TEXTURETYPE_2D; texture.format=init.ColorTargetFormat;
  texture.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER;
  texture.width=256; texture.height=128; texture.layer_count_or_depth=texture.num_levels=1;
  auto *target=SDL_CreateGPUTexture(gpu,&texture); check(target,"Output texture");
  SDL_GPUTransferBufferCreateInfo transfer{}; transfer.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD; transfer.size=256*128*4;
  auto *download=SDL_CreateGPUTransferBuffer(gpu,&transfer); check(download,"Readback buffer");
  if(argc>2 && std::string(argv[2])=="--bench") {
   SDL_ReleaseGPUTexture(gpu,target); texture.width=1920; texture.height=1080;
   target=SDL_CreateGPUTexture(gpu,&texture); check(target,"Benchmark texture");
   io.DisplaySize=ImVec2(1920,1080);
   for(int scope:{0,1,2}) {
    std::deque<SDL_GPUFence*> pending;
    std::vector<double> records,waits;
    unsigned long long steady_growths=0;
    for(int sample=0;sample<240;++sample) {
     auto now=[] { return std::chrono::steady_clock::now(); };
     auto elapsed=[&](auto start) { return std::chrono::duration<double,std::milli>(now()-start).count(); };
     auto wait_start=now();
     if(pending.size()>=2) { auto *fence=pending.front(); pending.pop_front(); SDL_WaitForGPUFences(gpu,true,&fence,1); SDL_ReleaseGPUFence(gpu,fence); }
     const double wait_ms=elapsed(wait_start);
     ImGui_ImplSDLGPU3_NewFrame(); ImGui::NewFrame();
     auto *draw=ImGui::GetBackgroundDrawList();
     draw->AddRectFilled(ImVec2(0,0),io.DisplaySize,IM_COL32(8,10,14,255));
     const std::string row(80,'@');
     for(int y=0;y<34;++y) draw->AddText(ImVec2(20,20+y*22.f),IM_COL32_WHITE,row.c_str());
     auto *overlay=ImGui::GetForegroundDrawList();
     for(int y=0;y<25;++y) overlay->AddText(ImVec2(1400,20+y*25.f),IM_COL32_WHITE,"Inventory and character information");
     ImGui::Render();
     auto *cmd=SDL_AcquireGPUCommandBuffer(gpu);
     CrtFrame frame; frame.scope=scope; frame.settings=CrtSettings(3); frame.game=draw;
     frame.game_pos=ImVec2(0,0); frame.game_size=ImVec2(1300,950); frame.seconds=sample/60.;
     const auto before=DeluxeGpuGetUploadStats();
     auto start=now(); renderer.render(cmd,target,1920,1080,ImGui::GetDrawData(),frame);
     const double record_ms=elapsed(start);
     const auto after=DeluxeGpuGetUploadStats();
     check(after.uploads==before.uploads+1,"Benchmark frame uploaded twice");
     if(sample>20) steady_growths+=after.buffer_growths-before.buffer_growths;
     pending.push_back(SDL_SubmitGPUCommandBufferAndAcquireFence(cmd));
     if(sample>20) { records.push_back(record_ms); waits.push_back(wait_ms); }
    }
    for(auto *fence:pending) { SDL_WaitForGPUFences(gpu,true,&fence,1); SDL_ReleaseGPUFence(gpu,fence); }
    auto report=[](const char *label,std::vector<double> times) {
     std::sort(times.begin(),times.end());
     std::cout<<label<<" p50="<<times[times.size()/2]<<" p95="<<times[times.size()*95/100]<<" max="<<times.back()<<" ms ";
    };
    std::cout<<"scope="<<scope<<" "; report("record",records); report("wait",waits); std::cout<<" steady buffer growths="<<steady_growths<<"\n";
    check(steady_growths==0,"Idle frames keep reallocating upload buffers");
   }
   renderer.shutdown(); SDL_ReleaseGPUTransferBuffer(gpu,download); SDL_ReleaseGPUTexture(gpu,target);
   SDL_WaitForGPUIdle(gpu); ImGui_ImplSDLGPU3_Shutdown(); ImGui::DestroyContext(); SDL_DestroyGPUDevice(gpu); SDL_Quit(); return 0;
  }
  CrtFrame frame; frame.scope=2;
  for(auto &part:frame.settings.parts) part.enabled=false;
  auto render=[&](bool lit,bool overlay=false) {
   io.DisplaySize=ImVec2(float(texture.width),float(texture.height));
   ImGui_ImplSDLGPU3_NewFrame(); ImGui::NewFrame();
   auto *draw=ImGui::GetBackgroundDrawList();
   draw->AddRectFilled(ImVec2(0,0),ImVec2(256,128),IM_COL32(0,0,0,255));
   draw->AddRectFilled(ImVec2(8,8),ImVec2(248,24),IM_COL32_WHITE);
   frame.game=draw;
   if(overlay) ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(44,36),ImVec2(52,44),IM_COL32(0,255,0,255));
   if(lit) draw->AddRectFilled(ImVec2(40,32),ImVec2(56,48),IM_COL32_WHITE);
   draw->AddRectFilled(ImVec2(200,96),ImVec2(220,112),IM_COL32(255,0,0,255));
   ImGui::Render();
   auto *cmd=SDL_AcquireGPUCommandBuffer(gpu); check(cmd,"Command buffer");
   const auto uploads_before=DeluxeGpuGetUploadStats().uploads;
   renderer.render(cmd,target,texture.width,texture.height,ImGui::GetDrawData(),frame);
   check(DeluxeGpuGetUploadStats().uploads==uploads_before+1,"UI was uploaded more than once in a frame");
   auto *copy=SDL_BeginGPUCopyPass(cmd);
   SDL_GPUTextureRegion region{}; region.texture=target; region.w=texture.width; region.h=texture.height; region.d=1;
   SDL_GPUTextureTransferInfo dest{}; dest.transfer_buffer=download;
   SDL_DownloadFromGPUTexture(copy,&region,&dest); SDL_EndGPUCopyPass(copy);
   auto *fence=SDL_SubmitGPUCommandBufferAndAcquireFence(cmd); check(fence,"Submit");
   check(SDL_WaitForGPUFences(gpu,true,&fence,1),"Readback fence"); SDL_ReleaseGPUFence(gpu,fence);
   auto *bytes=static_cast<unsigned char*>(SDL_MapGPUTransferBuffer(gpu,download,false)); check(bytes,"Readback map");
   std::vector<unsigned char> result(bytes,bytes+texture.width*texture.height*4); SDL_UnmapGPUTransferBuffer(gpu,download);
   return result;
  };
  auto pixel=[&](const auto &image,int x,int y,int channel=0) { return image[(y*texture.width+x)*4+channel]; };
  frame.scope=0; auto original=render(true);
  check(pixel(original,48,40)>250 && pixel(original,210,104,1)<3,"Offscreen source pattern");
  frame.scope=2; auto plain=render(true);
  check(pixel(plain,48,40)>250 && pixel(plain,48,88)<3 && pixel(plain,210,104)>250,"Shader orientation/pass-through");
  frame.settings.parts[Glow]={true,100}; auto glow=render(true);
  check(pixel(glow,58,40)>pixel(plain,58,40)+5,"GPU glow outside bright pixels");
  frame.settings.parts[Glow].enabled=false; frame.settings.parts[Bloom]={true,100}; auto bloom=render(true);
  check(pixel(bloom,64,40)>pixel(plain,64,40)+2,"GPU bloom diffusion");
  frame.settings.parts[Bloom].enabled=false; frame.settings.parts[Fringe]={true,100}; auto fringe=render(true);
  check(pixel(fringe,55,40)<pixel(fringe,55,40,2)-20,"GPU chromatic aberration");
  frame.settings.parts[Fringe].enabled=false; frame.settings.parts[Edges]={true,100}; auto vignette=render(true);
  check(pixel(vignette,10,8)<pixel(plain,10,8)-15,"GPU vignetting");
  frame.settings.parts[Edges].enabled=false; frame.settings.parts[Hum]={true,100}; frame.seconds=6; auto hum=render(false);
  check(pixel(hum,128,60)>15 && pixel(hum,128,66)<3,"Hum front direction and trailing fade");
  frame.settings.parts[Hum].enabled=false;
  frame.settings.parts[Scanlines]={true,100}; auto lines=render(true);
  check(pixel(lines,48,39)!=pixel(lines,48,40),"GPU scanline pattern");
  frame.settings.parts[Barrel]={true,100}; auto curved=render(true);
  CrtCurve curve(ImVec2(0,0),io.DisplaySize,frame.settings);
  for(int x:{32,128,224}) {
   const ImVec2 p(x+.5f,16.5f); const float row=curve.map(p,true).y;
   const float dx=std::abs(curve.map(ImVec2(p.x+.5f,p.y),true).y-curve.map(ImVec2(p.x-.5f,p.y),true).y);
   const float dy=std::abs(curve.map(ImVec2(p.x,p.y+.5f),true).y-curve.map(ImVec2(p.x,p.y-.5f),true).y);
   const float aa=std::max(.5f,.5f*(dx+dy));
   const float phase=(row-.5f)/3+.5f;
   const float distance=std::abs((phase-std::floor(phase))*3-1.5f);
   const float t=std::clamp((distance-(.5f-aa))/(2*aa),0.f,1.f);
   const float expected=255-150*(1-t*t*(3-2*t));
   check(std::abs(pixel(curved,x,16)-expected)<5,"Scanlines do not follow inverse barrel coordinates");
  }
  frame.scope=1; frame.game_pos=ImVec2(0,0); frame.game_size=ImVec2(128,128);
  auto scoped=render(true,true);
  check(pixel(scoped,210,104)==pixel(original,210,104),"Game-only effects touched sidebar");
  check(pixel(scoped,48,40,1)>250 && pixel(scoped,48,40)<3,"Game-only effects touched overlay popup");
  frame.scope=2; frame.settings.parts[Barrel].enabled=false;
  frame.settings.parts[Scanlines].enabled=false; frame.settings.parts[Ghost]={true,100};
  frame.seconds=1; render(true); frame.seconds+=1./60.; auto fading=render(false);
  check(pixel(fading,48,40)>30 && pixel(fading,48,40)<220,"Temporal afterimage");
  frame.seconds+=.2; auto faded=render(false); check(pixel(faded,48,40)<3,"Afterimage expiration");
  frame.session="new session"; auto reset=render(false); check(pixel(reset,48,40)<3,"Session history reset");
  frame.seconds=3; render(true); frame.ui_scale=1.5f; frame.seconds+=1./60.;
  auto rescaled=render(false); check(pixel(rescaled,48,40)<3,"UI scale retained stale history");
  frame.seconds=4; render(true);
  SDL_ReleaseGPUTexture(gpu,target); texture.width=128; texture.height=64;
  target=SDL_CreateGPUTexture(gpu,&texture); check(target,"Resized destination");
  frame.seconds+=1./60.; auto resized=render(false); check(pixel(resized,48,40)<3,"Resize retained stale history");
  frame.scope=0; render(true); frame.scope=2; frame.seconds+=1./60.;
  auto toggled=render(false); check(pixel(toggled,48,40)<3,"Disabled CRT retained stale history");
  renderer.shutdown(); SDL_ReleaseGPUTransferBuffer(gpu,download); SDL_ReleaseGPUTexture(gpu,target);
  SDL_WaitForGPUIdle(gpu); ImGui_ImplSDLGPU3_Shutdown(); ImGui::DestroyContext(); SDL_DestroyGPUDevice(gpu); SDL_Quit();
  std::cout<<"Offscreen GPU CRT checks passed\n"; return 0;
 } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
