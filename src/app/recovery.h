// Autorecovery, modelled on RestartRecovery.dll (REA ev_d587df81...):
// periodically snapshot a dirty image to BMP + a small .info sidecar, find the
// latest snapshot, load it on startup, and clear it after a normal save.
#pragma once

#include <memory>
#include <string>

#include "../core/image.h"

std::wstring RecoveryDirectory();
bool SaveRecoverySnapshot(const pr::Image& image, const std::wstring& originalPath);
std::wstring FindLatestRecovery();
std::unique_ptr<pr::Image> LoadRecovery(const std::wstring& bmpPath);
void ClearRecovery(const std::wstring& bmpPath);
