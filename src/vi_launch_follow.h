#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Creation stamps, not names, identify processes. Retain ancestors after exit so
// short launcher lifetimes and same-name/Java descendants do not lose the tree.
namespace vi_launch_follow {
struct Process {
    std::uint32_t pid=0, parent=0;
    std::uint64_t born=0, ended=0;
    std::wstring name;
    bool alive=false, window=false;
};
inline bool Name(const std::wstring& a,const std::wstring& b) {
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i) {
        const auto lower=[](wchar_t c){return c>=L'A'&&c<=L'Z'?c+(L'a'-L'A'):c;};
        if(lower(a[i])!=lower(b[i]))return false;
    }
    return true;
}
// A Toolhelp row supplies parent/name but no creation identity. A handle opened
// later can refer to a replacement PID; do not combine that birth with the old
// parent. The cutoff is sampled BEFORE the snapshot, and image identity comes
// from the same handle as the creation FILETIME.
inline bool SnapshotIdentity(std::uint32_t rowPid,const std::wstring& rowName,std::uint32_t handlePid,const std::wstring& imagePath,std::uint64_t born,std::uint64_t beforeSnapshot){
    if(!rowPid||rowPid!=handlePid||!born||!beforeSnapshot||born>beforeSnapshot||rowName.empty()||imagePath.empty())return false;
    const auto slash=imagePath.find_last_of(L"\\/");const auto image=slash==std::wstring::npos?imagePath:imagePath.substr(slash+1);
    return !image.empty()&&Name(image,rowName);
}
inline std::wstring Launcher(const std::wstring& target) {
    if(Name(target,L"SkyrimSE.exe"))return L"SkyrimSELauncher.exe";
    if(Name(target,L"Oblivion.exe"))return L"OblivionLauncher.exe";
    return {};
}
class Tree {
    std::map<std::uint64_t,Process> nodes; // insertion IDs; values carry PID + creation identity
public:
    Process root{};
    explicit Tree(Process p):root(p){Remember(p);}
    bool TrackedPid(std::uint32_t pid)const{for(const auto& n:nodes)if(n.second.pid==pid)return true;return false;}
    void Remember(const Process& p) {
        // Multiple PIDs can share a FILETIME. Find by full identity rather than
        // treating a creation timestamp alone as a unique key.
        for(auto& n:nodes)if(n.second.pid==p.pid&&n.second.born==p.born){n.second=p;return;}
        nodes.emplace(nodes.size()+1,p);
    }
    bool Contains(const Process& p)const {
        for(const auto& n:nodes)if(n.second.pid==p.pid&&n.second.born==p.born)return true;
        return false;
    }
    void Observe(const std::vector<Process>& snapshot) {
        for(const auto& p:snapshot)if(Contains(p))Remember(p);
        bool added=true;
        while(added){added=false;for(const auto& p:snapshot){
            if(!p.born||Contains(p))continue;
            for(const auto& n:nodes){const auto& parent=n.second;
                if(p.parent==parent.pid&&p.born>=parent.born&&(!parent.ended||p.born<=parent.ended)){
                    Remember(p);added=true;break;
                }
            }
        }}
    }
    // Never choose arbitrarily among two window-owning descendants. A parent
    // window is a splash when an observed descendant also owns a window.
    std::vector<Process> Candidates(const std::vector<Process>& snapshot,const std::wstring& target)const {
        std::vector<Process> result;
        for(const auto& p:snapshot)if(p.alive&&p.window&&Contains(p)&&(target.empty()||Name(p.name,target)))result.push_back(p);
        if(target.empty()&&result.size()>1){
            std::vector<Process> leaves;
            for(const auto& p:result){bool ancestor=false;for(const auto& q:result){
                Process cursor=q;
                for(size_t depth=0;depth<nodes.size()&&cursor.pid!=p.pid;++depth){
                    bool found=false;for(const auto& n:nodes)if(n.second.pid==cursor.parent&&cursor.born>=n.second.born&&(!n.second.ended||cursor.born<=n.second.ended)){cursor=n.second;found=true;break;}
                    if(!found)break;
                }
                if(q.pid!=p.pid&&cursor.pid==p.pid&&cursor.born==p.born){ancestor=true;break;}
            }if(!ancestor)leaves.push_back(p);}
            return leaves;
        }
        return result;
    }
};
} // namespace vi_launch_follow
