#pragma once
namespace vi_launch_splash {
inline bool Pending(bool ownedPopup,bool sameProcessOwner,bool borderless,bool dialogClass,bool emptyText){return ownedPopup&&sameProcessOwner&&borderless&&!dialogClass&&emptyText;}
}
