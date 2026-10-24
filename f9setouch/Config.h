// \file f9setouch/Config.h
//
// fon9 泰國證券交易所(SET) OUCH 下單模組.
//
#ifndef __f9setouch_Config_h__
#define __f9setouch_Config_h__
#include "fon9/sys/Config.h"

/// 設定 f9setouch_API: MSVC 的 DLL import/export.
#if defined(f9setouch_NODLL)
   #define f9setouch_API
#elif defined(f9setouch_EXPORT)
   #define f9setouch_API    __declspec(dllexport)
#elif defined(f9setouch_WINDOWS)
   #define f9setouch_API    __declspec(dllimport)
#else//not defined: fon9_WINDOWS
   #define f9setouch_API
#endif

#endif//__f9setouch_Config_h__
