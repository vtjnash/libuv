/* Copyright libuv contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/*
 * appcontainer.c - Run a program inside a Windows AppContainer.
 *
 * Usage: appcontainer.exe program.exe [args...]
 *
 * Creates an AppContainer profile and stages the program (along with
 * every file in the program's directory, and test/fixtures if present
 * under the current directory) into the profile's own package folder,
 * which Windows pre-grants to the AppContainer SID.  This avoids
 * modifying the DACL of any pre-existing file or directory: the only
 * global state touched is the NUL device DACL and the firewall
 * loopback exemption list, both of which are revoked on exit
 * (including on Ctrl+C, via a console control handler).  The staged
 * copies are removed together with the package folder by
 * DeleteAppContainerProfile.
 *
 * The child runs with the package folder as its working directory and
 * with TMP/TEMP pointing at a subdirectory of it, and its exit code is
 * returned.
 */

#include <windows.h>
#include <userenv.h>
#include <sddl.h>
#include <aclapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Well-known capability SID for network access.
 * See https://devblogs.microsoft.com/oldnewthing/20220503-00/?p=106557
 */
#define INTERNET_CLIENT_SERVER_SID "S-1-15-3-2"

/* Must be wide strings: CreateAppContainerProfile has no A variant. */
static const wchar_t profile_name[] = L"libuv-test-appcontainer";
static const wchar_t profile_display[] = L"libuv test";
static const wchar_t profile_desc[] = L"AppContainer for libuv tests";

/* Grant or revoke the AppContainer SID's access to the NUL device.
 * uv_spawn needs to open NUL for ignored stdio handles.
 * NUL is a device object, so we must use handle-based
 * GetSecurityInfo/SetSecurityInfo (the named variants silently
 * fail on devices). */
static void modify_nul_access(PSID sid, int grant) {
  HANDLE h;
  PACL old_acl = NULL;
  PACL new_acl = NULL;
  PSECURITY_DESCRIPTOR sd = NULL;
  EXPLICIT_ACCESSA ea;
  DWORD err;

  /* Open NUL with permission to read and modify the DACL. */
  h = CreateFileA("\\\\.\\NUL",
                  READ_CONTROL | WRITE_DAC,
                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                  NULL,
                  OPEN_EXISTING,
                  0,
                  NULL);
  if (h == INVALID_HANDLE_VALUE) {
    if (grant)
      fprintf(stderr, "appcontainer: warning: open NUL: %lu\n", GetLastError());
    return;
  }

  err = GetSecurityInfo(h,
                        SE_FILE_OBJECT,
                        DACL_SECURITY_INFORMATION,
                        NULL, NULL, &old_acl, NULL, &sd);
  if (err != ERROR_SUCCESS) {
    if (grant)
      fprintf(stderr, "appcontainer: warning: GetSecurityInfo NUL: %lu\n", err);
    CloseHandle(h);
    return;
  }

  memset(&ea, 0, sizeof(ea));
  ea.grfAccessPermissions = grant ? (GENERIC_READ | GENERIC_WRITE) : 0;
  ea.grfAccessMode = grant ? SET_ACCESS : REVOKE_ACCESS;
  ea.grfInheritance = NO_INHERITANCE;
  ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
  ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
  ea.Trustee.ptstrName = (LPSTR)sid;

  err = SetEntriesInAclA(1, &ea, old_acl, &new_acl);
  if (err != ERROR_SUCCESS) {
    if (grant)
      fprintf(stderr, "appcontainer: warning: SetEntriesInAcl NUL: %lu\n", err);
    LocalFree(sd);
    CloseHandle(h);
    return;
  }

  err = SetSecurityInfo(h,
                        SE_FILE_OBJECT,
                        DACL_SECURITY_INFORMATION,
                        NULL, NULL, new_acl, NULL);
  if (grant) {
    if (err != ERROR_SUCCESS)
      fprintf(stderr, "appcontainer: warning: SetSecurityInfo NUL: %lu\n", err);
    else
      fprintf(stderr, "appcontainer: NUL device access granted\n");
  }

  LocalFree(new_acl);
  LocalFree(sd);
  CloseHandle(h);
}

/* Add or remove the AppContainer SID from the loopback exemption list
 * so that TCP/UDP tests can connect to localhost. */
