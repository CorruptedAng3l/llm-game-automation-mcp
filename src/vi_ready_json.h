// Strict bounded JSON validation for asynchronous input and injector handshakes.
// Required fields must be direct properties with exact JSON types.
#pragma once
#include <string>
#include <vector>
#include <map>
namespace vi_ready_json {
struct Value { char type=0;std::string raw; };
struct Reader {
 const std::string& text;size_t at=0;std::map<std::string,Value> fields;
 // Full config status may use128 top-level fields; strict handshakes retain64.
 size_t maxTopLevelFields=64;
 // Diagnostics only: retain the first duplicate under the existing key decoder.
 bool duplicateKeyFound=false;std::string duplicateKey;
 void Space(){while(at<text.size()&&(text[at]==' '||text[at]=='\t'||text[at]=='\r'||text[at]=='\n'))++at;}
 static int Hex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
 bool String(std::string& value) {
  if(at>=text.size()||text[at++]!='"')return false;
  while(at<text.size()) {
   unsigned char c=(unsigned char)text[at++];if(c=='"')return true;if(c<0x20)return false;
   if(c!='\\'){value+=(char)c;continue;}
   if(at>=text.size())return false;c=(unsigned char)text[at++];
   if(c=='"'||c=='\\'||c=='/'){value+=(char)c;continue;}
   if(c=='b'||c=='f'||c=='n'||c=='r'||c=='t'){value+=(char)(c=='b'?8:c=='f'?12:c=='n'?10:c=='r'?13:9);continue;}
   if(c!='u'||at+4>text.size())return false;unsigned code=0;
   for(int i=0;i<4;++i){int digit=Hex(text[at++]);if(digit<0)return false;code=code*16+(unsigned)digit;}
   value+=code<128?(char)code:'?';
  }
  return false;
 }
 bool Number() {
  if(at<text.size()&&text[at]=='-')++at;if(at>=text.size())return false;
  if(text[at]=='0')++at;else{if(text[at]<'1'||text[at]>'9')return false;while(at<text.size()&&text[at]>='0'&&text[at]<='9')++at;}
  if(at<text.size()&&text[at]=='.'){size_t start=++at;while(at<text.size()&&text[at]>='0'&&text[at]<='9')++at;if(at==start)return false;}
  if(at<text.size()&&(text[at]=='e'||text[at]=='E')){++at;if(at<text.size()&&(text[at]=='+'||text[at]=='-'))++at;size_t start=at;while(at<text.size()&&text[at]>='0'&&text[at]<='9')++at;if(at==start)return false;}
  return true;
 }
 bool Object(unsigned depth,bool top=false) {
  if(depth>16||at>=text.size()||text[at++]!='{')return false;Space();if(at<text.size()&&text[at]=='}'){++at;return true;}
  std::vector<std::string> names;
  for(;;){Space();std::string key;if(!String(key))return false;for(const auto& name:names)if(name==key){duplicateKeyFound=true;duplicateKey=key;return false;}if(names.size()>=(top?maxTopLevelFields:size_t(64)))return false;names.push_back(key);Space();if(at>=text.size()||text[at++]!=':')return false;
   Space();const size_t start=at;char kind=0;if(!Any(depth+1,kind))return false;if(top)fields.emplace(key,Value{kind,text.substr(start,at-start)});
   Space();if(at>=text.size())return false;if(text[at]=='}'){++at;return true;}if(text[at++]!=',')return false;
  }
 }
 bool Any(unsigned depth,char& type) {
  if(depth>16||at>=text.size())return false;char c=text[at];type=c;
  if(c=='{')return Object(depth);
  if(c=='['){++at;Space();if(at<text.size()&&text[at]==']'){++at;return true;}unsigned count=0;for(;;){char kind=0;Space();if(++count>256||!Any(depth+1,kind))return false;Space();if(at>=text.size())return false;if(text[at]==']'){++at;return true;}if(text[at++]!=',')return false;}}
  if(c=='"'){std::string ignored;return String(ignored);}
  for(const char* literal:{"true","false","null"}){const std::string word=literal;if(text.compare(at,word.size(),word)==0){at+=word.size();type=c=='n'?'z':'b';return true;}}
  type='n';return Number();
 }
 bool Parse(){duplicateKeyFound=false;duplicateKey.clear();if(!maxTopLevelFields||maxTopLevelFields>128||text.empty()||text.size()>65536)return false;Space();if(!Object(0,true))return false;Space();return at==text.size();}
 bool Bool(const char* name,bool& value)const {auto field=fields.find(name);if(field==fields.end()||field->second.type!='b')return false;value=field->second.raw=="true";return true;}
};
struct Reply { bool supported=false,ok=false,hooks=false,window=false,queue=false; };
inline Reply Parse(const std::string& text) {
 Reader reader{text,0,{}};Reply result;if(!reader.Parse())return result;
 auto version=reader.fields.find("readiness_version");if(version==reader.fields.end()||version->second.type!='n'||version->second.raw!="1")return result;
 result.supported=reader.Bool("ok",result.ok)&&reader.Bool("hook_ready",result.hooks)&&reader.Bool("window_ready",result.window)&&reader.Bool("queue_ready",result.queue);return result;
}
} // namespace vi_ready_json
