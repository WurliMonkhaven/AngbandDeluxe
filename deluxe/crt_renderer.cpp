#include "crt_renderer.h"
#include "imgui_gpu_range.h"
#include "shaders/generated.h"

namespace {
// Linear-light halos need precision near black to avoid visible falloff bands.
constexpr auto light_format=SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
struct Bytes { const unsigned char *data; size_t size; };
struct Shader { Bytes dxil,spirv,msl; };
#define CRT_SHADER(name) Shader{{crt_shaders::name##_dxil,sizeof(crt_shaders::name##_dxil)}, {crt_shaders::name##_spirv,sizeof(crt_shaders::name##_spirv)}, {crt_shaders::name##_msl,sizeof(crt_shaders::name##_msl)}}
SDL_GPUShader *shader(SDL_GPUDevice *device,Shader code,SDL_GPUShaderStage stage,unsigned samplers,unsigned uniforms) {
 SDL_GPUShaderCreateInfo info{}; const auto formats=SDL_GetGPUShaderFormats(device);
 Bytes bytes{};
 if(formats&SDL_GPU_SHADERFORMAT_DXIL) { info.format=SDL_GPU_SHADERFORMAT_DXIL; bytes=code.dxil; info.entrypoint="main"; }
 else if(formats&SDL_GPU_SHADERFORMAT_SPIRV) { info.format=SDL_GPU_SHADERFORMAT_SPIRV; bytes=code.spirv; info.entrypoint="main"; }
 else if(formats&SDL_GPU_SHADERFORMAT_MSL) { info.format=SDL_GPU_SHADERFORMAT_MSL; bytes=code.msl; info.entrypoint="main0"; }
 else { SDL_SetError("No supported CRT shader format"); return nullptr; }
 info.code=bytes.data; info.code_size=bytes.size; info.stage=stage; info.num_samplers=samplers; info.num_uniform_buffers=uniforms;
 return SDL_CreateGPUShader(device,&info);
}
SDL_GPUGraphicsPipeline *pipeline(SDL_GPUDevice *device,SDL_GPUTextureFormat format,SDL_GPUShader *vertex,SDL_GPUShader *fragment) {
 SDL_GPUColorTargetDescription target{}; target.format=format;
 SDL_GPUGraphicsPipelineCreateInfo info{};
 info.vertex_shader=vertex; info.fragment_shader=fragment;
 info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
 info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL; info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
 info.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_1;
 info.target_info.color_target_descriptions=&target; info.target_info.num_color_targets=1;
 return SDL_CreateGPUGraphicsPipeline(device,&info);
}
struct BlurUniforms { float step[4],region[4]; };
struct CrtUniforms { float region[4],viewport[4],effects[4],shape[4]; };
static_assert(sizeof(BlurUniforms)==32 && sizeof(CrtUniforms)==64,"Shader uniform layout changed");
}