static void modify_loopback_exemption(PSID sid, int grant) {
  /* Load NetworkIsolationGetAppContainerConfig and NetworkIsolationSetAppContainerConfig
   * directly from FirewallAPI.dll, since netfw.h does not always contain their declarations.
   * See https://learn.microsoft.com/en-us/windows/win32/api/netfw/nf-netfw-networkisolationsetappcontainerconfig */
  typedef DWORD (WINAPI *fnGetConfig)(DWORD*, PSID_AND_ATTRIBUTES*);
  typedef DWORD (WINAPI *fnSetConfig)(DWORD, PSID_AND_ATTRIBUTES);
  HMODULE hFirewall;
  fnGetConfig getConfig;
  fnSetConfig setConfig;
  DWORD numSids = 0;
  PSID_AND_ATTRIBUTES oldSids = NULL;
  PSID_AND_ATTRIBUTES newSids = NULL;
  DWORD newCount = 0;
  DWORD err;
  DWORD i;

  hFirewall = LoadLibraryA("FirewallAPI.dll");
  if (!hFirewall) {
    if (grant)
      fprintf(stderr, "appcontainer: warning: LoadLibrary(FirewallAPI.dll) "
              "failed: %lu\n", GetLastError());
    return;
  }

  getConfig = (fnGetConfig)GetProcAddress(hFirewall, "NetworkIsolationGetAppContainerConfig");
  setConfig = (fnSetConfig)GetProcAddress(hFirewall, "NetworkIsolationSetAppContainerConfig");
  if (!getConfig || !setConfig) {
    if (grant)
      fprintf(stderr, "appcontainer: warning: NetworkIsolation API not found\n");
    FreeLibrary(hFirewall);
    return;
  }

  /* Get current exemption list.  (oldSids is deliberately leaked: the
   * API does not document which allocator owns it and this process is
   * short-lived.) */
  err = getConfig(&numSids, &oldSids);
  if (err != ERROR_SUCCESS) {
    if (grant)
      fprintf(stderr, "appcontainer: warning: NetworkIsolationGetAppContainerConfig: %lu\n", err);
    FreeLibrary(hFirewall);
    return;
  }

  /* Build new list: copy existing entries (excluding our SID to avoid
   * duplicates), then append our SID if granting. */
  newSids = (PSID_AND_ATTRIBUTES)malloc(
    (numSids + 1) * sizeof(SID_AND_ATTRIBUTES));
  if (!newSids) {
    FreeLibrary(hFirewall);
    return;
  }

  for (i = 0; i < numSids; i++) {
    if (!EqualSid(oldSids[i].Sid, sid))
      newSids[newCount++] = oldSids[i];
  }
  if (grant) {
    newSids[newCount].Sid = sid;
    newSids[newCount].Attributes = SE_GROUP_ENABLED;
    newCount++;
  }

  err = setConfig(newCount, newSids);
  if (grant) {
    if (err != ERROR_SUCCESS)
      fprintf(stderr, "appcontainer: warning: NetworkIsolationSetAppContainerConfig: %lu\n", err);
    else
      fprintf(stderr, "appcontainer: loopback exemption added\n");
  }

  free(newSids);
  FreeLibrary(hFirewall);
}

/* Revoke the global grants (NUL device DACL, loopback exemption) and
 * delete the profile.  Idempotent; also invoked from the console
 * control handler so that Ctrl+C does not leave stale machine-wide
 * state behind.  DeleteAppContainerProfile removes the package folder,
 * and with it all the staged files. */
static PSID cleanup_sid;
static volatile LONG cleanup_done;

static void cleanup(void) {
  if (InterlockedExchange(&cleanup_done, 1))
    return;
  modify_nul_access(cleanup_sid, 0);
  modify_loopback_exemption(cleanup_sid, 0);
  DeleteAppContainerProfile(profile_name);
}

static BOOL WINAPI ctrl_handler(DWORD type) {
  (void)type;
  cleanup();
  return FALSE;  /* Continue with default handling (terminate). */
}

/* Copy every regular file in the directory src into the directory dst
 * (non-recursive).  Returns 0 on success. */
