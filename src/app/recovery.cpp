#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>

#include "../core/codec.h"
#include "recovery.h"

namespace {

void EnsureDirectory(const std::wstring& path) {
    // %LOCALAPPDATA%\PaintRebuild\AutoRecover — create both parents if needed.
    const std::size_t first = path.find(L'\\');
    if (first != std::wstring::npos) {
        const std::wstring parent = path.substr(0, first);
        CreateDirectoryW(parent.c_str(), nullptr);
    }
    const std::size_t second = path.find(L'\\', first + 1);
    if (second != std::wstring::npos)
        CreateDirectoryW(path.substr(0, second).c_str(), nullptr);
    CreateDirectoryW(path.c_str(), nullptr);
}

std::wstring Timestamp() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t buffer[64];
    swprintf(buffer, 64, L"%04d%02d%02d_%02d%02d%02d", now.wYear, now.wMonth, now.wDay,
             now.wHour, now.wMinute, now.wSecond);
    return buffer;
}

bool WriteInfo(const std::wstring& path, const std::wstring& original) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const std::wstring text = L"PaintRebuild autorecovery\nOriginal: " + original +
                              L"\nSaved: " + Timestamp() + L"\n";
    DWORD written = 0;
    const BOOL ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size() * sizeof(wchar_t)),
                              &written, nullptr);
    CloseHandle(file);
    return ok != FALSE;
}

}  // namespace

std::wstring RecoveryDirectory() {
    wchar_t localAppData[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return std::wstring();
    return std::wstring(localAppData) + L"\\PaintRebuild\\AutoRecover";
}

bool SaveRecoverySnapshot(const pr::Image& image, const std::wstring& originalPath) {
    const std::wstring directory = RecoveryDirectory();
    if (directory.empty()) return false;
    EnsureDirectory(directory);
    const std::wstring bmp = directory + L"\\AutoRecovery " + Timestamp() + L".bmp";
    if (!pr::SaveBmp(image, bmp)) return false;
    return WriteInfo(bmp + L".info", originalPath.empty() ? L"(untitled)" : originalPath);
}

std::wstring FindLatestRecovery() {
    const std::wstring directory = RecoveryDirectory();
    if (directory.empty()) return std::wstring();
    const std::wstring pattern = directory + L"\\AutoRecovery *.bmp";
    WIN32_FIND_DATAW data{};
    HANDLE handle = FindFirstFileW(pattern.c_str(), &data);
    if (handle == INVALID_HANDLE_VALUE) return std::wstring();
    FILETIME newest = data.ftLastWriteTime;
    std::wstring best = directory + L"\\" + data.cFileName;
    for (;;) {
        if (!FindNextFileW(handle, &data)) break;
        if (CompareFileTime(&data.ftLastWriteTime, &newest) > 0) {
            newest = data.ftLastWriteTime;
            best = directory + L"\\" + data.cFileName;
        }
    }
    FindClose(handle);
    return best;
}

std::unique_ptr<pr::Image> LoadRecovery(const std::wstring& bmpPath) {
    return pr::LoadBmp(bmpPath);
}

void ClearRecovery(const std::wstring& bmpPath) {
    if (bmpPath.empty()) return;
    DeleteFileW(bmpPath.c_str());
    DeleteFileW((bmpPath + L".info").c_str());
}
