#pragma once
#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Read/parse independently of display cadence. Small Windows pipes otherwise
// stall the backend once per pipe fragment until another UI frame reads it.
// Only complete messages cross to the UI; the workers never access game state.
class BackendReader {
public:
 struct Batch {
  std::deque<nlohmann::json> frames;
  std::string diagnostic, error;
  bool finished=false;
 };
 BackendReader(SDL_IOStream *output,SDL_IOStream *errors) {
  make_blocking(output);
  if(errors) make_blocking(errors);
  try {
   output_done=false;
   output_worker=std::thread([this,output]{read(output,false,output_done);});
   if(errors) {
    errors_done=false;
    errors_worker=std::thread([this,errors]{read(errors,true,errors_done);});
   }
  } catch(...) { stop(); throw; }
 }
 ~BackendReader() { stop(); }
 BackendReader(const BackendReader&)=delete;
 BackendReader& operator=(const BackendReader&)=delete;
 Batch take() {
  Batch result;
  {
   std::lock_guard<std::mutex> lock(mutex);
   result.frames.swap(pending.frames);
   result.diagnostic.swap(pending.diagnostic);
   result.error.swap(pending.error);
   result.finished=output_done && errors_done;
   queued_bytes=0;
  }
  room.notify_one();
  return result;
 }
private:
 static constexpr size_t frame_limit=1048576, queue_limit=4*frame_limit;
 std::atomic<bool> stopping{false}, output_done{true}, errors_done{true};
 std::mutex mutex;
 std::condition_variable room;
 Batch pending;
 size_t queued_bytes=0;
 std::thread output_worker, errors_worker;

 static void make_blocking(SDL_IOStream *stream) {
#ifdef _WIN32
  // Only worker-owned read handles change mode; stdin remains nonblocking.
  auto handle=static_cast<HANDLE>(SDL_GetPointerProperty(SDL_GetIOProperties(stream),SDL_PROP_IOSTREAM_WINDOWS_HANDLE_POINTER,nullptr));
  if(handle) {
   DWORD mode=PIPE_WAIT;
   if(!SetNamedPipeHandleState(handle,&mode,nullptr,nullptr))
    throw std::runtime_error("Cannot configure backend pipe reader");
  }
#else
  (void)stream;
#endif
 }
 void stop() {
  stopping=true; room.notify_all();
#ifdef _WIN32
  // Cancel a blocked ReadFile. Repeat until done to cover the small race where
  // stop is requested just before a reader enters ReadFile. No process killing.
  while((output_worker.joinable() && !output_done) || (errors_worker.joinable() && !errors_done)) {
   if(output_worker.joinable() && !output_done) CancelSynchronousIo(output_worker.native_handle());
   if(errors_worker.joinable() && !errors_done) CancelSynchronousIo(errors_worker.native_handle());
   SDL_DelayNS(1000000);
  }
#endif
  if(output_worker.joinable()) output_worker.join();
  if(errors_worker.joinable()) errors_worker.join();
 }
 void read(SDL_IOStream *stream,bool diagnostic,std::atomic<bool> &done) {
  try {
   std::string received;
   char buffer[32768];
   while(!stopping) {
    const size_t n=SDL_ReadIO(stream,buffer,sizeof(buffer));
    if(stopping) break;
    if(!n) {
     const auto status=SDL_GetIOStatus(stream);
     if(status==SDL_IO_STATUS_ERROR) throw std::runtime_error(SDL_GetError());
     if(status==SDL_IO_STATUS_EOF) {
      if(!received.empty()) throw std::runtime_error("Backend stopped during a message");
      break;
     }
     SDL_DelayNS(1000000); // Non-Windows nonblocking streams, or test streams.
     continue;
    }
    if(diagnostic) {
     std::lock_guard<std::mutex> lock(mutex);
     pending.diagnostic.append(buffer,n);
     if(pending.diagnostic.size()>65536) pending.diagnostic.erase(0,pending.diagnostic.size()-65536);
     continue;
    }
    received.append(buffer,n);
    size_t end;
    while((end=received.find('\n'))!=std::string::npos) {
     if(end>frame_limit) throw std::runtime_error("Backend frame too large");
     auto frame=nlohmann::json::parse(received.begin(),received.begin()+end);
     {
      std::unique_lock<std::mutex> lock(mutex);
      room.wait(lock,[&]{return stopping || queued_bytes+end+1<=queue_limit;});
      if(stopping) break;
      queued_bytes+=end+1;
      pending.frames.push_back(std::move(frame));
     }
     received.erase(0,end+1);
    }
    if(received.size()>frame_limit) throw std::runtime_error("Backend frame too large");
   }
  } catch(const std::exception &e) {
   std::lock_guard<std::mutex> lock(mutex);
   pending.error=e.what();
  }
  done=true;
 }
};
