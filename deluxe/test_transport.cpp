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
   if(!c.start(argv[1],argv[2],argv[3])) throw std::runtime_error(c.menu_error);
   auto wait=[&](auto condition) {
    const Uint64 deadline=SDL_GetTicksNS()+10000000000ull;
    while(!condition()) {
     c.poll();
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
   std::vector<double> times;
   for(int i=0;i<20;++i) {
    const auto revision=c.state.value("revision","");
    const auto start=SDL_GetTicksNS();
    c.key(18); c.flush_input(); // Redraw: same large dungeon snapshot, no turns.
    wait([&]{return c.state.value("revision","")!=revision && c.ready();});
    times.push_back(double(SDL_GetTicksNS()-start)/1e6);
   }
   std::sort(times.begin(),times.end());
   std::cout << "SDL transport, 60 Hz polling: median " << times[times.size()/2]
             << " ms, max " << times.back() << " ms, depth " << c.state["player"]["depth"] << std::endl;
   if(times[times.size()/2]>150) throw std::runtime_error("Frame-cadence pipe stall regressed");
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
