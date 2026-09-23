// Exercise the actual SDL child-process transport at display-frame cadence.
// Pass a disposable user directory containing a save named ProtocolTest.
#define DELUXE_CLIENT_TEST
#include "client.cpp"
#include <iostream>
#include <stdexcept>

int main(int argc,char **argv) {
 try {
  if(argc!=4) throw std::runtime_error("Pass backend, data directory and disposable user directory");
  if(!SDL_Init(0)) throw std::runtime_error(SDL_GetError());
  {
   Connection c;
   std::vector<double> polls;
   if(!c.start(argv[1],argv[2],argv[3])) throw std::runtime_error(c.menu_error);
   auto wait=[&](auto condition) {
    const Uint64 deadline=SDL_GetTicksNS()+10000000000ull;
    while(!condition()) {
     const auto polling=SDL_GetTicksNS(); c.poll();
     polls.push_back(double(SDL_GetTicksNS()-polling)/1e6);
     if(condition()) break;
     if(!c.connected || SDL_GetTicksNS()>deadline) throw std::runtime_error("Transport timeout: "+c.diagnostic);
     if(!condition()) SDL_DelayNS(16666667);
    }
   };
   wait([&]{return c.negotiated;});
   c.send("session.load",{{"save","ProtocolTest"}}); c.flush_input();
   wait([&]{return c.state.contains("terminal");});
   for(int i=0;!c.ready() && i<20;++i) {
    const auto revision=c.state.value("revision","");
    c.key("enter"); c.flush_input();
    wait([&]{return c.state.value("revision","")!=revision;});
   }
   if(!c.ready()) throw std::runtime_error("Fixture did not reach normal play");
   std::vector<double> times; polls.clear();
   for(int i=0;i<60;++i) {
    const auto revision=c.state.value("revision","");
    const auto start=SDL_GetTicksNS();
    c.key(18); c.flush_input(); // Redraw: same large dungeon snapshot, no turns.
    wait([&]{return c.state.value("revision","")!=revision && c.ready();});
    times.push_back(double(SDL_GetTicksNS()-start)/1e6);
   }
   std::sort(times.begin(),times.end());
   std::cout << "SDL transport, 60 Hz polling: median " << times[times.size()/2]
             << " ms, p95 " << times[times.size()*95/100] << " ms, max " << times.back() << " ms, depth " << c.state["player"]["depth"] << std::endl;
   std::sort(polls.begin(),polls.end());
   std::cout << "UI-thread receive: p95 " << polls[polls.size()*95/100] << " ms, max " << polls.back() << " ms" << std::endl;
   if(times[times.size()/2]>100 || times[times.size()*95/100]>150)
    throw std::runtime_error("Frame-cadence pipe stall regressed");
   // Exercise actual movement too. Choose a clear adjacent floor from the
   // disposable fixture, alternating back to its initial staircase.
   const int x=c.state["player"]["x"],y=c.state["player"]["y"];
   int dx=0,dy=0;
   auto catalog_id=c.send("catalog.get"); c.flush_input();
   wait([&]{return !c.requests.count(catalog_id);});
   for(const auto &step:std::array<std::array<int,2>,4>{{{{1,0}},{{-1,0}},{{0,1}},{{0,-1}}}}) {
    const int xx=x+step[0],yy=y+step[1];
    if(xx<0 || yy<0 || yy>=int(c.state["map"]["actual"].size()) || xx>=int(c.state["map"]["actual"][yy].size())) continue;
    auto feature=c.state["map"]["actual"][yy][xx];
    bool floor=false,occupied=false;
    for(const auto &f:c.catalog["features"]) if(f["id"]==feature && f.value("name","")=="open floor") floor=true;
    for(const auto &m:c.state["monsters"]) if(m.value("x",-1)==xx && m.value("y",-1)==yy) occupied=true;
    if(floor && !occupied) { dx=step[0]; dy=step[1]; break; }
   }
   if(!dx && !dy) throw std::runtime_error("No adjacent floor for movement benchmark");
   times.clear();
   for(int i=0;i<12;++i) {
    const int xx=x+(i%2?0:dx),yy=y+(i%2?0:dy);
    const auto revision=c.state.value("revision","");
    const auto start=SDL_GetTicksNS();
    c.send("dungeon.click",{{"context",c.state["context"]},{"x",xx},{"y",yy}}); c.busy=true; c.flush_input();
    wait([&]{return c.state.value("revision","")!=revision;});
    for(int n=0;!c.ready() && n<20;++n) {
     if(!c.prompt.empty()) throw std::runtime_error("Unexpected movement prompt");
     const auto before=c.state.value("revision","");
     c.key("enter"); c.flush_input(); wait([&]{return c.state.value("revision","")!=before;});
    }
    if(!c.ready() || c.state["player"]["x"]!=xx || c.state["player"]["y"]!=yy) throw std::runtime_error("Fixture movement was interrupted");
    times.push_back(double(SDL_GetTicksNS()-start)/1e6);
   }
   std::sort(times.begin(),times.end());
   std::cout<<"Movement input-to-state: median "<<times[times.size()/2]<<" ms, p95 "<<times[times.size()*95/100]<<" ms"<<std::endl;
   if(times[times.size()/2]>100 || times[times.size()*95/100]>150)
    throw std::runtime_error("Movement response latency regressed");
   c.save(true); c.flush_input();
   wait([&]{return c.closed;});
   c.close_process(); c=Connection{};
   if(!c.start(argv[1],argv[2],argv[3])) throw std::runtime_error(c.menu_error);
   wait([&]{return c.negotiated;});
   // Both readers may be blocked waiting for a quiet backend. Closing must
   // cancel them before destroying pipe handles, without waiting for output.
   const auto closing=SDL_GetTicksNS();
   c.close_process();
   if(SDL_GetTicksNS()-closing>1000000000ull) throw std::runtime_error("Reader shutdown stalled");
  }
  SDL_Quit(); return 0;
 } catch(const std::exception &e) { std::cerr<<e.what()<<std::endl; return 1; }
}
