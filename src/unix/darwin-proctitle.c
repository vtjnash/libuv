/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
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

#include "uv.h"
#include "internal.h"

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <TargetConditionals.h>

#if !TARGET_OS_IPHONE
#include "darwin-stub.h"
#endif


static int uv__pthread_setname_np(const char* name) {
  char namebuf[64];  /* MAXTHREADNAMESIZE */
  int err;

<<<<<<< HEAD
#if !TARGET_OS_IPHONE
static CFStringRef (*pCFStringCreateWithCString)(CFAllocatorRef,
                                                 const char*,
                                                 CFStringEncoding);
static CFBundleRef (*pCFBundleGetBundleWithIdentifier)(CFStringRef);
static void *(*pCFBundleGetDataPointerForName)(CFBundleRef, CFStringRef);
static void *(*pCFBundleGetFunctionPointerForName)(CFBundleRef, CFStringRef);
static CFTypeRef (*pLSGetCurrentApplicationASN)(void);
static OSStatus (*pLSSetApplicationInformationItem)(int,
                                                    CFTypeRef,
                                                    CFStringRef,
                                                    CFStringRef,
                                                    CFDictionaryRef*);
static void* application_services_handle;
static void* core_foundation_handle;
static CFBundleRef launch_services_bundle;
static CFStringRef* display_name_key;
static CFDictionaryRef (*pCFBundleGetInfoDictionary)(CFBundleRef);
static CFBundleRef (*pCFBundleGetMainBundle)(void);
static CFBundleRef hi_services_bundle;
static OSStatus (*pSetApplicationIsDaemon)(int);
static CFDictionaryRef (*pLSApplicationCheckIn)(int, CFDictionaryRef);
static void (*pLSSetApplicationLaunchServicesServerConnectionStatus)(uint64_t,
                                                                     void*);


UV_DESTRUCTOR(static void uv__set_process_title_platform_fini(void)) {
  if (core_foundation_handle != NULL) {
    dlclose(core_foundation_handle);
    core_foundation_handle = NULL;
  }
=======
  strncpy(namebuf, name, sizeof(namebuf) - 1);
  namebuf[sizeof(namebuf) - 1] = '\0';
>>>>>>> v1.42.0

  err = pthread_setname_np(namebuf);
  if (err)
    return UV__ERR(err);

  return 0;
}

<<<<<<< HEAD
void uv__set_process_title_platform_init(void) {
#if !TARGET_OS_IPHONE
=======

int uv__set_process_title(const char* title) {
#if TARGET_OS_IPHONE
  return uv__pthread_setname_np(title);
#else
  CFStringRef (*pCFStringCreateWithCString)(CFAllocatorRef,
                                            const char*,
                                            CFStringEncoding);
  CFBundleRef (*pCFBundleGetBundleWithIdentifier)(CFStringRef);
  void *(*pCFBundleGetDataPointerForName)(CFBundleRef, CFStringRef);
  void *(*pCFBundleGetFunctionPointerForName)(CFBundleRef, CFStringRef);
  CFTypeRef (*pLSGetCurrentApplicationASN)(void);
  OSStatus (*pLSSetApplicationInformationItem)(int,
                                               CFTypeRef,
                                               CFStringRef,
                                               CFStringRef,
                                               CFDictionaryRef*);
  void* application_services_handle;
  void* core_foundation_handle;
  CFBundleRef launch_services_bundle;
  CFStringRef* display_name_key;
  CFDictionaryRef (*pCFBundleGetInfoDictionary)(CFBundleRef);
  CFBundleRef (*pCFBundleGetMainBundle)(void);
  CFDictionaryRef (*pLSApplicationCheckIn)(int, CFDictionaryRef);
  void (*pLSSetApplicationLaunchServicesServerConnectionStatus)(uint64_t,
                                                                void*);
  CFTypeRef asn;
  int err;

  err = UV_ENOENT;
>>>>>>> v1.42.0
  application_services_handle = dlopen("/System/Library/Frameworks/"
                                       "ApplicationServices.framework/"
                                       "Versions/A/ApplicationServices",
                                       RTLD_LAZY | RTLD_LOCAL);
  core_foundation_handle = dlopen("/System/Library/Frameworks/"
                                  "CoreFoundation.framework/"
                                  "Versions/A/CoreFoundation",
                                  RTLD_LAZY | RTLD_LOCAL);

  if (application_services_handle == NULL || core_foundation_handle == NULL)
    goto out;

  *(void **)(&pCFStringCreateWithCString) =
      dlsym(core_foundation_handle, "CFStringCreateWithCString");
  *(void **)(&pCFBundleGetBundleWithIdentifier) =
      dlsym(core_foundation_handle, "CFBundleGetBundleWithIdentifier");
  *(void **)(&pCFBundleGetDataPointerForName) =
      dlsym(core_foundation_handle, "CFBundleGetDataPointerForName");
  *(void **)(&pCFBundleGetFunctionPointerForName) =
      dlsym(core_foundation_handle, "CFBundleGetFunctionPointerForName");

  if (pCFStringCreateWithCString == NULL ||
      pCFBundleGetBundleWithIdentifier == NULL ||
      pCFBundleGetDataPointerForName == NULL ||
      pCFBundleGetFunctionPointerForName == NULL) {
    goto out;
  }

#define S(s) pCFStringCreateWithCString(NULL, (s), kCFStringEncodingUTF8)

  launch_services_bundle =
      pCFBundleGetBundleWithIdentifier(S("com.apple.LaunchServices"));

  if (launch_services_bundle == NULL)
    goto out;

  *(void **)(&pLSGetCurrentApplicationASN) =
      pCFBundleGetFunctionPointerForName(launch_services_bundle,
                                         S("_LSGetCurrentApplicationASN"));

  if (pLSGetCurrentApplicationASN == NULL)
    goto out;

  *(void **)(&pLSSetApplicationInformationItem) =
      pCFBundleGetFunctionPointerForName(launch_services_bundle,
                                         S("_LSSetApplicationInformationItem"));

  if (pLSSetApplicationInformationItem == NULL)
    goto out;

  display_name_key = pCFBundleGetDataPointerForName(launch_services_bundle,
                                                    S("_kLSDisplayNameKey"));

  if (display_name_key == NULL || *display_name_key == NULL)
    goto out;

  *(void **)(&pCFBundleGetInfoDictionary) = dlsym(core_foundation_handle,
                                     "CFBundleGetInfoDictionary");
  *(void **)(&pCFBundleGetMainBundle) = dlsym(core_foundation_handle,
                                 "CFBundleGetMainBundle");
  if (pCFBundleGetInfoDictionary == NULL || pCFBundleGetMainBundle == NULL)
    goto out;

  *(void **)(&pLSApplicationCheckIn) = pCFBundleGetFunctionPointerForName(
      launch_services_bundle,
      S("_LSApplicationCheckIn"));

  if (pLSApplicationCheckIn == NULL)
    goto out;

  *(void **)(&pLSSetApplicationLaunchServicesServerConnectionStatus) =
      pCFBundleGetFunctionPointerForName(
          launch_services_bundle,
          S("_LSSetApplicationLaunchServicesServerConnectionStatus"));

  if (pLSSetApplicationLaunchServicesServerConnectionStatus == NULL)
    goto out;

  pLSSetApplicationLaunchServicesServerConnectionStatus(0, NULL);

  /* Check into process manager?! */
  pLSApplicationCheckIn(-2,
                        pCFBundleGetInfoDictionary(pCFBundleGetMainBundle()));

  asn = pLSGetCurrentApplicationASN();

  err = UV_EBUSY;
  if (asn == NULL)
    goto out;

  err = UV_EINVAL;
  if (pLSSetApplicationInformationItem(-2,  /* Magic value. */
                                       asn,
                                       *display_name_key,
                                       S(title),
                                       NULL) != noErr) {
    goto out;
  }

  uv__pthread_setname_np(title);  /* Don't care if it fails. */
  err = 0;

out:
  if (core_foundation_handle != NULL)
    dlclose(core_foundation_handle);

  if (application_services_handle != NULL)
    dlclose(application_services_handle);

<<<<<<< HEAD
void uv__set_process_title(const char* title) {
  char namebuf[64];  /* MAXTHREADNAMESIZE */
#if !TARGET_OS_IPHONE
  if (core_foundation_handle != NULL && pSetApplicationIsDaemon(1) != noErr) {
    CFTypeRef asn;
    pLSSetApplicationLaunchServicesServerConnectionStatus(0, NULL);
    pLSApplicationCheckIn(/* Magic value */ -2,
                          pCFBundleGetInfoDictionary(pCFBundleGetMainBundle()));
    asn = pLSGetCurrentApplicationASN();
    pLSSetApplicationInformationItem(/* Magic value */ -2, asn,
                                     *display_name_key, S(title), NULL);
  }
#endif  /* !TARGET_OS_IPHONE */

  strncpy(namebuf, title, sizeof(namebuf) - 1);
  namebuf[sizeof(namebuf) - 1] = '\0';
  pthread_setname_np(namebuf);
=======
  return err;
#endif  /* !TARGET_OS_IPHONE */
>>>>>>> v1.42.0
}
