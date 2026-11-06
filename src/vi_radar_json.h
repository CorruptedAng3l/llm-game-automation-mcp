#pragma once
#include "vi_ready_json.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
namespace vi_radar_json {
inline bool String(const vi_ready_json::Reader& reader,const char* key,std::string& value){auto it=reader.fields.find(key);if(it==reader.fields.end()||it->second.type!='"')return false;vi_ready_json::Reader stringReader{it->second.raw};return stringReader.String(value)&&stringReader.at==it->second.raw.size();}
inline bool U64(const std::string& value,std::uint64_t& number){if(value.empty()||value.size()>20||value[0]=='0'||!std::all_of(value.begin(),value.end(),[](char c){return c>='0'&&c<='9';}))return false;try{number=std::stoull(value);return number!=0;}catch(...){return false;}}
inline bool Number(const vi_ready_json::Reader& reader,const char* key,double& value){auto it=reader.fields.find(key);if(it==reader.fields.end()||it->second.type!='n')return false;char* end=nullptr;value=std::strtod(it->second.raw.c_str(),&end);return end&&!*end&&std::isfinite(value);}
inline bool ExactObject(const vi_ready_json::Reader& reader,const char* key,const std::string& expected){auto it=reader.fields.find(key);return it!=reader.fields.end()&&it->second.type=='{'&&it->second.raw==expected;}
}
