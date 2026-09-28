#include "storage/storage_paths.hpp"

#include <Windows.h>
#include <ShlObj.h>

#include "utils/unicode.hpp"

namespace ir {
namespace {

std::string wide_to_utf8(const std::wstring& w) {
    return utf16_to_utf8(
        std::u16string(reinterpret_cast<const char16_t*>(w.data()), w.size()));
}

}  // namespace

std::string default_data_dir() {
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE,
                                    nullptr, &local))) {
        if (local != nullptr) CoTaskMemFree(local);
        return {};
    }
    std::wstring dir(local);
    CoTaskMemFree(local);
    dir += L"\\InputRecorder";

    // Create the directory if it does not exist (idempotent).
    if (!CreateDirectoryW(dir.c_str(), nullptr)) {
        if (GetLastError() != ERROR_ALREADY_EXISTS) {
            return {};
        }
    }
    return wide_to_utf8(dir);
}

std::string default_database_path() {
    const std::string dir = default_data_dir();
    if (dir.empty()) return {};
    return dir + "\\InputRecorder.db";
}

}  // namespace ir
