#include "tileset.h"
#include <fstream>
#include <vector>
#include <cstring>
#include <cstdlib>
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

bool TilesetLibrary::load(int id) {
 if(id<1 || id>6 || !device) return false;
 auto &a=atlases[id];
 if(a.tried) return a.texture!=nullptr;
 a.tried=true;
 // Shockbolt's two palettes use different mappings into the same atlas.
 if(id>=5 && atlases[id==5?6:5].texture) { a=atlases[id==5?6:5]; return true; }
 std::ifstream file(directory/sets[id].file,std::ios::binary);
 std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),{});
 int channels=0;
 auto *pixels=bytes.empty()?nullptr:stbi_load_from_memory(bytes.data(),int(bytes.size()),&a.width,&a.height,&channels,4);
 if(!pixels) { a.error="Could not read tileset artwork. ASCII will be used."; return false; }
 const size_t size=size_t(a.width)*a.height*4;
 SDL_GPUTextureCreateInfo info{}; info.type=SDL_GPU_TEXTURETYPE_2D; info.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
 info.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER; info.width=a.width; info.height=a.height; info.layer_count_or_depth=info.num_levels=1;
 a.texture=SDL_CreateGPUTexture(device,&info);
 SDL_GPUTransferBufferCreateInfo transfer{}; transfer.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD; transfer.size=Uint32(size);
 auto *buffer=SDL_CreateGPUTransferBuffer(device,&transfer);
 auto *mapped=buffer?SDL_MapGPUTransferBuffer(device,buffer,false):nullptr;
 if(!a.texture || !mapped) {
  a.error=SDL_GetError(); stbi_image_free(pixels);
  if(buffer) SDL_ReleaseGPUTransferBuffer(device,buffer);
  if(a.texture) SDL_ReleaseGPUTexture(device,a.texture);
  a.texture=nullptr; return false;
 }
 memcpy(mapped,pixels,size); stbi_image_free(pixels); SDL_UnmapGPUTransferBuffer(device,buffer);
 auto *cmd=SDL_AcquireGPUCommandBuffer(device);
 if(!cmd) { a.error=SDL_GetError(); SDL_ReleaseGPUTransferBuffer(device,buffer); SDL_ReleaseGPUTexture(device,a.texture); a.texture=nullptr; return false; }
 auto *copy=SDL_BeginGPUCopyPass(cmd);
 SDL_GPUTextureTransferInfo source{}; source.transfer_buffer=buffer;
 SDL_GPUTextureRegion region{}; region.texture=a.texture; region.w=a.width; region.h=a.height; region.d=1;
 SDL_UploadToGPUTexture(copy,&source,&region,false); SDL_EndGPUCopyPass(copy);
 bool ok=SDL_SubmitGPUCommandBuffer(cmd); SDL_ReleaseGPUTransferBuffer(device,buffer);
 if(!ok) { a.error=SDL_GetError(); SDL_ReleaseGPUTexture(device,a.texture); a.texture=nullptr; }
 return ok;
}
void TilesetLibrary::shutdown() {
 if(!device) return;
 SDL_WaitForGPUIdle(device);
 if(atlases[5].texture==atlases[6].texture) atlases[6].texture=nullptr;
 for(auto &a:atlases) { if(a.texture) SDL_ReleaseGPUTexture(device,a.texture); a={}; }
 device=nullptr;
}

void TilesetLibrary::preview(int id) {
 if(id<1 || id>6 || !load(id)) return;
 static const int samples[6][10]={
{162,128,186,128,128,140,151,154,145,143},
{131,128,134,128,128,146,142,169,142,158},
{128,150,146,150,135,131,147,141,157,140},
{132,128,135,128,128,146,128,172,145,162},
{200,154,203,150,135,131,147,141,210,157},
{128,151,128,150,135,131,147,141,210,157}
 };
 const auto &s=samples[id-1]; const float cell=std::min(48.f,ImGui::GetContentRegionAvail().x/9);
 const auto p=ImGui::GetCursorScreenPos(); auto *d=ImGui::GetWindowDrawList();
 d->AddRectFilled(p,{p.x+cell*9,p.y+cell*3},IM_COL32(8,10,13,255));
 for(int y=0;y<3;++y) for(int x=0;x<9;++x) {
  const bool wall=y==0 || y==2 || x==0 || x==8;
  tile(d,id,s[wall?2:0],s[wall?3:1],{p.x+x*cell,p.y+y*cell},{cell,cell});
 }
 for(int i=2;i<5;++i) tile(d,id,s[i*2],s[i*2+1],{p.x+(i*2-2)*cell,p.y+cell},{cell,cell});
 ImGui::Dummy({cell*9,cell*3});
}
