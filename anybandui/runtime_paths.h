#pragma once
#include <filesystem>
#include <string>
#include <vector>

// UI assets are independent of engine packages and have no source-tree fallback.
struct RuntimePaths {
 std::filesystem::path backend, data, font, audio, engines;
 static RuntimePaths discover(const std::filesystem::path &base) {
  RuntimePaths p;
  p.font=base/"fonts"/"Cousine-Regular.ttf";
  p.audio=base/"audio";
  p.engines=base/"engines";
  return p;
 }
 std::vector<std::string> missing() const {
  std::vector<std::string> out;
  for(const auto &path:{font,audio/"pack.json"})
   if(!std::filesystem::is_regular_file(path)) out.push_back(path.string());
  return out;
 }
};
