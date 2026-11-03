// Capture destinations are caller supplied; create their parents, preserving errors such as
// a file occupying a directory component or an ACL denial instead of hiding them in a WIC HRESULT.
#pragma once
#include <filesystem>
#include <string>

namespace vi_paths {
inline bool CaptureParent(const std::wstring& path, std::string& error) {
    const auto parent = std::filesystem::path(path).parent_path();
    if (parent.empty()) return true;
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (!ec && std::filesystem::is_directory(parent, ec)) return true;
    error = "capture parent directory could not be created (" + std::to_string(ec.value()) + "): " + ec.message();
    return false;
}
}
