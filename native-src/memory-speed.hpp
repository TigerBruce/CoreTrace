#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <algorithm>
// SMBIOS Type 17 Configured Memory Speed (0x20), in MT/s, not rated SPD speed.
// https://www.dmtf.org/sites/default/files/standards/documents/DSP0134_3.8.0.pdf
inline std::wstring configuredMemorySpeed(const unsigned char* data, size_t size) {
  std::vector<unsigned long> speeds;
  bool unknown=false;
  for(size_t at=0;at+4<=size;){
    auto p=data+at; size_t len=p[1];
    if(len<4||len>size-at)break;
    if(p[0]==127)break;
    if(p[0]==17&&len>=14&&(p[12]||p[13])){
      unsigned long speed=len>=34?(unsigned(p[32])|(unsigned(p[33])<<8)):0;
      if(speed==65535){speed=0;if(len>=92)for(int i=0;i<4;i++)speed|=static_cast<unsigned long>(p[88+i])<<(8*i);}
      if(speed&&speed!=0xffffffff) speeds.push_back(speed); else unknown=true;
    }
    size_t end=at+len;
    while(end+1<size&&(data[end]!=0||data[end+1]!=0))end++;
    if(end+1>=size)break;
    at=end+2;
  }
  if(speeds.empty())return L"速率 —";
  auto [lo,hi]=std::minmax_element(speeds.begin(),speeds.end());
  return std::to_wstring(*lo)+(*lo!=*hi?L"–"+std::to_wstring(*hi):L"")+L" MT/s"+(unknown?L" *":L"");
}
inline std::wstring readMemorySpeed(){
  constexpr DWORD provider=0x52534d42; // 'RSMB' provider signature.
  UINT bytes=GetSystemFirmwareTable(provider,0,nullptr,0);
  if(bytes<8||bytes>16*1024*1024)return L"速率 —";
  std::vector<unsigned char> raw(bytes);
  if(GetSystemFirmwareTable(provider,0,raw.data(),bytes)!=bytes)return L"速率 —";
  unsigned long len=0;for(int i=0;i<4;i++)len|=static_cast<unsigned long>(raw[4+i])<<(8*i);
  if(len>bytes-8)return L"速率 —";
  return configuredMemorySpeed(raw.data()+8,len);
}
