#include "TimedStream.h"
#include <vector>
#include <stdexcept>
#include <stdio.h>
#include <fstream>

using namespace nd6timed;
void require(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
struct Event { uint64_t sample; std::vector<uint8_t> bytes; };
struct Sink {
  Stream* stream = nullptr;
  std::vector<Event> events;
  unsigned resets = 0;
  static void emit(void* p, const uint8_t* b, unsigned n) {
    auto& s = *static_cast<Sink*>(p);
    s.events.push_back({s.stream->samples, std::vector<uint8_t>(b, b+n)});
  }
  static void reset(void* p) { ++static_cast<Sink*>(p)->resets; }
};
std::vector<uint8_t> packet(uint8_t type, uint32_t session, const std::vector<uint8_t>& data = {}) {
  std::vector<uint8_t> b(data.size() + 13);
  b[0]=0xf9; b[1]='N'; b[2]='D'; b[3]='6'; b[4]=type;
  writeLE(b.data()+5, session, 4); writeLE(b.data()+9, data.size(), 2);
  for (unsigned i=0;i<data.size();++i) b[11+i]=data[i];
  writeLE(b.data()+11+data.size(), crc16(b.data()+4, data.size()+7), 2);
  return b;
}
void send(Stream& s, uint8_t type, uint32_t id, const std::vector<uint8_t>& data = {}, uint64_t now = 0) {
  for (auto b : packet(type, id, data)) require(s.input(b, now), "frame byte mistaken for legacy");
}
void basicTests() {
  Sink sink; Stream s(Sink::emit, Sink::reset, &sink); sink.stream=&s;
  require(!s.input(0x52,0), "legacy fallback");
  require(s.input(0xf8,0) && s.capabilityPending, "capability");
  uint8_t reply[32]; s.status(reply,true,0);
  require(readLE(reply+30,2)==crc16(reply+4,26) && reply[4]=='C', "status CRC");
  require(reply[29]==1,"PCM timing capability");
  send(s,1,17);
  send(s,2,16,{0x50,0x99}); // stale session is ignored
  require(s.consumed==0 && s.state==Stream::Preload,"stale session");
  send(s,2,17,{0x52,0x2b,0x80, 0x61,0,0, 0x81,0x70, 0x70, 0x8f,0x71, 0x62, 0x63, 0x66});
  s.tick(0); require(sink.events.size()==1 && s.consumed==6,"untimed init parks before positive PCM wait");
  send(s,3,17); s.tick(0);
  require(sink.events.size()==2 && s.samples==1,"first DAC immediate");
  s.tick(21); require(s.samples==1,"wait one sample not early");
  s.tick(22); require(s.samples==2,"short wait exact");
  s.tick(45); require(sink.events.size()==3 && s.samples==17,"implicit DAC wait");
  s.tick(Stream::sampleUS(17)); s.tick(Stream::sampleUS(752));
  s.tick(Stream::sampleUS(1634));
  require(s.state==Stream::Done && s.samples==1634,"all wait encodings/end");
  require(Stream::sampleUS(44100)==1000000 && Stream::sampleUS(44100ULL*3600)==3600000000ULL,"fractional/long clock");
  send(s,1,18); send(s,2,18,{0x61,0xff,0xff,0x66}); send(s,3,18); s.tick(0);
  require(s.remainingUS(100)==1485954,"long wait deadline");
  send(s,4,18,{},100); require(s.state==Stream::Idle,"stop bypasses long wait");
  send(s,1,19); send(s,2,19,{0x81,0x70}); send(s,3,19); s.tick(0); s.tick(22);
  require(s.error==Stream::Underrun,"underrun reported, no catch-up burst");
  send(s,1,20); auto bad=packet(2,20,{0x50,0xaa}); bad[12]^=1;
  for(auto b:bad) s.input(b,0);
  require(s.error==Stream::BadFrame,"CRC corruption");
  send(s,1,21); send(s,2,21,{0x52,0x10}); require(s.error==Stream::BadCommand,"truncated command");
  send(s,1,22); send(s,2,22,{0x66,0x50,0}); require(s.error==Stream::BadCommand,"data after end");
  send(s,1,23);
  std::vector<uint8_t> full(512,0x70);
  for(unsigned i=0;i<Stream::Capacity/512;++i) send(s,2,23,full);
  send(s,2,23,{0x70}); require(s.error==Stream::Overflow,"capacity limit");
  send(s,1,24); s.tick(3000001); require(s.error==Stream::HostTimeout,"lost host timeout");
  send(s,1,25); s.input(0xf9,0); s.input('N',500001); require(s.error==Stream::BadFrame,"partial frame timeout");
  send(s,1,26); send(s,2,26,{0x82,0x80,0x82,0x81,0x66}); send(s,3,26);
  s.tick(0); s.tick(Stream::sampleUS(2)+57);
  require(s.maxPCMLateUS==57,"PCM deadline lateness");
  s.status(reply,false,Stream::sampleUS(2)+57);
  require(reply[29]==5 && readLE(reply+30,2)==crc16(reply+4,26),"PCM status timing/CRC");
  puts("PASS: protocol, CRC, sessions, waits, preload, stop, underrun, capacity, disconnect");
}

// Fixture contains uint64 expected total, uint32 stream size, raw timed commands.
// Execute through the actual framed ring decoder with 512-byte fragmented input.
void replay(const char* name) {
  std::ifstream f(name,std::ios::binary);
  std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)),{});
  require(file.size()>=12,"fixture header");
  const uint64_t total=readLE(file.data(),8);
  require(readLE(file.data()+8,4)==file.size()-12,"fixture length");
  std::vector<Event> expected;
  uint64_t at=0;
  for(size_t i=12;i<file.size();) {
    const auto* b=file.data()+i; const auto n=commandSize(*b);
    require(n && i+n<=file.size(),"fixture command");
    if (*b==0x30 || *b==0x50 || *b==0x52 || *b==0x53 || *b==0x55 || (*b>=0x80 && *b<=0x8f))
      expected.push_back({at,std::vector<uint8_t>(b,b+n)});
    at+=waitSamples(b); i+=n;
  }
  require(at==total,"fixture total");
  Sink sink; Stream s(Sink::emit,Sink::reset,&sink); sink.stream=&s;
  send(s,1,1); uint64_t now=0,sent=0, submittedSamples=0; size_t pos=12;
  bool started=false;
  while(s.state!=Stream::Done) {
    if (pos<file.size() && sent-s.consumed<Stream::Capacity-512 &&
        (!started || submittedSamples<=s.played(now)+4410)) {
      std::vector<uint8_t> data;
      while(pos<file.size()) {
        const auto n=commandSize(file[pos]);
        if(data.size()+n>512) break;
        submittedSamples+=waitSamples(file.data()+pos);
        data.insert(data.end(),file.begin()+pos,file.begin()+pos+n); pos+=n;
        if (submittedSamples>s.played(now)+4410) break;
      }
      send(s,2,1,data,now); sent+=data.size();
    }
    if (!started && (submittedSamples>=2205 || pos==file.size() || sent-s.consumed>Stream::Capacity-512)) {
      send(s,3,1,{},now); started=true;
    }
    s.tick(now);
    require(s.state!=Stream::Error,"replay stream fault");
    if (started) {
      // Advance to the next deadline; simulate periodic status/keepalive.
      const auto remaining=s.remainingUS(now);
      if(remaining) now+=remaining>1000?1000:remaining;
      send(s,5,1,{},now);
    }
  }
  require(s.samples==total && sink.events.size()==expected.size(),"replay event count/total");
  for(size_t i=0;i<expected.size();++i)
    require(sink.events[i].sample==expected[i].sample && sink.events[i].bytes==expected[i].bytes,"replay register/order/time mismatch");
  printf("PASS: %s: %zu register writes; %llu samples\n",name,expected.size(),(unsigned long long)total);
}
int main(int argc,char** argv) {
  try { basicTests(); for(int i=1;i<argc;++i) replay(argv[i]); }
  catch(const std::exception& e) { fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
