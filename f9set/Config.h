// \file f9set/Config.h
//
// fon9 泰國證券交易所(SET) 模組.
//
#ifndef __f9set_Config_h__
#define __f9set_Config_h__
#include "fon9/sys/Config.h"

/// 設定 f9set_API: MSVC 的 DLL import/export.
#if defined(f9set_NODLL)
   #define f9set_API
#elif defined(f9set_EXPORT)
   #define f9set_API    __declspec(dllexport)
#elif defined(f9set_WINDOWS)
   #define f9set_API    __declspec(dllimport)
#else//not defined: fon9_WINDOWS
   #define f9set_API
#endif

#endif//__f9set_Config_h__
