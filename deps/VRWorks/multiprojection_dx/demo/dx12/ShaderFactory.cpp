//----------------------------------------------------------------------------------
// File:        ShaderFactory.cpp
// SDK Version: 2.0
// Email:       vrsupport@nvidia.com
// Site:        http://developer.nvidia.com/
//
// Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of NVIDIA CORPORATION nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//----------------------------------------------------------------------------------

#include "ShaderFactory.h"
#include <Windows.h>
#include <sstream>
#include <d3dcompiler.h>

#include "dxcHelper\dxchelper.h"

#pragma comment(lib, "d3dcompiler.lib")


ShaderCacheEntry::ShaderCacheEntry()
	: data(nullptr)
	, sizeOfData(0)
{
}

ShaderCacheEntry::~ShaderCacheEntry()
{
	delete[] data;
	data = NULL;
}



UINT ShaderFactory::Hasher(const std::string& input)
{
  UINT _Val = 2166136261U;
  UINT _First = 0;
  UINT _Last = (UINT)input.size();
  UINT _Stride = 1;// + _Last / 10;

  for (; _First < _Last; _First += _Stride)
    _Val = 16777619U * _Val ^ (UINT)input[_First];

  return _Val;
}

UINT ShaderFactory::GetNameHash(const std::string& fileName)
{
  return Hasher(fileName);
}

UINT ShaderFactory::GetDefinesHash(const std::vector<shaderMacro>* pDefines)
{
  size_t shaderDefinesNum = pDefines ? pDefines->size() : 0;

  std::stringstream hashKey;
  if (pDefines && shaderDefinesNum)
  {
    for (auto it = pDefines->begin(); it != pDefines->end(); ++it)
    {
      hashKey << it->Name << '=' << it->Definition;
    }
  }

  return Hasher(hashKey.str());
}



ShaderFactory::ShaderFactory(NVRHI::IRendererInterface* rendererInterface, const std::string& basePath, std::mutex& contextMutex)
	: m_RendererInterface(rendererInterface)
	, m_BasePath(basePath)
	, m_ContextMutex(contextMutex)
{
}

void ShaderFactory::ClearCache()
{
	m_BytecodeCache.clear();
}

bool ShaderFactory::GetBytecode(const char* fileName, const char* target, const std::vector<shaderMacro>* pDefines, ShaderByteCode& byteCode, bool useDXC)
{
	(void)target;
	UINT nameHash = GetNameHash(fileName);
	UINT definesHash = GetDefinesHash(pDefines);

	UINT64 cacheKey = (UINT64(nameHash) << 32) | definesHash;
	ShaderCacheEntry& entry = m_BytecodeCache[cacheKey];

	if (entry.data)
	{
		byteCode.pBytecode = &(entry.data[0]);
		byteCode.bytecodeSize = entry.sizeOfData;

		return true;
	}
	else
	{
		std::string fullPath = m_BasePath + fileName + ".hlsl";
		std::wstring fullPathW(fullPath.begin(), fullPath.end());

		ID3DBlob* pCode = nullptr;
		ID3DBlob* pErrorMessages = nullptr;

		std::vector<D3D_SHADER_MACRO> definesWithTerminator;
		D3D_SHADER_MACRO* pD3DDefines = nullptr;

		// for dxcompiler
		IDxcBlob* pDxcCode = nullptr;
		IDxcBlobEncoding* pDxcErrorMessages = nullptr;

		std::vector<DxcDefine> dxcDefines;
		struct DxcDefineHelper
		{
			std::wstring Name;
			std::wstring Value;
		} dxcDefineHelper[32];

		if (pDefines)
		{
			if (useDXC)
			{
				UINT32 dxcDefineCount = 0;
				for (auto& define : *pDefines)
				{
					std::string strName = define.Name;
					std::string strDefinition = define.Definition;
					dxcDefineHelper[dxcDefineCount].Name = std::wstring(strName.begin(), strName.end());
					dxcDefineHelper[dxcDefineCount].Value = std::wstring(strDefinition.begin(), strDefinition.end());
					dxcDefines.push_back(DxcDefine{ dxcDefineHelper[dxcDefineCount].Name.c_str(), dxcDefineHelper[dxcDefineCount].Value.c_str() });
					dxcDefineCount++;
				}
			}
			else
			{
				for (auto& define : *pDefines)
				{
					definesWithTerminator.push_back(D3D_SHADER_MACRO{ define.Name, define.Definition });
				}
				definesWithTerminator.push_back(D3D_SHADER_MACRO{ nullptr, nullptr });

				pD3DDefines = &definesWithTerminator[0];
			}
		}

		while (true)
		{
			HRESULT hr;
			if (useDXC)
			{
				std::string strTarget = target;
				std::wstring strTargetW(strTarget.begin(), strTarget.end());

				hr = DxcHelper_CompileFromFile(
					fullPathW.c_str(),
					L"main",
					strTargetW.c_str(),
					NULL,
					0,
					&dxcDefines[0],
					(UINT)dxcDefines.size(),
					&pDxcCode,
					&pDxcErrorMessages);
			}
			else
			{
				hr = D3DCompileFromFile(
					fullPathW.c_str(),
					pD3DDefines,
					D3D_COMPILE_STANDARD_FILE_INCLUDE,
					"main",
					target,
					D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_DEBUG,
					0,
					&pCode,
					&pErrorMessages);
			}

			if (SUCCEEDED(hr) && (pCode || pDxcCode))
			{
				if (pErrorMessages)
				{
					OutputDebugStringA((const char*)pErrorMessages->GetBufferPointer());
					pErrorMessages->Release();
				}

				if (pDxcErrorMessages)
				{
					OutputDebugStringA((const char*)pDxcErrorMessages->GetBufferPointer());
					pDxcErrorMessages->Release();
				}

				break;
			}

			std::stringstream ss;
			ss << "ERROR: Failed to compile shader " << fileName << " for target " << target << ".\n";

			if (pErrorMessages)
			{
				const char* msg = (char*)pErrorMessages->GetBufferPointer();
				ss << msg << "\n";
				pErrorMessages->Release();
			}

			OutputDebugStringA(ss.str().c_str());

			ss << "\nReload and retry?";

			int mbResult = MessageBoxA(0, ss.str().c_str(), "VRWorks SDK", MB_ICONERROR | MB_YESNO);

			if (mbResult == IDNO)
				ExitProcess(1);
			else
				continue;
		}

		char* copyOfData = NULL;
		size_t size = 0;
		if (useDXC)
		{
			size = pDxcCode->GetBufferSize();
			copyOfData = new char[size];
			memcpy(copyOfData, pDxcCode->GetBufferPointer(), size);
			pDxcCode->Release();
		}
		else
		{
			size = pCode->GetBufferSize();
			copyOfData = new char[size];
			memcpy(copyOfData, pCode->GetBufferPointer(), size);
			pCode->Release();
		}

		entry.data = copyOfData;
		entry.sizeOfData = size;

		byteCode.bytecodeSize = size;
		byteCode.pBytecode = copyOfData;

		return true;
	}
}


