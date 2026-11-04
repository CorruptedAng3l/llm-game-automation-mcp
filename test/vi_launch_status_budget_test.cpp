// Pure-memory before/after parser contract. No Win32, process or desktop API.
#include "vi_ready_json.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

static unsigned passed=0,failed=0;
static void Check(bool ok,const std::string& name) {
 std::printf("%s %s\n",ok?"PASS":"FAIL",name.c_str());ok?++passed:++failed;
}
template<class Reader> static Reader StatusReader(const std::string& text,size_t limit=128) {
 // Deliberately dependent: the old three-field aggregate compiles unchanged.
 if constexpr(requires {Reader{text,0,{},size_t(128)};})return Reader{text,0,{},limit};
 else {static_cast<void>(limit);return Reader{text,0,{}};}
}
static bool DefaultParse(const std::string& text) {vi_ready_json::Reader reader{text,0,{}};return reader.Parse();}
static bool StatusParse(const std::string& text,size_t limit=128) {auto reader=StatusReader<vi_ready_json::Reader>(text,limit);return reader.Parse();}
static std::string Fields(unsigned count) {
 std::string text="{";
 for(unsigned i=0;i<count;++i){if(i)text+=',';text+='"'+std::string("f")+std::to_string(i)+"\":0";}
 return text+'}';
}
static std::string Array(unsigned count) {
 std::string text="[";for(unsigned i=0;i<count;++i){if(i)text+=',';text+="null";}return text+']';
}
static std::string Nested(unsigned count) {
 std::string text="{}";for(unsigned i=1;i<count;++i)text="{\"child\":"+text+'}';return "{\"payload\":"+text+'}';
}
int main(int argc,char** argv) {
 Check(DefaultParse(Fields(64)),"default top64 accepted");
 Check(!DefaultParse(Fields(65)),"default top65 refused");
 Check(StatusParse(Fields(81)),"config top81 accepted");
 Check(StatusParse(Fields(128)),"config top128 accepted");
 Check(!StatusParse(Fields(129)),"config top129 refused");
 Check(!StatusParse(Fields(129),129),"requested budget129 refused");
 Check(StatusParse("{\"payload\":"+Fields(64)+'}'),"config nested64 accepted");
 Check(!StatusParse("{\"payload\":"+Fields(65)+'}'),"config nested65 refused");
 Check(!StatusParse("{\"payload\":["+Fields(65)+"]}"),"config array nested65 refused");
 Check(StatusParse("{\"payload\":"+Array(256)+'}'),"array256 accepted");
 Check(!StatusParse("{\"payload\":"+Array(257)+'}'),"array257 refused");
 Check(StatusParse(Nested(16)),"depth16 accepted");
 Check(!StatusParse(Nested(17)),"depth17 refused");
 const std::string maximum="{\"x\":\""+std::string(65528,'a')+"\"}";
 Check(maximum.size()==65536&&StatusParse(maximum),"bytes65536 accepted");
 Check(!StatusParse(maximum+' '),"bytes65537 refused");
 Check(!StatusParse("{\"ok\":true,\"ok\":false}"),"duplicate direct key refused");
 Check(!StatusParse("{\"ok\":true,\"\\u006fk\":false}"),"duplicate escaped key refused");
 Check(!StatusParse("{\"payload\":{\"x\":1,\"\\u0078\":2}}"),"duplicate nested escaped key refused");
 Check(!StatusParse("{\"ok\":true} trailing"),"trailing data refused");
 Check(!StatusParse("{\"ok\":true,}"),"malformed object refused");
 Check(!StatusParse("{\"ok\":tru}"),"malformed literal refused");
 const std::string wrongBool="{\"ok\":1,\"hwnd\":\"0x123\"}";
 auto numeric=StatusReader<vi_ready_json::Reader>(wrongBool);bool ok=false;
 Check(numeric.Parse()&&!numeric.Bool("ok",ok),"numeric ok never boolean ACK");
 const std::string wrongHwnd="{\"ok\":true,\"hwnd\":123}";
 auto handle=StatusReader<vi_ready_json::Reader>(wrongHwnd);
 Check(handle.Parse()&&handle.fields.at("hwnd").type!='"',"numeric HWND never string ACK");
 const std::string ready="{\"ok\":true,\"readiness_version\":1,\"hook_ready\":true,\"window_ready\":true,\"queue_ready\":true}";
 const auto handshake=vi_ready_json::Parse(ready);
 Check(handshake.supported&&handshake.ok&&handshake.hooks&&handshake.window&&handshake.queue,"default readiness unchanged");
 Check(argc==4,"three exact captured config fixtures supplied");
 for(int i=1;i<argc;++i) {
  std::ifstream file(argv[i],std::ios::binary);
  const std::string text{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
  Check(file.good()||file.eof(),"observed config file readable "+std::to_string(i));
  Check(!text.empty()&&text.size()<=65536,"observed config file bounded "+std::to_string(i));
  Check(!DefaultParse(text),"observed config still exceeds default handshake limit "+std::to_string(i));
  auto reader=StatusReader<vi_ready_json::Reader>(text);bool accepted=false;
  const bool parsed=reader.Parse();
  const auto hwnd=reader.fields.find("hwnd");
  Check(parsed&&reader.fields.size()>64&&reader.fields.size()<=128&&reader.Bool("ok",accepted)&&accepted&&hwnd!=reader.fields.end()&&hwnd->second.type=='"',"actual config accepted within explicit status budget "+std::to_string(i));
 }
 std::printf("RESULT %u PASS %u FAIL\n",passed,failed);return failed?1:0;
}