static int stage_dir_flat(const char* src, const char* dst) {
  char pattern[MAX_PATH];
  char from[MAX_PATH];
  char to[MAX_PATH];
  WIN32_FIND_DATAA fd;
  HANDLE find;

  snprintf(pattern, sizeof(pattern), "%s\\*", src);
  find = FindFirstFileA(pattern, &fd);
  if (find == INVALID_HANDLE_VALUE) {
    fprintf(stderr, "appcontainer: FindFirstFile %s: %lu\n",
            pattern, GetLastError());
    return -1;
  }
  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
      continue;
    snprintf(from, sizeof(from), "%s\\%s", src, fd.cFileName);
    snprintf(to, sizeof(to), "%s\\%s", dst, fd.cFileName);
    if (!CopyFileA(from, to, FALSE)) {
      fprintf(stderr, "appcontainer: copy %s -> %s: %lu\n",
              from, to, GetLastError());
      FindClose(find);
      return -1;
    }
  } while (FindNextFileA(find, &fd));
  FindClose(find);
  return 0;
}

/* Recursively copy the directory tree src to dst, creating dst.
 * Returns 0 on success. */
static int stage_tree(const char* src, const char* dst) {
  char pattern[MAX_PATH];
  char from[MAX_PATH];
  char to[MAX_PATH];
  WIN32_FIND_DATAA fd;
  HANDLE find;
  int err = 0;

  if (!CreateDirectoryA(dst, NULL) &&
      GetLastError() != ERROR_ALREADY_EXISTS) {
    fprintf(stderr, "appcontainer: CreateDirectory %s: %lu\n",
            dst, GetLastError());
    return -1;
  }

  snprintf(pattern, sizeof(pattern), "%s\\*", src);
  find = FindFirstFileA(pattern, &fd);
  if (find == INVALID_HANDLE_VALUE) {
    fprintf(stderr, "appcontainer: FindFirstFile %s: %lu\n",
            pattern, GetLastError());
    return -1;
  }
  do {
    if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
      continue;
    snprintf(from, sizeof(from), "%s\\%s", src, fd.cFileName);
    snprintf(to, sizeof(to), "%s\\%s", dst, fd.cFileName);
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      err = stage_tree(from, to);
    } else if (!CopyFileA(from, to, FALSE)) {
      fprintf(stderr, "appcontainer: copy %s -> %s: %lu\n",
              from, to, GetLastError());
      err = -1;
    }
  } while (err == 0 && FindNextFileA(find, &fd));
  FindClose(find);
  return err;
}

/* Append s, surrounded by double quotes and preceded by a space when
 * not first, to buf, tracking the write position in *pos.  Note:
 * embedded quotes in s are not escaped; the test suite's arguments
 * never contain them.  Returns 0 on success, -1 if s does not fit. */
static int append_quoted(char* buf, size_t size, size_t* pos, const char* s) {
  size_t len = strlen(s);

  /* Two quotes, a possible separating space and the NUL terminator. */
  if (*pos + len + 4 > size)
    return -1;
  if (*pos > 0)
    buf[(*pos)++] = ' ';
  buf[(*pos)++] = '"';
  memcpy(&buf[*pos], s, len);
  *pos += len;
  buf[(*pos)++] = '"';
  buf[*pos] = '\0';
  return 0;
}

/* Launch a child process inside the AppContainer and return its
 * exit code. */
static int run_child(const char* abs_exe, const char* cmdline,
                     const char* cwd, SECURITY_CAPABILITIES* sc) {
  STARTUPINFOEXA si;
  PROCESS_INFORMATION pi;
  SIZE_T attr_size;
  DWORD exit_code = 1;

  memset(&si, 0, sizeof(si));
  memset(&pi, 0, sizeof(pi));

  /* Allocate the proc thread attribute list. */
  attr_size = 0;
  InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
  si.StartupInfo.cb = sizeof(si);
  si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(attr_size);
  if (!si.lpAttributeList) {
    fprintf(stderr, "malloc failed\n");
    return 1;
  }
  if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0,
                                          &attr_size)) {
    fprintf(stderr, "InitializeProcThreadAttributeList failed: %lu\n",
            GetLastError());
    free(si.lpAttributeList);
    return 1;
  }

  if (!UpdateProcThreadAttribute(si.lpAttributeList,
                                 0,
                                 PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
                                 sc,
                                 sizeof(*sc),
                                 NULL,
                                 NULL)) {
    fprintf(stderr, "UpdateProcThreadAttribute failed: %lu\n", GetLastError());
    DeleteProcThreadAttributeList(si.lpAttributeList);
    free(si.lpAttributeList);
    return 1;
  }

  /* Inherit handles so stdout/stderr flow through. */
  si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  si.StartupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.StartupInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  si.StartupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);

  if (!CreateProcessA(abs_exe,
                      (LPSTR)cmdline,
                      NULL,
                      NULL,
                      TRUE,
                      EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW,
                      NULL,
                      cwd,
                      &si.StartupInfo,
                      &pi)) {
    fprintf(stderr, "CreateProcessA failed: %lu\n", GetLastError());
    DeleteProcThreadAttributeList(si.lpAttributeList);
    free(si.lpAttributeList);
    return 1;
  }

  WaitForSingleObject(pi.hProcess, INFINITE);
  GetExitCodeProcess(pi.hProcess, &exit_code);

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  DeleteProcThreadAttributeList(si.lpAttributeList);
  free(si.lpAttributeList);
  return (int)exit_code;
}