bool CrtRenderer::initialize(SDL_GPUDevice *device,SDL_GPUTextureFormat format) {
 shutdown(); device_=device; format_=format; error_.clear();
 auto *vertex=shader(device,CRT_SHADER(fullscreen),SDL_GPU_SHADERSTAGE_VERTEX,0,0);
 auto *blur=shader(device,CRT_SHADER(blur),SDL_GPU_SHADERSTAGE_FRAGMENT,1,1);
 auto *composite=shader(device,CRT_SHADER(crt),SDL_GPU_SHADERSTAGE_FRAGMENT,4,1);
 if(vertex && blur && composite) {
  blur_=pipeline(device,light_format,vertex,blur); composite_=pipeline(device,format,vertex,composite);
 }
 if(vertex) SDL_ReleaseGPUShader(device,vertex);
 if(blur) SDL_ReleaseGPUShader(device,blur);
 if(composite) SDL_ReleaseGPUShader(device,composite);
 SDL_GPUSamplerCreateInfo info{};
 info.min_filter=info.mag_filter=SDL_GPU_FILTER_LINEAR;
 info.address_mode_u=info.address_mode_v=info.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
 sampler_=SDL_CreateGPUSampler(device,&info);
 if(!blur_ || !composite_ || !sampler_) {
  error_=std::string("CRT rendering unavailable: ")+SDL_GetError(); shutdown(); return false;
 }
 return true;
}
void CrtRenderer::release_targets() {
 for(auto &target:targets_) { if(target) SDL_ReleaseGPUTexture(device_,target); target=nullptr; }
 width_=height_=0; history_valid_=false;
}
void CrtRenderer::shutdown() {
 if(!device_) return;
 release_targets();
 if(blur_) SDL_ReleaseGPUGraphicsPipeline(device_,blur_);
 if(composite_) SDL_ReleaseGPUGraphicsPipeline(device_,composite_);
 if(sampler_) SDL_ReleaseGPUSampler(device_,sampler_);
 blur_=composite_=nullptr; sampler_=nullptr; device_=nullptr;
}
bool CrtRenderer::resize(Uint32 width,Uint32 height) {
 if(width==width_ && height==height_) return true;
 release_targets();
 width_=width; height_=height;
 return ensure_target(0);
}
bool CrtRenderer::ensure_target(int index) {
 if(targets_[index]) return true;
 SDL_GPUTextureCreateInfo info{};
 info.type=SDL_GPU_TEXTURETYPE_2D; info.format=(index>=1 && index<=4)?light_format:format_;
 info.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER;
 info.layer_count_or_depth=info.num_levels=1;
 const unsigned divisor=(index==1 || index==2)?2:(index==3 || index==4)?4:1;
 info.width=std::max(1u,(width_+divisor-1)/divisor); info.height=std::max(1u,(height_+divisor-1)/divisor);
 targets_[index]=SDL_CreateGPUTexture(device_,&info);
 if(!targets_[index]) { error_=std::string("CRT texture allocation failed: ")+SDL_GetError(); release_targets(); return false; }
 return true;
}
void CrtRenderer::draw_ui(SDL_GPUCommandBuffer *cmd,SDL_GPUTexture *target,ImDrawData *data,bool clear,int first,int last) {
 SDL_GPUColorTargetInfo color{}; color.texture=target;
 color.load_op=clear?SDL_GPU_LOADOP_CLEAR:SDL_GPU_LOADOP_LOAD; color.store_op=SDL_GPU_STOREOP_STORE;
 color.clear_color={.04f,.05f,.07f,1};
 auto *pass=SDL_BeginGPURenderPass(cmd,&color,1,nullptr);
 DeluxeGpuRenderRange(data,cmd,pass,first,last<0?data->CmdListsCount:last); SDL_EndGPURenderPass(pass);
}
void CrtRenderer::draw_pass(SDL_GPUCommandBuffer *cmd,SDL_GPUTexture *target,SDL_GPUGraphicsPipeline *pipeline,
 SDL_GPUTexture **inputs,unsigned count,const void *uniforms,unsigned bytes) {
 SDL_GPUColorTargetInfo color{}; color.texture=target; color.load_op=SDL_GPU_LOADOP_DONT_CARE; color.store_op=SDL_GPU_STOREOP_STORE;
 SDL_PushGPUFragmentUniformData(cmd,0,uniforms,bytes);
 auto *pass=SDL_BeginGPURenderPass(cmd,&color,1,nullptr);
 SDL_BindGPUGraphicsPipeline(pass,pipeline);
 SDL_GPUTextureSamplerBinding bindings[4]{};
 for(unsigned i=0;i<count;++i) bindings[i]={inputs[i],sampler_};
 SDL_BindGPUFragmentSamplers(pass,0,bindings,count);
 SDL_DrawGPUPrimitives(pass,3,1,0,0); SDL_EndGPURenderPass(pass);
}
void CrtRenderer::render(SDL_GPUCommandBuffer *cmd,SDL_GPUTexture *destination,Uint32 width,Uint32 height,ImDrawData *data,const CrtFrame &frame) {
 ImGui_ImplSDLGPU3_PrepareDrawData(data,cmd); // One upload, shared by every UI pass.
 int game_index=-1;
 for(int i=0;i<data->CmdListsCount;++i) if(data->CmdLists[i]==frame.game) game_index=i;
 const bool enabled=frame.scope==2 || (frame.scope==1 && game_index>=0);
 if(!ready() || !enabled || !width || !height || data->DisplaySize.x<=0 || data->DisplaySize.y<=0 || !resize(width,height)) {
  if(device_) release_targets();
  history_valid_=false; draw_ui(cmd,destination,data,true); return;
 }
 const auto &settings=frame.settings;
 const bool persist=settings.level(Ghost)>0;
 for(int i=1;i<7;++i) {
  const bool wanted=i<=2?settings.level(Glow)>0:i<=4?settings.level(Bloom)>0:persist;
  if(wanted && !ensure_target(i)) { draw_ui(cmd,destination,data,true); return; }
  if(!wanted && targets_[i]) { SDL_ReleaseGPUTexture(device_,targets_[i]); targets_[i]=nullptr; }
 }
 const ImVec2 pos=frame.scope==2?data->DisplayPos:frame.game_pos;
 const ImVec2 size=frame.scope==2?data->DisplaySize:frame.game_size;
 if(size.x<=0 || size.y<=0) { history_valid_=false; draw_ui(cmd,destination,data,true); return; }
 // Render through the game draw list, then composite later UI layers unchanged.
 // This keeps popups, sidebar panels and tooltips out of game-only effects.
 const int split=frame.scope==2?data->CmdListsCount:game_index+1;
 draw_ui(cmd,targets_[0],data,true,0,split);
 CrtUniforms u{};
 u.region[0]=(pos.x-data->DisplayPos.x)/data->DisplaySize.x;
 u.region[1]=(pos.y-data->DisplayPos.y)/data->DisplaySize.y;
 u.region[2]=size.x/data->DisplaySize.x; u.region[3]=size.y/data->DisplaySize.y;
 const std::array<float,8> geometry={float(frame.scope),pos.x,pos.y,size.x,size.y,data->FramebufferScale.x,data->FramebufferScale.y,frame.ui_scale};
 if(!(settings==history_settings_) || geometry!=history_geometry_ || frame.session!=history_session_) {
  history_settings_=settings; history_geometry_=geometry; history_session_=frame.session; history_valid_=false;
 }
 u.viewport[0]=float(width); u.viewport[1]=float(height); u.viewport[2]=float(std::fmod(frame.seconds,12.));
 u.viewport[3]=history_valid_?crt_persistence_alpha(settings.level(Ghost),frame.seconds-last_time_):0;
 u.effects[0]=settings.level(Scanlines); u.effects[1]=settings.level(Glow); u.effects[2]=settings.level(Bloom); u.effects[3]=settings.level(Fringe);
 u.shape[0]=settings.level(Edges); u.shape[1]=.018f*settings.level(Barrel); u.shape[2]=settings.level(Hum);
 u.shape[3]=std::clamp(frame.health_glitch,0.f,1.f);
 auto blur=[&](int first,float radius,float threshold) {
  BlurUniforms b{}; std::copy(std::begin(u.region),std::end(u.region),b.region);
  const unsigned factor=first==1?2:4;
  const float blur_width=float((width+factor-1)/factor),blur_height=float((height+factor-1)/factor);
  b.step[0]=1/blur_width; b.step[1]=1/blur_height; b.step[2]=threshold; b.step[3]=float(factor);
  SDL_GPUTexture *input=targets_[0]; draw_pass(cmd,targets_[first+1],blur_,&input,1,&b,sizeof(b));
  // Match the old halo width, but sample a contiguous Gaussian at the actual
  // reduced resolution. Reuse the pair for prefilter -> horizontal -> vertical.
  b.step[1]=0; b.step[2]=1.7f*radius*blur_width/width; b.step[3]=0;
  input=targets_[first+1]; draw_pass(cmd,targets_[first],blur_,&input,1,&b,sizeof(b));
  b.step[0]=0; b.step[1]=1/blur_height; b.step[2]=1.7f*radius*blur_height/height;
  input=targets_[first]; draw_pass(cmd,targets_[first+1],blur_,&input,1,&b,sizeof(b));
 };
 const float glow_scale=std::max(1.f,frame.ui_scale*data->FramebufferScale.y);
 if(u.effects[1]>0) blur(1,(1.2f+2.0f*std::sqrt(u.effects[1]))*glow_scale,.08f);
 if(u.effects[2]>0) blur(3,(2.f+5.f*u.effects[2])*glow_scale,.30f);
 SDL_GPUTexture *inputs[]={targets_[0],u.effects[1]>0?targets_[2]:targets_[0],u.effects[2]>0?targets_[4]:targets_[0],history_valid_?targets_[5+previous_]:targets_[0]};
 auto *output=persist?targets_[5+1-previous_]:destination;
 draw_pass(cmd,output,composite_,inputs,4,&u,sizeof(u));
 if(persist) {
  SDL_GPUBlitInfo blit{}; blit.source.texture=output; blit.source.w=width; blit.source.h=height;
  blit.destination.texture=destination; blit.destination.w=width; blit.destination.h=height;
  blit.load_op=SDL_GPU_LOADOP_DONT_CARE; blit.filter=SDL_GPU_FILTER_NEAREST; SDL_BlitGPUTexture(cmd,&blit);
 }
 if(split<data->CmdListsCount) draw_ui(cmd,destination,data,false,split,data->CmdListsCount);
 previous_=1-previous_; history_valid_=persist; last_time_=frame.seconds;
}
