#include <windows.h>
#include "dxchelper.h"

typedef HRESULT(WINAPI * LPDXCREATEINSTANCE)(_In_ REFCLSID   rclsid, _In_ REFIID     riid, _Out_ LPVOID*   ppv);

static LPDXCREATEINSTANCE s_pDxcCreateInstance = NULL;
static IDxcLibrary *s_pDxcLibrary = NULL;
static IDxcCompiler *s_pDxcCompiler = NULL;

BOOL DxcHelper_Initialyze()
{
	HMODULE hModDXC = LoadLibrary(DXCCOMPILER_DLL);
	if (hModDXC == NULL)
	{
		return FALSE;
	}
	s_pDxcCreateInstance = (LPDXCREATEINSTANCE)GetProcAddress(hModDXC, "DxcCreateInstance");

	s_pDxcCreateInstance(CLSID_DxcLibrary, __uuidof(IDxcLibrary), (void **)&s_pDxcLibrary);
	if (s_pDxcLibrary == NULL)
		return FALSE;

	s_pDxcCreateInstance(CLSID_DxcCompiler, __uuidof(IDxcCompiler), (void **)&s_pDxcCompiler);
	if (s_pDxcCompiler == NULL)
	{
		s_pDxcLibrary->Release();
		return FALSE;
	}

	return TRUE;
}

VOID DxcHelper_Release()
{
	if (s_pDxcLibrary)
	{
		s_pDxcLibrary->Release();
		s_pDxcLibrary = NULL;
	}

	if (s_pDxcCompiler)
	{
		s_pDxcCompiler->Release();
		s_pDxcCompiler = NULL;
	}
}

BOOL DxcHelper_IsDxcEnabled()
{
	return (s_pDxcCreateInstance != NULL);
}

HRESULT DxcHelper_CompileFromFile(_In_ LPCWSTR pFileName,
																	_In_ LPCWSTR pEntryPoint,                     // entry point name
																	_In_ LPCWSTR pTargetProfile,                  // shader profile to compile
																	_In_count_(argCount) LPCWSTR *pArguments,     // Array of pointers to arguments
																	_In_ UINT32 argCount,                         // Number of arguments
																	_In_count_(defineCount) const DxcDefine *pDefines,  // Array of defines
																	_In_ UINT32 defineCount,                      // Number of defines
																	_Out_ IDxcBlob **ppCode,
																	_Always_(_Outptr_opt_result_maybenull_) IDxcBlobEncoding **ppErrorMsgs)
{
	*ppCode = NULL;
	*ppErrorMsgs = NULL;

	if (s_pDxcCreateInstance == NULL)
		return E_FAIL;

	HRESULT hr;
	
	IDxcIncludeHandler *pDxcIncludeHandler = NULL;
	hr = s_pDxcLibrary->CreateIncludeHandler(&pDxcIncludeHandler);
	if (hr != S_OK)
		return hr;

	IDxcBlobEncoding *pSource;
	hr = s_pDxcLibrary->CreateBlobFromFile(pFileName, NULL, &pSource);
	if (hr != S_OK)
		return hr;

	LPCWSTR *pDxcArgumnets = pArguments; 
	UINT32 dxcArgCount = argCount;

	LPCWSTR ppDefualtArgs[] = { L"/Zpr" };
	if (pDxcArgumnets == NULL)
	{
		pDxcArgumnets = ppDefualtArgs;
		dxcArgCount = _countof(ppDefualtArgs);
	}

	IDxcOperationResult *pResult;
	hr = s_pDxcCompiler->Compile(
		pSource,          // program text
		pFileName,   // file name, mostly for error messages
		pEntryPoint,          // entry point function
		pTargetProfile,        // target profile
		pDxcArgumnets,           // compilation arguments
		dxcArgCount, // number of compilation arguments
		pDefines,  // Array of defines
		defineCount,                      // Number of defines
		pDxcIncludeHandler,          // handler for #include directives
		&pResult);

	pSource->Release();
	if (hr != S_OK)
		return hr;

	HRESULT hrCompilation;
	pResult->GetStatus(&hrCompilation);

	if (SUCCEEDED(hrCompilation)) {
		pResult->GetResult(ppCode);
		// Save to a file, disassemble and print, store somewhere ...
		pResult->Release();
	}
	else {
		pResult->GetErrorBuffer(ppErrorMsgs);
	}

	return hrCompilation;
}