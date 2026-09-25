// Offscreen visual review of a captured protocol fixture. Never opens a window
// or connects to a running game. Usage: preview fixture.json output.bmp [width height].
#include "client.cpp"
#include <iostream>
int main(int argc,char **argv) {
 try {
  if(argc<3) throw std::runtime_error("Provide a fixture and output BMP path");
  json fixture; std::ifstream(argv[1])>>fixture;
  const int w=argc>3?std::stoi(argv[3]):1440,h=argc>4?std::stoi(argv[4]):1000;
  if(w<400 || h<400 || w>4096 || h>4096) throw std::runtime_error("Invalid preview dimensions");
  if(!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
  auto *gpu=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL,false,"direct3d12");
  if(!gpu) throw std::runtime_error(SDL_GetError());
  ImGui::CreateContext(); DeluxeTheme::apply();
  auto &io=ImGui::GetIO(); io.IniFilename=nullptr; io.DisplaySize=ImVec2(float(w),float(h)); io.DeltaTime=1.f/60;
  FontLibrary fonts; fonts.load(fs::path(DELUXE_FONT_FILE).parent_path()); io.FontDefault=fonts.fonts[0]; ImGui::GetStyle().FontSizeBase=18;
  ImGui_ImplSDLGPU3_InitInfo init{}; init.Device=gpu; init.ColorTargetFormat=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM; init.MSAASamples=SDL_GPU_SAMPLECOUNT_1;
  if(!ImGui_ImplSDLGPU3_Init(&init)) throw std::runtime_error(SDL_GetError());
  CrtRenderer renderer; if(!renderer.initialize(gpu,init.ColorTargetFormat)) throw std::runtime_error(renderer.error());
  SDL_GPUTextureCreateInfo tex{}; tex.type=SDL_GPU_TEXTURETYPE_2D; tex.format=init.ColorTargetFormat; tex.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER; tex.width=w; tex.height=h; tex.layer_count_or_depth=tex.num_levels=1;
  auto *target=SDL_CreateGPUTexture(gpu,&tex);
  SDL_GPUTransferBufferCreateInfo buf{}; buf.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD; buf.size=w*h*4;
  auto *download=SDL_CreateGPUTransferBuffer(gpu,&buf);
  if(!target || !download) throw std::runtime_error(SDL_GetError());
  Connection c; c.connected=c.negotiated=true; c.saves=fixture.value("saves",json::array()); c.catalog=fixture.value("catalog",json::object()); c.commands=fixture.value("commands",json::array());
  if(fixture.contains("previous_state")) { c.inventory_changes.update(fixture["previous_state"]); c.level_feedback.update(fixture["previous_state"],double(SDL_GetTicksNS())/1e9); }
  c.receive({{"kind","event"},{"event","state.changed"},{"data",fixture.at("state")}});
  if(fixture.contains("level_elapsed")) c.level_feedback.started=double(SDL_GetTicksNS())/1e9-fixture["level_elapsed"].get<double>();
  if(fixture.contains("prompt")) c.prompt=fixture["prompt"];
  if(fixture.contains("blast")) c.blast=fixture["blast"];
  UI ui{c}; ui.font_library=&fonts; ui.font_settings.load(fixture.value("fonts",json::object())); ui.base_style=ImGui::GetStyle();
  ui.quit_dialog=fixture.value("quit_dialog",false);
  c.inventory_requested=fixture.value("inventory_window",false);
  c.equipment_requested=fixture.value("equipment_window",false);
  ui.theme_settings.load(fixture.value("theme",json::object())); DeluxeTheme::configure(ui.theme_settings);
  ui.quickbar_enabled=fixture.value("quickbar_enabled",false);
  c.capabilities=fixture.value("capabilities",json::object());
  if(fixture.contains("layout_preset")) ui.layout.preset(fixture["layout_preset"].get<int>());
  if(fixture.contains("layout")) ui.layout.load(fixture["layout"]);
  ui.layout.dividers_locked=fixture.value("layout_dividers_locked",true);
  ui.layout.floating_locked=fixture.value("layout_floating_locked",false);
  if(fixture.value("layout_edit",false)) { ui.layout.before=ui.layout.arrangement(); ui.layout.editing=true; }
  if(fixture.value("layout_float",false)) ui.layout.move({WorkspaceLayout::Inventory,0,5});
  ui.scene_animation=fixture.contains("transition");
  ui.scale=std::clamp(fixture.value("scale",1.f),.75f,1.5f);
  ImGui::GetStyle().ScaleAllSizes(ui.scale); ImGui::GetStyle().FontScaleMain=ui.scale;
  for(const auto &item:c.state.value("items",json::array())) if(item.value("location","")=="Pack") { ui.selected=item.value("id",""); break; }
  for(int i=0;i<std::clamp(fixture.value("frames",3),3,120);++i) {
   for(const auto &event:fixture.value("input",json::array())) if(event.value("frame",-1)==i) {
    if(event.contains("mouse")) io.AddMousePosEvent(event["mouse"][0].get<float>(),event["mouse"][1].get<float>());
    if(event.contains("down")) io.AddMouseButtonEvent(0,event["down"].get<bool>());
    if(event.contains("right")) io.AddMouseButtonEvent(1,event["right"].get<bool>());
   }
   if(fixture.contains("transition")) {
    const auto name=fixture["transition"].value("kind","");
    using K=SceneTransitions::Kind;
    const auto kind=name=="death"?K::Death:name=="shop"?K::Shop:name=="up"?K::Up:name=="down"?K::Down:name=="dissolve"?K::Dissolve:K::Load;
    const double born=double(SDL_GetTicksNS())/1e9-fixture["transition"].value("elapsed",.15);
    if(kind==K::Death) {
     c.transitions.death(c.state,c.game_grid,born);
    } else c.transitions.start(kind,born,c.game_grid);
    c.transitions.label=fixture["transition"].value("label","The Alchemist");
   }

   if(fixture.contains("motion")) {
    auto batch=fixture["motion"]; batch["received"]=double(SDL_GetTicksNS())/1e9-fixture.value("motion_elapsed",.14);
    ui.motion_feedback.ripples.clear(); c.motion_events.push_back(batch);
   }
   if(fixture.contains("projectiles")) {
    auto batch=fixture["projectiles"]; batch["received"]=double(SDL_GetTicksNS())/1e9-fixture.value("projectile_elapsed",.15);
    ui.projectile_feedback.tiles.clear(); c.projectile_events.push_back(batch);
   }
   ImGui_ImplSDLGPU3_NewFrame(); io.FontDefault=fonts.get(ui.font_settings.interface_font); ImGui::NewFrame();
   ui.draw(nullptr);
   if(fixture.value("font_picker",false) && i==0) {
    ui.begin_settings(); ImGui::Begin("Angband Deluxe"); ImGui::OpenPopup("Settings"); ImGui::End();
   }
   if(fixture.contains("journal")) {
    ImGui::SetNextWindowSize({std::min(float(w)-40,ImGui::GetFontSize()*48),float(h)-80});
    ImGui::SetNextWindowPos({30,40}); ImGui::SetNextWindowFocus();
    ImGui::Begin("Run journal - offscreen review",nullptr,ImGuiWindowFlags_NoSavedSettings);
    ui.run_journal.contents(fixture["journal"]); ImGui::End();
   }
   if(fixture.value("equipment_portrait",false)) {
    ImGui::SetNextWindowSize({std::min(float(w)-40,ImGui::GetFontSize()*53),float(h)-80});
    ImGui::SetNextWindowPos({20,40});
    ImGui::SetNextWindowFocus();
    ImGui::Begin("Equipment - offscreen review",nullptr,ImGuiWindowFlags_NoSavedSettings);
    EquipmentPortrait::draw(c.state["items"],c.state["slots"],c.state["player"].value("name",""));
    ImGui::End();
   }
   if(fixture.contains("keybindings")) {
    if(i==0) ui.keybinding_editor.load(fixture["keybindings"]);
    ImGui::SetNextWindowSize(ImVec2(504*ui.scale,612*ui.scale));
    ImGui::SetNextWindowPos(ImVec2((w-504*ui.scale)*.5f,40));
    ImGui::SetNextWindowFocus();
    ImGui::Begin("Keybindings - offscreen review",nullptr,ImGuiWindowFlags_NoSavedSettings);
    ui.keybinding_editor.draw(); ImGui::End();
   }
   ImGui::Render();
   auto *cmd=SDL_AcquireGPUCommandBuffer(gpu); CrtFrame frame; frame.scope=fixture.value("crt_scope",0); frame.game=ui.game_draw_list; frame.game_pos=ui.game_pos; frame.game_size=ui.game_size;
   frame.desaturation=ui.scene_animation?c.transitions.desaturation(double(SDL_GetTicksNS())/1e9):0;
   frame.retain_scene=ui.scene_animation;
   frame.hold_scene=ui.scene_animation && c.transitions.hold_shop(double(SDL_GetTicksNS())/1e9);
   frame.scene_darkness=ui.scene_animation?c.transitions.shop_darkness(double(SDL_GetTicksNS())/1e9):0;
   renderer.render(cmd,target,w,h,ImGui::GetDrawData(),frame);
   auto *copy=SDL_BeginGPUCopyPass(cmd);
   SDL_GPUTextureRegion region{}; region.texture=target; region.w=w; region.h=h; region.d=1;
   SDL_GPUTextureTransferInfo dest{}; dest.transfer_buffer=download;
   SDL_DownloadFromGPUTexture(copy,&region,&dest); SDL_EndGPUCopyPass(copy);
   auto *fence=SDL_SubmitGPUCommandBufferAndAcquireFence(cmd); SDL_WaitForGPUFences(gpu,true,&fence,1); SDL_ReleaseGPUFence(gpu,fence);
  }
  if(fixture.value("layout_trace",false)) { std::ofstream out(std::string(argv[2])+".json"); out<<ui.layout.arrangement().dump(2); }
  if(fixture.value("inventory_trace",false)) { std::ofstream out(std::string(argv[2])+".json"); out<<json{{"open",ui.inventory_window_open},{"selected",ui.inventory_selected},{"outgoing",c.outgoing}}.dump(2); }
  auto *bytes=SDL_MapGPUTransferBuffer(gpu,download,false);
  auto *surface=SDL_CreateSurfaceFrom(w,h,SDL_PIXELFORMAT_RGBA32,bytes,w*4);
  if(!surface || !SDL_SaveBMP(surface,argv[2])) throw std::runtime_error(SDL_GetError());
  SDL_DestroySurface(surface); SDL_UnmapGPUTransferBuffer(gpu,download);
  SDL_WaitForGPUIdle(gpu); renderer.shutdown(); SDL_ReleaseGPUTexture(gpu,target); SDL_ReleaseGPUTransferBuffer(gpu,download);
  ImGui_ImplSDLGPU3_Shutdown(); ImGui::DestroyContext(); SDL_DestroyGPUDevice(gpu); SDL_Quit();
  return 0;
 } catch(const std::exception &e) { std::cerr<<e.what()<<"\n"; return 1; }
}
