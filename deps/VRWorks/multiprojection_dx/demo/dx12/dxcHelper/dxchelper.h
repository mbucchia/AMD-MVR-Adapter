
///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// dxchelper.h                                                                  //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

#ifndef __DXC_HELPER__
#define __DXC_HELPER__

#include "dxcapi.h"

#define DXCCOMPILER_DLL L"dxcompiler.dll"

BOOL DxcHelper_Initialyze();
VOID DxcHelper_Release();

BOOL DxcHelper_IsDxcEnabled();

HRESULT DxcHelper_CompileFromFile(_In_ LPCWSTR pFileName,
																	_In_ LPCWSTR pEntryPoint,                     // entry point name
																	_In_ LPCWSTR pTargetProfile,                  // shader profile to compile
																	_In_count_(argCount) LPCWSTR *pArguments,     // Array of pointers to arguments
																	_In_ UINT32 argCount,                         // Number of arguments
																	_In_count_(defineCount) const DxcDefine *pDefines,  // Array of defines
																	_In_ UINT32 defineCount,                      // Number of defines
																	_Out_ IDxcBlob **ppCode,
																	_Always_(_Outptr_opt_result_maybenull_) IDxcBlobEncoding **ppErrorMsgs);

#endif
