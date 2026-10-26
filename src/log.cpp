#include "log.h"

#include <stdarg.h>
#include <stdio.h>

namespace apb {

static HANDLE g_file = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_cs;
static bool g_init = false;

void LogOpen(const wchar_t* path, bool enabled) {
  if (!g_init) {
    InitializeCriticalSection(&g_cs);
    g_init = true;
  }
  if (!enabled) return;
  g_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
}

void Log(const char* fmt, ...) {
  if (g_file == INVALID_HANDLE_VALUE) return;
  char buf[1024];
  SYSTEMTIME st;
  GetLocalTime(&st);
  int n = _snprintf(buf, sizeof(buf), "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute,
                    st.wSecond, st.wMilliseconds);
  va_list ap;
  va_start(ap, fmt);
  int m = _vsnprintf(buf + n, sizeof(buf) - n - 3, fmt, ap);
  va_end(ap);
  if (m < 0) m = (int)(sizeof(buf) - n - 3);
  n += m;
  buf[n++] = '\r';
  buf[n++] = '\n';
  EnterCriticalSection(&g_cs);
  DWORD w;
  WriteFile(g_file, buf, (DWORD)n, &w, nullptr);
  FlushFileBuffers(g_file);
  LeaveCriticalSection(&g_cs);
}

void LogClose() {
  if (g_file != INVALID_HANDLE_VALUE) CloseHandle(g_file);
  g_file = INVALID_HANDLE_VALUE;
}

}  // namespace apb
