#pragma once
#include <filesystem>
#include <string>
#include <vector>

// One asset-discovery policy for development and relocatable Windows builds.
// Explicit command-line paths win; packaged files win over build-time fallbacks.
struct RuntimePaths {
 std::filesystem::path backend, data, font, audio;
 static std::filesystem::path user_directory(const std::filesystem::path &preferred) {
  // Compatibility identifier only: never rename or move an existing save store.
  const auto folder=preferred.filename().empty()?preferred.parent_path():preferred;
  const auto legacy=folder.parent_path().parent_path()/"AngbandDeluxe"/"AngbandDeluxe";
  std::error_code error;
  const bool populated=std::filesystem::exists(preferred,error) && !std::filesystem::is_empty(preferred,error);
  if(!populated && std::filesystem::is_directory(legacy,error)) return legacy;
  return preferred;
 }
 static RuntimePaths discover(const std::filesystem::path &base,
                              const std::filesystem::path &source_data,
                              const std::filesystem::path &source_font) {
  RuntimePaths p;
#ifdef _WIN32
  p.backend=base/"angband-backend.exe";
#else
  p.backend=base/"angband-backend";
#endif
  const bool packaged=std::filesystem::is_directory(base/"data");
  p.data=packaged?base/"data":source_data;
  p.font=(packaged || std::filesystem::is_regular_file(base/"fonts"/"Cousine-Regular.ttf"))?base/"fonts"/"Cousine-Regular.ttf":source_font;
  p.audio=base/"audio";
  return p;
 }
 std::vector<std::string> missing() const {
  std::vector<std::string> out;
  for(const auto &path:{backend,font,data/"gamedata"/"constants.txt",data/"gamedata"/"monster.txt",data/"customize"/"pref.prf",audio/"pack.json"})
   if(!std::filesystem::is_regular_file(path)) out.push_back(path.string());
  return out;
 }
};