int main(int argc, char* argv[]) {
  PSID sid = NULL;
  HRESULT hr;
  char cwd[MAX_PATH];
  char abs_exe[MAX_PATH];
  char exe_dir[MAX_PATH];
  char folder[MAX_PATH];
  char staged_exe[MAX_PATH];
  char path[MAX_PATH];
  char fixtures[MAX_PATH];
  char cmdline[32768];
  SECURITY_CAPABILITIES sc;
  LPWSTR sid_wstr = NULL;
  LPWSTR folder_wstr = NULL;
  LPSTR sid_str = NULL;
  const char* exe_name;
  int i;
  int r;
  int exit_code;
  size_t pos;
  ULONGLONG start_time;

  /* Capability SIDs to add. */
  PSID net_sid = NULL;
  SID_AND_ATTRIBUTES caps[1];
  int num_caps = 0;

  start_time = GetTickCount64();

  if (argc < 2) {
    fprintf(stderr, "usage: appcontainer.exe program.exe [args...]\n");
    return 1;
  }

  /* Get the current directory. */
  if (!GetCurrentDirectoryA(MAX_PATH, cwd)) {
    fprintf(stderr, "GetCurrentDirectoryA failed: %lu\n", GetLastError());
    return 1;
  }

  /* Resolve the exe to an absolute path. */
  if (!GetFullPathNameA(argv[1], MAX_PATH, abs_exe, NULL)) {
    fprintf(stderr, "GetFullPathNameA failed: %lu\n", GetLastError());
    return 1;
  }

  fprintf(stderr, "appcontainer: cwd=%s\n", cwd);
  fprintf(stderr, "appcontainer: exe=%s\n", abs_exe);

  /* Verify the exe exists. */
  if (GetFileAttributesA(abs_exe) == INVALID_FILE_ATTRIBUTES) {
    fprintf(stderr, "appcontainer: exe not found: %s (error %lu)\n",
            abs_exe, GetLastError());
    return 1;
  }

  /* Split the exe path into directory and file name. */
  exe_name = strrchr(abs_exe, '\\');
  if (exe_name == NULL || (size_t)(exe_name - abs_exe) >= sizeof(exe_dir)) {
    fprintf(stderr, "appcontainer: cannot split exe path: %s\n", abs_exe);
    return 1;
  }
  memcpy(exe_dir, abs_exe, exe_name - abs_exe);
  exe_dir[exe_name - abs_exe] = '\0';
  exe_name++;

  /* Create capability SIDs for network access. */
  if (!ConvertStringSidToSidA(INTERNET_CLIENT_SERVER_SID, &net_sid)) {
    fprintf(stderr, "appcontainer: warning: ConvertStringSidToSidA(%s): %lu\n",
            INTERNET_CLIENT_SERVER_SID, GetLastError());
  } else {
    caps[num_caps].Sid = net_sid;
    caps[num_caps].Attributes = SE_GROUP_ENABLED;
    num_caps++;
  }

  /* Delete any leftover profile from a previous run. */
  DeleteAppContainerProfile(profile_name);

  /* Create the AppContainer profile. */
  hr = CreateAppContainerProfile(profile_name,
                                 profile_display,
                                 profile_desc,
                                 num_caps > 0 ? caps : NULL,
                                 num_caps,
                                 &sid);
  if (FAILED(hr)) {
    fprintf(stderr, "CreateAppContainerProfile failed: 0x%08lx\n", hr);
    return 1;
  }

  if (ConvertSidToStringSidA(sid, &sid_str)) {
    fprintf(stderr, "appcontainer: SID=%s\n", sid_str);
    LocalFree(sid_str);
  }

  /* Find the profile's package folder.  Windows creates it as part of
   * the profile with a DACL already granting the AppContainer SID full
   * access, so staging the tests there needs no ACL modifications of
   * our own. */
  if (!ConvertSidToStringSidW(sid, &sid_wstr)) {
    fprintf(stderr, "ConvertSidToStringSidW failed: %lu\n", GetLastError());
    cleanup();
    return 1;
  }
  hr = GetAppContainerFolderPath(sid_wstr, &folder_wstr);
  LocalFree(sid_wstr);
  if (FAILED(hr)) {
    fprintf(stderr, "GetAppContainerFolderPath failed: 0x%08lx\n", hr);
    cleanup();
    return 1;
  }
  if (!WideCharToMultiByte(CP_ACP, 0, folder_wstr, -1,
                           folder, sizeof(folder), NULL, NULL)) {
    fprintf(stderr, "WideCharToMultiByte failed: %lu\n", GetLastError());
    CoTaskMemFree(folder_wstr);
    cleanup();
    return 1;
  }
  CoTaskMemFree(folder_wstr);
  fprintf(stderr, "appcontainer: folder=%s\n", folder);

  cleanup_sid = sid;
  SetConsoleCtrlHandler(ctrl_handler, TRUE);

  /* Stage the exe's directory (the exe itself, helper files and DLLs)
   * into the package folder, along with test/fixtures if the current
   * directory has one, and create a private temp directory. */
  if (stage_dir_flat(exe_dir, folder) != 0) {
    cleanup();
    return 1;
  }
  snprintf(fixtures, sizeof(fixtures), "%s\\test\\fixtures", cwd);
  if (GetFileAttributesA(fixtures) != INVALID_FILE_ATTRIBUTES) {
    snprintf(path, sizeof(path), "%s\\test", folder);
    if (!CreateDirectoryA(path, NULL) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
      fprintf(stderr, "appcontainer: CreateDirectory %s: %lu\n",
              path, GetLastError());
      cleanup();
      return 1;
    }
    snprintf(path, sizeof(path), "%s\\test\\fixtures", folder);
    if (stage_tree(fixtures, path) != 0) {
      cleanup();
      return 1;
    }
  }
  snprintf(path, sizeof(path), "%s\\tmp", folder);
  if (!CreateDirectoryA(path, NULL) &&
      GetLastError() != ERROR_ALREADY_EXISTS) {
    fprintf(stderr, "appcontainer: CreateDirectory %s: %lu\n",
            path, GetLastError());
    cleanup();
    return 1;
  }

  /* Point the child's temp directory into the package folder too.
   * The child inherits our environment. */
  SetEnvironmentVariableA("TMP", path);
  SetEnvironmentVariableA("TEMP", path);

  /* Grant the remaining (machine-global, revoked-on-exit) access. */
  modify_nul_access(sid, 1);
  modify_loopback_exemption(sid, 1);
  fprintf(stderr, "appcontainer: setup: %.3f s\n",
          (GetTickCount64() - start_time) / 1000.0);

  /* Set up SECURITY_CAPABILITIES. */
  memset(&sc, 0, sizeof(sc));
  sc.AppContainerSid = sid;
  if (num_caps > 0) {
    sc.Capabilities = caps;
    sc.CapabilityCount = num_caps;
  }

  /* Build the command line, running the staged copy of the exe. */
  snprintf(staged_exe, sizeof(staged_exe), "%s\\%s", folder, exe_name);
  pos = 0;
  r = append_quoted(cmdline, sizeof(cmdline), &pos, staged_exe);
  for (i = 2; r == 0 && i < argc; i++)
    r = append_quoted(cmdline, sizeof(cmdline), &pos, argv[i]);
  if (r != 0) {
    fprintf(stderr, "appcontainer: command line too long\n");
    cleanup();
    return 1;
  }

  fprintf(stderr, "appcontainer: launching: %s\n", cmdline);

  {
    ULONGLONG child_start = GetTickCount64();
    exit_code = run_child(staged_exe, cmdline, folder, &sc);
    fprintf(stderr, "appcontainer: child exited with code %d (%.3f s)\n",
            exit_code, (GetTickCount64() - child_start) / 1000.0);
  }

  /* Tear down: revoke the NUL and loopback grants and delete the
   * profile, which also removes the package folder and the staged
   * files with it. */
  cleanup();

  if (net_sid)
    LocalFree(net_sid);
  FreeSid(sid);
  fprintf(stderr, "appcontainer: elapsed %.3f s\n",
          (GetTickCount64() - start_time) / 1000.0);
  return exit_code;
}
