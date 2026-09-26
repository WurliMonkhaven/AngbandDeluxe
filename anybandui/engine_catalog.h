#pragma once
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include <nlohmann/json.hpp>
#include "protocol_contract.h"

namespace AnybandEngine {
using json=nlohmann::json;
namespace fs=std::filesystem;
inline const json& contract() { static const json value=json::parse(anybandui_contract_json); return value; }
inline bool safe_id(const std::string& s) {
 return !s.empty() && s.size()<=100 && s!="." && s!=".." && s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")==std::string::npos;
}
struct Package {
 fs::path directory, executable, data;
 std::string id,name,version,save_compatibility;
};
inline fs::path inside(const fs::path& root,const std::string& value) {
 fs::path relative(value);
 if(relative.empty() || relative.is_absolute() || relative.has_root_name()) throw std::runtime_error("Expected a relative package path");
 const auto base=fs::canonical(root), resolved=fs::canonical(base/relative);
 auto a=base.begin(),b=resolved.begin();
 for(;a!=base.end();++a,++b) if(b==resolved.end() || *a!=*b) throw std::runtime_error("Package path escapes its directory");
 return resolved;
}
inline Package load(const fs::path& manifest) {
 json j; std::ifstream stream(manifest); stream>>j;
 if(j.value("manifest_version",0)!=1 || j.value("profile","")!="full-v1" || j.at("protocol")!=contract().at("protocol"))
  throw std::runtime_error("Unsupported engine contract");
 const auto& e=j.at("engine"); Package p; p.directory=fs::canonical(manifest.parent_path());
 p.id=e.at("id").get<std::string>(); p.name=e.at("name").get<std::string>();
 p.version=e.at("version").get<std::string>(); p.save_compatibility=e.at("save_compatibility").get<std::string>();
 if(!safe_id(p.id)||!safe_id(p.save_compatibility)||p.name.empty()||p.version.empty()) throw std::runtime_error("Invalid engine identity");
 p.executable=inside(p.directory,j.at("executable").get<std::string>());
 p.data=inside(p.directory,j.at("data_directory").get<std::string>());
 if(!fs::is_regular_file(p.executable)||!fs::is_directory(p.data)) throw std::runtime_error("Incomplete engine package");
 return p;
}
struct Catalog {std::vector<Package> packages;std::vector<std::string> errors;};
inline Catalog discover(const fs::path& root) {
 Catalog result; std::error_code ec;
 if(!fs::is_directory(root,ec)) return result;
 std::vector<fs::path> manifests;
 if(fs::is_regular_file(root/"engine.anyband.json",ec)) manifests.push_back(root/"engine.anyband.json");
 for(fs::directory_iterator it(root,ec),end; !ec && it!=end; it.increment(ec))
  if(it->is_directory(ec) && fs::is_regular_file(it->path()/"engine.anyband.json",ec)) manifests.push_back(it->path()/"engine.anyband.json");
 std::sort(manifests.begin(),manifests.end());
 for(const auto& manifest:manifests) try { result.packages.push_back(load(manifest)); }
 catch(const std::exception& e) {result.errors.push_back(manifest.string()+": "+e.what());}
 if(ec) result.errors.push_back(ec.message());
 return result;
}
inline std::string validate_hello(const json& result,const Package* expected=nullptr) {
 try {
  if(result.at("protocol")!=contract().at("protocol")) return "Engine selected an unsupported protocol.";
  if(result.value("profile","")!="full-v1") return "Engine does not implement the full AnybandUI contract.";
  if(result.value("max_frame_bytes",0)<contract().at("max_frame_bytes").get<int>()) return "Engine frame limit is too small.";
  const auto& caps=result.at("capabilities");
  for(auto it=contract().at("required_capabilities").begin();it!=contract().at("required_capabilities").end();++it)
   if(!caps.contains(it.key()) || !caps.at(it.key()).is_number_integer() || caps.at(it.key()).get<int>()<it.value().get<int>()) return "Engine is missing required support: "+it.key();
  const auto& e=result.at("engine");
  if(expected && (e.value("id","")!=expected->id || e.value("version","")!=expected->version || e.value("save_compatibility","")!=expected->save_compatibility)) return "Engine identity does not match its package.";
  return {};
 } catch(const std::exception&) {return "Invalid engine handshake.";}
}
}