bool ShaderFactory::CreateShader(const char* fileName, const std::vector<shaderMacro>* pDefines, NVRHI::ShaderType::Enum shaderType, NVRHI::ShaderHandle* pHandle)
{
	return CreateShader(fileName, pDefines, NVRHI::ShaderDesc(shaderType), pHandle);
}

bool ShaderFactory::CreateShader(const char* fileName, const std::vector<shaderMacro>* pDefines, const NVRHI::ShaderDesc& desc, NVRHI::ShaderHandle* pHandle)
{
	const char* profile = nullptr;
	switch (desc.shaderType)
	{
	case NVRHI::ShaderType::SHADER_VERTEX:
		profile = desc.useDXC ? "vs_6_1" : "vs_5_0";
		break;
	case NVRHI::ShaderType::SHADER_HULL:
		profile = desc.useDXC ? "hs_6_1" : "hs_5_0";
		break;
	case NVRHI::ShaderType::SHADER_DOMAIN:
		profile = desc.useDXC ? "ds_6_1" : "ds_5_0";
		break;
	case NVRHI::ShaderType::SHADER_GEOMETRY:
		profile = desc.useDXC ? "gs_6_1" : "gs_5_0";
		break;
	case NVRHI::ShaderType::SHADER_PIXEL:
		profile = desc.useDXC ? "ps_6_1" : "ps_5_0";
		break;
	case NVRHI::ShaderType::SHADER_COMPUTE:
		profile = desc.useDXC ? "cs_6_1" : "cs_5_0";
		break;
	}

	ShaderByteCode byteCode;
	if (!GetBytecode(fileName, profile, pDefines, byteCode, desc.useDXC))
		return false;

	*pHandle = m_RendererInterface->createShader(desc, byteCode.pBytecode, byteCode.bytecodeSize);

	return true;
} 

bool ShaderFactory::CreateInputLayout(const char* fileName, const std::vector<shaderMacro>* pDefines, const NVRHI::VertexAttributeDesc* pElements, UINT numElements, NVRHI::InputLayoutHandle* pLayoutHandle)
{
	ShaderByteCode byteCode;
	if (!GetBytecode(fileName, "vs_5_0", pDefines, byteCode))
		return false;

	*pLayoutHandle = m_RendererInterface->createInputLayout(pElements, numElements, byteCode.pBytecode, byteCode.bytecodeSize);

	return true;
}