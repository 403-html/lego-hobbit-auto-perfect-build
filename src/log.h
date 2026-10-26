// Auto Perfect Build - tiny file logger.
#pragma once
#include <windows.h>

namespace apb {

void LogOpen(const wchar_t* path, bool enabled);
void Log(const char* fmt, ...);
void LogClose();

}  // namespace apb
