//----------------------------------------------------------------------------------
// File:        demo_dx11.cpp
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



#if USE_D3D11

#include "DeviceManager11.h"
#include "GFSDK_NVRHI_D3D11.h"

#define API_STRING "D3D11"
NVRHI::RendererInterfaceD3D11* g_pRendererInterface = NULL;

#elif USE_D3D12

#include "DeviceManager12.h"
#include "GFSDK_NVRHI_D3D12.h"
#define API_STRING "D3D12"
NVRHI::RendererInterfaceD3D12* g_pRendererInterface = NULL;

// TODO this is a workaround for NVAPI lib file using _vsnwprintf 
#pragma comment(lib, "legacy_stdio_definitions.lib")

#elif USE_GL4

#include "DeviceManagerGL4.h"
#include "GFSDK_NVRHI_OpenGL4.h"
#define API_STRING "OpenGL"
NVRHI::RendererInterfaceOGL* g_pRendererInterface = NULL;

#endif

#if USE_D3D12
#include "dxcHelper\dxchelper.h"
#endif

#define SWAP_CHAIN_FORMAT DXGI_FORMAT_R8G8B8A8_UNORM

#include "BindingHelpers.h"

DeviceManager* g_DeviceManager = NULL;

static bool					g_bInitialized = false;

class RendererErrorCallback : public NVRHI::IErrorCallback
{
	void signalError(const char* file, int line, const char* errorDesc)
	{
		char buffer[4096];
		int length = (int)strlen(errorDesc);
		length = std::min(length, 4000); // avoid a "buffer too small" exception for really long error messages
		sprintf_s(buffer, "%s:%i\n%.*s", file, line, length, errorDesc);

		OutputDebugStringA(buffer);
		OutputDebugStringA("\n");
		MessageBoxA(NULL, buffer, "ERROR", MB_ICONERROR | MB_OK);
	}
};

RendererErrorCallback g_ErrorCallback;


#include <util.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <windows.h>
#include <d3d11_1.h>
#include "camera.h"
#include <AntTweakBar.h>
#include <nvapi.h>
#include <nvShaderExtnEnums.h>
#include <nv_vr.h>
#include "shaders/shader-slots.h"
#include "Scene.h"
#include "json/reader.h"
#include <fstream>
#include <Shlwapi.h>
#include "ShaderFactory.h"
#include "ShaderState.h"
#include <thread>
#include <memory>
#include "VrSystem.h"
#include "cxxopts.hpp"
#include "FreeImage.h"

using namespace util;

// Rendering settings - global for simplicity


float3						g_vecDirectionalLight		= normalize(makefloat3(-2.0f, 10.0f, 1.5f));
rgb							g_rgbDirectionalLight		= makergb(1.1f, 1.0f, 0.7f);
rgb							g_rgbSky					= makergb(0.f, 0.f, 0.f);
float						g_normalOffsetShadow		= 1e-5f;	// meters
float						g_shadowSharpening			= 1.0f;
float						g_zNear						= 0.1f;		// meters
float						g_zFar						= 100.0f;	// meters
bool						g_drawUI					= true;
int							g_triangleCounter			= 0;
int							g_perfHudMode				= 0;
bool                        g_enablePerfQueries         = false;

// Rendering
bool						g_vsync						= false;
int							g_repeatRenderingCount		= 1;
int 						g_msaaSampleCount			= 1;
bool						g_enableSSAO				= true;
bool						g_temporalAA				= true;
bool						g_suppressTemporalAA		= false;
int							g_sceneIndex				= 0;
std::string					g_sceneName					= "sponza.json";
float						g_convergenceAngle			= 0;

// Common multi-projection data
Nv::VR::Data				g_projectionData			= {};
Nv::VR::Data				g_projectionDataPrev		= {};
Nv::VR::Data				g_projectionDataVR[2]		= {};
Nv::VR::Data				g_projectionDataVRPrev[2]	= {};
Nv::VR::Data				g_planarData				= {};
Nv::VR::Data				g_planarDataVR[2]			= {};

// MultiRes
bool						g_multiResEnabled			= false;
bool						g_drawMultiResSplits		= false;
bool						g_disableUpscale			= false;
Nv::VR::MultiRes::Configuration		g_multiResConfig		= Nv::VR::MultiRes::ConfigurationSet_OculusRift_CV1[0];
Nv::VR::MultiRes::Configuration		g_multiResConfigVR[2]	= {};
int							g_multiResConfigIndex		= 0;

// PascalVR
bool						g_lensMatchedShadingEnabled	= false;
bool						g_drawLMSSplits				= false;
bool						g_disableUnwarp				= false;
Nv::VR::LensMatched::Configuration	g_lensMatchedConfig			= Nv::VR::LensMatched::ConfigurationSet_OculusRift_CV1[0];
Nv::VR::LensMatched::Configuration	g_lensMatchedConfigVR[2]	= {};
int							g_lensMatchedConfigIndex	= 0;
float						g_resolutionScale			= 1.0f;
bool						g_frustumCulling			= true;
bool						g_fakeVREnabled				= false;
bool						g_instancedStereoEnabled	= false;
bool						g_singlePassStereoEnabled	= false;
bool						g_MultiViewEnabled		= false;
NVRHI::ViewInstancingDesc	g_ViewInstancingDesc;
StereoMode					g_stereoMode				= StereoMode::NONE;
NvFeatureLevel				g_featureLevel				= NvFeatureLevel::GENERIC_DX11;
#if USE_D3D12
D3D12_VIEW_INSTANCING_TIER  g_ViewInstancingTier = D3D12_VIEW_INSTANCING_TIER_NOT_SUPPORTED;
#endif

int							g_captureFrame = 0;
std::string					g_captureFileName = "";

// Messages to show
const int					err_none						= 0;
const int					err_MRSPlusLMSNotSuppored		= 1;
const int					err_MRSPlusSPSNotSupported		= 2;
const int					err_StereoNotEnabled			= 3;
const int					err_InstancedPlanarOnly			= 4;
const int					err_NoVrHeadset					= 5;
const int					err_VrDisplayLost				= 6;
const int					err_VrDisplayWrongAdapter		= 7;
const int					err_MRSPlusMultiViewNotSupported		= 8;
const int					err_LMSPlusViewInstancingNotSupported	= 9;
const int					err_ViewInstancingNotSupported			= 10;
const int					err_ExperimentalShaderModelsNotSupported = 11;
const int					err_ConvergencePlusSPSNotSupported = 12;
const int					err_ConvergenceNotSupported = 13;
int							g_message_to_show; // ID of Error/Warning message to show
bool						g_suppressMessageBox = false;

const wchar_t*					g_WindowTitle = L"NVIDIA VRWorks Sample (DX11)";

bool IsMultiResShadingSupported() { return g_featureLevel >= NvFeatureLevel::MAXWELL_GPU; }
bool IsLensMatchedShadingSupported() { return g_featureLevel >= NvFeatureLevel::PASCAL_GPU; }
bool IsSinglePassStereoSupported() { return g_featureLevel >= NvFeatureLevel::PASCAL_GPU; }
#if USE_D3D12
bool IsMultiViewSupported() { return true; }
bool IsMultiViewEmulationEnabled() { return false; }
#else
bool IsMultiViewSupported() { return g_featureLevel >= NvFeatureLevel::TURING_GPU; }
bool IsMultiViewEmulationEnabled() { return g_MultiViewEnabled && g_featureLevel < NvFeatureLevel::TURING_GPU; }
#endif

// Constant buffers

struct CBFrame									// matches cbuffer CBFrame in shader-common.hlsli
{
	float4x4	m_matWorldToClip;
	float4x4	m_matWorldToClipR;
	float4x4	m_matWorldToClipPrev;
	float4x4	m_matWorldToClipPrevR;
	float4x4	m_matProjInv;
	float4x4	m_matWorldToViewNormal;
	float4x4	m_matWorldToUvzwShadow;
	float3x4	m_matWorldToUvzShadowNormal;	// actually float3x3, but constant buffer packing rules...
	
	float3		m_vecDirectionalLight;
	float		m_padding1;

	rgb			m_rgbDirectionalLight;
	float		m_padding2;

	float2		m_dimsShadowMap;
	float		m_normalOffsetShadow;
	float		m_shadowSharpening;

	float4		m_screenSize;

	float2		m_viewportOrigin;
	float2		m_randomOffset;

	float		m_temporalAAClampingFactor;
	float		m_temporalAANewFrameWeight;
	float		m_textureLodBias;
	float		m_padding3;
};

struct CBVRData
{
	Nv::VR::FastGSCBData	m_vrFastGSCBData;
	Nv::VR::FastGSCBData	m_vrFastGSCBDataR;
	Nv::VR::RemapCBData		m_vrRemapCBData;
	Nv::VR::RemapCBData		m_vrRemapCBDataR;
	Nv::VR::RemapCBData		m_vrRemapCBDataPrev;
	Nv::VR::RemapCBData		m_vrRemapCBDataPrevR;
};

// Very simple shadow map class, fits an orthogonal shadow map around a scene bounding box
class ShadowMap
{
public:
	NVRHI::IRendererInterface* m_rendererInterface;
	uint32_t			m_width;
	uint32_t			m_height;
	NVRHI::TextureHandle m_shadowTexture;
	float3				m_vecLight;					// Unit vector toward directional light
	box3				m_boundsScene;				// AABB of scene in world space

	float4x4			m_matProj;					// Projection matrix
	float4x4			m_matWorldToClip;			// Matrix for rendering shadow map
	float4x4			m_matWorldToUvzw;			// Matrix for sampling shadow map
	float3x3			m_matWorldToUvzNormal;		// Matrix for transforming normals to shadow map space
	float3				m_vecDiam;					// Diameter in world units along shadow XYZ axes

	ShadowMap::ShadowMap()
		: m_rendererInterface(nullptr)
		, m_width(0)
		, m_height(0)
		, m_shadowTexture(nullptr)
		, m_vecLight(makefloat3(0.0f))
		, m_boundsScene(makebox3Empty())
		, m_matProj(makefloat4x4(0.0f))
		, m_matWorldToClip(makefloat4x4(0.0f))
		, m_matWorldToUvzw(makefloat4x4(0.0f))
		, m_matWorldToUvzNormal(makefloat3x3(0.0f))
		, m_vecDiam(makefloat3(0.0f))
	{
	}

	void Init(
		NVRHI::IRendererInterface* rendererInterface,
		int2_arg dims,
		NVRHI::Format::Enum shadowFormat)
	{
		m_width = dims.x;
		m_height = dims.y;

		m_rendererInterface = rendererInterface;
		
		NVRHI::TextureDesc desc;
		desc.width = dims.x;
		desc.height = dims.y;
		desc.sampleCount = 1;
		desc.isRenderTarget = true;
		desc.format = shadowFormat;
        desc.debugName = "ShadowMap";
        desc.useClearValue = true;
        desc.clearValue = NVRHI::Color(1.f, 0.f, 0.f, 0.f);
		m_shadowTexture = m_rendererInterface->createTexture(desc, nullptr);

		LOG("Created shadow map - %dx%d", dims.x, dims.y);
	}

	void Reset()
	{
		m_rendererInterface->destroyTexture(m_shadowTexture);
		m_rendererInterface = nullptr;

		m_vecLight = makefloat3(0.0f);
		m_boundsScene = makebox3Empty();
		m_matProj = makefloat4x4(0.0f);
		m_matWorldToClip = makefloat4x4(0.0f);
		m_matWorldToUvzw = makefloat4x4(0.0f);
		m_matWorldToUvzNormal = makefloat3x3(0.0f);
		m_vecDiam = makefloat3(0.0f);
	}

	void UpdateMatrix()
	{
		// Calculate view matrix based on light direction

		// Choose a world-space up-vector
		float3 vecUp = { 0.0f, 0.0f, 1.0f };
		if (all(isnear(m_vecLight.xy, 0.0f)))
			vecUp = makefloat3(1.0f, 0.0f, 0.0f);

		affine3 viewToWorld = lookatZ(-m_vecLight, vecUp);
		affine3 worldToView = transpose(viewToWorld);

		// Transform scene AABB into view space and recalculate bounds
		box3 boundsView = boxTransform(m_boundsScene, worldToView);
		float3 vecDiamOriginal = boundsView.diagonal();

		// Select maximum diameter along X and Y, so that shadow map texels will be square
		float maxXY = max(vecDiamOriginal.x, vecDiamOriginal.y);
		m_vecDiam = makefloat3(maxXY, maxXY, vecDiamOriginal.z);
		boundsView = boxGrow(boundsView, 0.5f * (m_vecDiam - vecDiamOriginal));

		// Calculate orthogonal projection matrix to fit the scene bounds
		m_matProj = orthoProjD3DStyle(
			boundsView.m_mins.x,
			boundsView.m_maxs.x,
			boundsView.m_mins.y,
			boundsView.m_maxs.y,
			-boundsView.m_maxs.z,
			-boundsView.m_mins.z);

		m_matWorldToClip = affineToHomogeneous(worldToView) * m_matProj;

		// Calculate alternate matrix that maps to [0, 1] UV space instead of [-1, 1] clip space
		float4x4 matClipToUvzw =
		{
			0.5f,  0,    0, 0,
			0,    -0.5f, 0, 0,
			0,     0,    1, 0,
			0.5f,  0.5f, 0, 1,
		};
		m_matWorldToUvzw = m_matWorldToClip * matClipToUvzw;

		// Calculate inverse transpose matrix for transforming normals
		float3x3 matWorldToUvz = makefloat3x3(m_matWorldToUvzw);
		m_matWorldToUvzNormal = transpose(inverse(matWorldToUvz));
	}

	void Invalidate()
	{
		memset(m_matWorldToClip, 0, sizeof(float4x4));
	}
};

static const Nv::VR::MultiRes::Configuration* MultiResPresets[] = {
	nullptr,
	&Nv::VR::MultiRes::Configuration_Balanced,
	&Nv::VR::MultiRes::Configuration_Aggressive,
	&Nv::VR::MultiRes::ConfigurationSet_OculusRift_CV1[0],
	&Nv::VR::MultiRes::ConfigurationSet_OculusRift_CV1[1],
	&Nv::VR::MultiRes::ConfigurationSet_OculusRift_CV1[2],
	&Nv::VR::MultiRes::ConfigurationSet_HTC_Vive[0],
	&Nv::VR::MultiRes::ConfigurationSet_HTC_Vive[1],
	&Nv::VR::MultiRes::ConfigurationSet_HTC_Vive[2],
	&Nv::VR::MultiRes::ConfigurationSet_HTC_VivePro[0],
	&Nv::VR::MultiRes::ConfigurationSet_HTC_VivePro[1],
	&Nv::VR::MultiRes::ConfigurationSet_HTC_VivePro[2],
};

static const Nv::VR::LensMatched::Configuration* LensMatchedPresets[] = {
	nullptr,
	&Nv::VR::LensMatched::ConfigurationSet_OculusRift_CV1[0],
	&Nv::VR::LensMatched::ConfigurationSet_OculusRift_CV1[1],
	&Nv::VR::LensMatched::ConfigurationSet_OculusRift_CV1[2],
	&Nv::VR::LensMatched::ConfigurationSet_HTC_Vive[0],
	&Nv::VR::LensMatched::ConfigurationSet_HTC_Vive[1],
	&Nv::VR::LensMatched::ConfigurationSet_HTC_Vive[2],
	&Nv::VR::LensMatched::ConfigurationSet_HTC_VivePro[0],
	&Nv::VR::LensMatched::ConfigurationSet_HTC_VivePro[1],
	&Nv::VR::LensMatched::ConfigurationSet_HTC_VivePro[2],
	&Nv::VR::LensMatched::ConfigurationSet_Generic[0],
	&Nv::VR::LensMatched::ConfigurationSet_Generic[1],
	&Nv::VR::LensMatched::ConfigurationSet_Generic[2],
};


class VRWorksSample : public IVisualController
{
	friend class AntTweakBarVisualController;

public:
	std::mutex							m_ContextMutex;

	std::vector<Scene*>					m_scenes;
	ShaderFactory*						m_pShaderFactory;
	ShaderState*						m_pShaderState;
	bool								m_bCompilingShaders;
	std::thread*						m_pCompilingThread;
	bool								m_bLoadingScene;
	std::thread*						m_pSceneLoadingThread;
	Scene::LoadingStats					m_SceneLoadingStats;

	ShadowMap							m_shadowMap;

	float4x4							m_matWorldToClipPrev;
	float4x4							m_matWorldToClipPrevR;
	Framework::FPSCamera				m_camera;

	std::unique_ptr<VrSystem>			m_vr;
	std::vector<NVRHI::TextureHandle>	m_oculusSwapTextures;
	float4x4							m_matProjVR[2];

	virtual void						OnRender(NVRHI::TextureHandle& mainRenderTarget) /*override*/;

	void								ResetCamera();
	void								DrawObjects(NVRHI::DrawCallState& drawCallState, NVRHI::ShaderHandle pPs, NVRHI::ShaderHandle pPsAlphaTest, bool bShadowMapPass);
	void								DrawObjectInstances(NVRHI::DrawCallState& drawCallState, NVRHI::ShaderHandle pPs, NVRHI::ShaderHandle pPsAlphaTest, Scene* pScene, bool bShadowMapPass);
	void								CalcEyeMatrices(bool vrActive, int eye, float4x4* ref_worldToClip, point3* ref_cameraPos, affine3* ref_eyeToWorld);
	void								CalculateProjectionMatrices();
	void								RenderScene();
	void								DrawRepeatedScene(NVRHI::DrawCallState& drawCallState);
	void								RenderShadowMap();
	void								FrustumCull(const float4x4& matWorldToClip);
	void								FrustumCullStereo(const float4x4& matWorldToClipLeft, const float4x4& matWorldToClipRight);
	StereoMode							GetCurrentStereoMode();

	// HMD support
	void								TryActivateVR();
	void								DeactivateVR();
    bool								IsVRActive() const { return m_vr != nullptr; }
    bool								IsVROrFakeVRActive() const { return m_vr != nullptr || g_fakeVREnabled; }
	void								CreateVRSwapChainTextures();
	void								ReleaseVRSwapChainTextures();

	//  VR common
	void								RecalcVRData();
	void								SetVRCBData(const Nv::VR::Data& data, const Nv::VR::Data& dataR, const Nv::VR::Data& dataPrev, const Nv::VR::Data& dataPrevR, const int2_arg dimsRt, CBVRData* ref_cbData);
	void								SetVRViewports(NVRHI::DrawCallState& state, const Nv::VR::ProjectionViewports& viewports, float2 viewportOffset = float2());
	void								SetVRCBDataMono(CBVRData* ref_cbData);
	void								SetVRCBDataSingleEye(int eye, CBVRData* ref_cbData);
	void								SetVRCBDataStereo(CBVRData* ref_cbData);
	void								SetVRViewportsMono(NVRHI::DrawCallState& state, float2 viewportOffset = float2());
	void								SetVRViewportsSingleEye(NVRHI::DrawCallState& state, int eye, float2 viewportOffset = float2());
	void								SetVRViewportsStereo(NVRHI::DrawCallState& state, float2 viewportOffset = float2());
	void								FlattenImage();
	void								PresentToHMD(NVRHI::TextureHandle pRT);

	// Lens-Matched shading
	void								EnableLensMatchedShading(const Nv::VR::LensMatched::Configuration* config);
	void								EnableLensMatchedShadingWithSinglePassStereo(const Nv::VR::LensMatched::Configuration* leftConfig, const Nv::VR::LensMatched::Configuration* rightConfig);
	void								DisableLensMatchedShading();
	void								DrawSafeZone(const NVRHI::DrawCallState& drawCallState);

	void								ProcessSaveCommand(NVRHI::TextureHandle pRtFinal);


	struct PerfSection
	{
		NVRHI::IRendererInterface* m_pRenderer;
		NVRHI::PerformanceQueryHandle m_Query;

		PerfSection(NVRHI::IRendererInterface* pRenderer, NVRHI::PerformanceQueryHandle query)
			: m_pRenderer(pRenderer)
		{
			m_Query = query;
			m_pRenderer->beginPerformanceQuery(m_Query, !g_enablePerfQueries);
		}

		~PerfSection()
		{
			m_pRenderer->endPerformanceQuery(m_Query);
		}
	};

#undef PERF_SECTION
    #define PERF_SECTION(QUERY) PerfSection PerfSection_##NAME(m_RendererInterface, QUERY)

	NVRHI::IRendererInterface*	m_RendererInterface;

	NVRHI::BufferHandle			m_pBufLineVertices;
	
	NVRHI::ConstantBufferHandle	m_cbFrame;
	NVRHI::ConstantBufferHandle	m_cbVRData;
	
    NVRHI::TextureHandle		m_grayTexture;
    NVRHI::TextureHandle		m_blackTexture;

	NVRHI::TextureHandle		m_rtSceneMSAA;
	NVRHI::TextureHandle		m_rtMotionVectorsMSAA;
	NVRHI::TextureHandle		m_rtNormalsMSAA;
	NVRHI::TextureHandle		m_rtScene;
	NVRHI::TextureHandle		m_rtScenePrev;
	NVRHI::TextureHandle		m_rtUpscaled;
	NVRHI::TextureHandle		m_dstSceneMSAA;

	NVRHI::SamplerHandle		m_pSsBilinearClamp;
	NVRHI::SamplerHandle		m_pSsTrilinearRepeatAniso;
	NVRHI::SamplerHandle		m_pSsPCF;

    NVRHI::PerformanceQueryHandle m_pqFrame;
    NVRHI::PerformanceQueryHandle m_pqRenderScene;
    NVRHI::PerformanceQueryHandle m_pqRenderShadowMap;
    NVRHI::PerformanceQueryHandle m_pqDrawSplits;
    NVRHI::PerformanceQueryHandle m_pqBlit;
    NVRHI::PerformanceQueryHandle m_pqSSAO;
    NVRHI::PerformanceQueryHandle m_pqTemporalAA;
    NVRHI::PerformanceQueryHandle m_pqDrawSafeZone;
    NVRHI::PerformanceQueryHandle m_pqFlattenImage;

	std::vector<LineVertex>		m_lineVertices;
	int							m_lineVerticesPerDraw;
	
	int2						m_windowSize;

	
	VRWorksSample()
		: m_bCompilingShaders(false)
		, m_pCompilingThread(nullptr)
		, m_bLoadingScene(false)
		, m_pSceneLoadingThread(nullptr)
		, m_vr(nullptr)
		, m_pBufLineVertices(nullptr)
		, m_cbFrame(nullptr)
		, m_cbVRData(nullptr)
        , m_grayTexture(nullptr)
        , m_blackTexture(nullptr)
		, m_rtSceneMSAA(nullptr)
		, m_rtMotionVectorsMSAA(nullptr)
		, m_rtNormalsMSAA(nullptr)
		, m_rtScene(nullptr)
		, m_rtScenePrev(nullptr)
		, m_rtUpscaled(nullptr)
		, m_dstSceneMSAA(nullptr)
		, m_pSsBilinearClamp(nullptr)
		, m_pSsTrilinearRepeatAniso(nullptr)
		, m_pSsPCF(nullptr)
        , m_pqFrame(nullptr)
        , m_pqRenderScene(nullptr)
        , m_pqRenderShadowMap(nullptr)
        , m_pqDrawSplits(nullptr)
        , m_pqBlit(nullptr)
        , m_pqSSAO(nullptr)
        , m_pqTemporalAA(nullptr)
        , m_pqDrawSafeZone(nullptr)
        , m_pqFlattenImage(nullptr)
	{
		memset(&m_matWorldToClipPrev, 0, sizeof(float4x4));
		memset(&m_matWorldToClipPrevR, 0, sizeof(float4x4));
		m_lineVerticesPerDraw = 1024;
	}


	virtual LRESULT MsgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) override
	{
		(hWnd);

		// Let the camera try to process the message
		if (m_camera.HandleWindowsMessage(message, wParam, lParam))
			return 0;

		switch (message)
		{
		case WM_KEYDOWN:
			switch (wParam)
			{
			case VK_HOME:
				ResetCamera();
				break;

			case VK_ESCAPE:
				PostQuitMessage(0);
				break;

			case VK_TAB:
				g_drawUI = !g_drawUI;
				break;

			case 'R':
				m_vr->Recenter();
				break;
			}
			return 0;

		default:
			return 1;
		}
	}

	virtual void Animate(double fElapsedTimeSeconds) override
	{
		m_camera.Update(float(fElapsedTimeSeconds));
	}

	virtual void Render(RenderTargetView RTV) override
	{
		#if USE_D3D11
				ID3D11Resource* pMainResource = NULL;
				RTV->GetResource(&pMainResource);
				NVRHI::TextureHandle mainRenderTarget = g_pRendererInterface->getHandleForTexture(pMainResource, NVRHI::Format::SRGBA8_UNORM);
				pMainResource->Release();
		#elif USE_D3D12
				(void)RTV;
				NVRHI::TextureHandle mainRenderTarget = g_pRendererInterface->getHandleForTexture(g_DeviceManager->GetCurrentBackBuffer(), NVRHI::Format::SRGBA8_UNORM);
				g_pRendererInterface->setNonManagedTextureResourceState(mainRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
		#elif USE_GL4
				(void)RTV;
				NVRHI::TextureHandle mainRenderTarget = g_pRendererInterface->getHandleForDefaultBackBuffer();
		#endif
		
		OnRender(mainRenderTarget);
				
		#if USE_D3D11
				g_pRendererInterface->forgetAboutTexture(pMainResource);
		#elif USE_D3D12
				// This needs to be done before resizing the window, but there's no PreResize event from DeviceManager
				g_pRendererInterface->destroyTexture(mainRenderTarget);
				g_pRendererInterface->flushCommandList();
		#elif USE_GL4
				g_pRendererInterface->UnbindFrameBuffer();
		#endif
	}

	virtual HRESULT DeviceCreated() override
	{
        #if USE_D3D11
				g_pRendererInterface = new NVRHI::RendererInterfaceD3D11(&g_ErrorCallback, g_DeviceManager->GetImmediateContext());
                NvAPI_D3D_RegisterDevice(g_DeviceManager->GetDevice());
		#elif USE_D3D12
				g_pRendererInterface = new NVRHI::RendererInterfaceD3D12(&g_ErrorCallback, g_DeviceManager->GetDevice(), g_DeviceManager->GetDefaultQueue());
				g_pRendererInterface->disableDebugMessages(NVRHI::MessageCategories::UNUSED_CB);
                NvAPI_D3D_RegisterDevice(g_DeviceManager->GetDevice());
		#elif USE_GL4
				g_pRendererInterface = new NVRHI::RendererInterfaceOGL(&g_ErrorCallback);
				g_pRendererInterface->init();
		#endif
		m_RendererInterface = g_pRendererInterface;

#if USE_D3D12
		DxcHelper_Initialyze();
#endif
		AllocateResources();

		bool bSupported = false;

#if USE_D3D11
		ID3D11Device* pDevice = g_DeviceManager->GetDevice();
		// There is no way to explicitly check for Maxwell FastGS support except by trying to create such FastGS.
		// Test for FP16 atomics instead (which were introduced in the same GPU architecture as FastGS).
		if (NvAPI_D3D11_IsNvShaderExtnOpCodeSupported(pDevice, NV_EXTN_OP_FP16_ATOMIC, &bSupported) == NVAPI_OK && bSupported)
#elif USE_D3D12
		ID3D12Device* pDevice = g_DeviceManager->GetDevice();
		if (NvAPI_D3D12_IsNvShaderExtnOpCodeSupported(pDevice, NV_EXTN_OP_FP16_ATOMIC, &bSupported) == NVAPI_OK && bSupported)
#endif
		{
			NV_QUERY_MODIFIED_W_SUPPORT_PARAMS modifiedWParams = { 0 };
			modifiedWParams.version = NV_QUERY_MODIFIED_W_SUPPORT_PARAMS_VER;
			NV_QUERY_SINGLE_PASS_STEREO_SUPPORT_PARAMS stereoParams = { 0 };
			stereoParams.version = NV_QUERY_SINGLE_PASS_STEREO_SUPPORT_PARAMS_VER;
			NV_QUERY_MULTIVIEW_SUPPORT_PARAMS queryMultiViewParams = { 0 };
			queryMultiViewParams.version = NV_QUERY_MULTIVIEW_SUPPORT_PARAMS_VER;

#if USE_D3D11
			if (NvAPI_D3D_QueryMultiViewSupport(pDevice, &queryMultiViewParams) == NVAPI_OK && queryMultiViewParams.bMultiViewSupported)
			{
				g_featureLevel = NvFeatureLevel::TURING_GPU;
			}
			else if (NvAPI_D3D_QueryModifiedWSupport(pDevice, &modifiedWParams) == NVAPI_OK && modifiedWParams.bModifiedWSupported &&
				NvAPI_D3D_QuerySinglePassStereoSupport(pDevice, &stereoParams) == NVAPI_OK && stereoParams.bSinglePassStereoSupported)
			{
				g_featureLevel = NvFeatureLevel::PASCAL_GPU;

				if (!g_suppressMessageBox)
					MessageBox(g_DeviceManager->GetHWND(), L"The GPU or the installed driver doesn't support Turing VR hardware features (Multi View Rendering). "
						L"Sample modes using these features will use software emulation with regular geometry shaders.", g_WindowTitle, MB_ICONEXCLAMATION);
			}
#elif USE_D3D12
			D3D12_FEATURE_DATA_D3D12_OPTIONS3 DeviceOptions3;
			memset(&DeviceOptions3, 0, sizeof(DeviceOptions3));
			if (SUCCEEDED(pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3, &DeviceOptions3, sizeof(DeviceOptions3))))
			{
				g_ViewInstancingTier = DeviceOptions3.ViewInstancingTier;
			}

			if (NvAPI_D3D12_QueryModifiedWSupport(pDevice, &modifiedWParams) == NVAPI_OK && modifiedWParams.bModifiedWSupported &&
				g_ViewInstancingTier == D3D12_VIEW_INSTANCING_TIER_3)
			{
				g_featureLevel = NvFeatureLevel::TURING_GPU;
			}
			else if (NvAPI_D3D12_QueryModifiedWSupport(pDevice, &modifiedWParams) == NVAPI_OK && modifiedWParams.bModifiedWSupported &&
				NvAPI_D3D12_QuerySinglePassStereoSupport(pDevice, &stereoParams) == NVAPI_OK && stereoParams.bSinglePassStereoSupported)
				g_featureLevel = NvFeatureLevel::PASCAL_GPU;
#endif
		else
			{
				g_featureLevel = NvFeatureLevel::MAXWELL_GPU;

				if (!g_suppressMessageBox)
					MessageBox(g_DeviceManager->GetHWND(), L"The GPU or the installed driver doesn't support either Pascal or Turing VR hardware features (Lens Matched Shading, Single Pass Stereo and Multi View Rendering). "
						L"Sample modes using these features will use software emulation with regular geometry shaders.", g_WindowTitle, MB_ICONEXCLAMATION);
			}
		}
		else
		{
			g_featureLevel = NvFeatureLevel::GENERIC_DX11;

			if (!g_suppressMessageBox)
				MessageBox(g_DeviceManager->GetHWND(), L"The GPU or the installed driver doesn't support Maxwell, Pascal or Turing VR hardware features (Multi-Res Shading, Lens Matched Shading, Single Pass Stereo and Multi View Rendering). "
					L"Sample modes using these features will use software emulation with regular geometry shaders.", g_WindowTitle, MB_ICONEXCLAMATION);
		}

		{
			std::string rootPath = "shaders/";

			for (int depth = 0; depth < 5; depth++)
			{
				// Find any shader that is in the same folder with others
				std::string shaderPath = rootPath + "shader-common.hlsli";

				if (PathFileExistsA(shaderPath.c_str()))
					break;

				rootPath = "../" + rootPath;
			}

			m_pShaderFactory = new ShaderFactory(m_RendererInterface, rootPath, m_ContextMutex);
		}

		// Load shaders for the initial frame and to detect GPU features
		VerifyShaders();

		// Init shadow map
		m_shadowMap.Init(m_RendererInterface, makeint2(4096), NVRHI::Format::D32);

		// Start loading the scene
		LoadSceneAsync();

		// Init the camera
		m_camera.m_moveSpeed = 3.0f;
		m_camera.m_mbuttonActivate = Framework::MBUTTON_Left;
		ResetCamera();
		
		{
            unsigned int grayImage = 0xff808080;
            unsigned int blackImage = 0x00000000;
			
			NVRHI::TextureDesc textureDesc;
			textureDesc.format = NVRHI::Format::RGBA8_UNORM;
			textureDesc.width = 1;
			textureDesc.height = 1;
			textureDesc.mipLevels = 1;
            textureDesc.debugName = "GrayTexture";
            m_grayTexture = m_RendererInterface->createTexture(textureDesc, &grayImage);
            m_blackTexture = m_RendererInterface->createTexture(textureDesc, &blackImage);
		}

		memset(&g_ViewInstancingDesc, 0, sizeof(g_ViewInstancingDesc));
		g_ViewInstancingDesc.ViewInstanceCount = 2;
		g_ViewInstancingDesc.ViewInstanceLocations[1].ViewportArrayIndex = 1;
		g_ViewInstancingDesc.Flags = NVRHI::VIEW_INSTANCING_FLAG_NONE;

		g_bInitialized = true;

		return S_OK;
	}

	virtual void DeviceDestroyed() override
	{
		DeactivateVR();
		
		TwTerminate();

		for (auto pScene : m_scenes)
		{
			delete pScene;
		}
		m_scenes.clear();

		m_RendererInterface->destroyBuffer(m_pBufLineVertices);

		m_RendererInterface->destroySampler(m_pSsBilinearClamp);
		m_RendererInterface->destroySampler(m_pSsTrilinearRepeatAniso);
		m_RendererInterface->destroySampler(m_pSsPCF);

		m_RendererInterface->destroyTexture(m_rtSceneMSAA);
		m_RendererInterface->destroyTexture(m_rtMotionVectorsMSAA);
		m_RendererInterface->destroyTexture(m_rtNormalsMSAA);
		m_RendererInterface->destroyTexture(m_rtScene);
		m_RendererInterface->destroyTexture(m_rtScenePrev);
		m_RendererInterface->destroyTexture(m_dstSceneMSAA);
		m_RendererInterface->destroyTexture(m_rtUpscaled);
		m_shadowMap.Reset();

		m_RendererInterface->destroyConstantBuffer(m_cbFrame);
		m_RendererInterface->destroyConstantBuffer(m_cbVRData);

        m_RendererInterface->destroyTexture(m_grayTexture);
        m_RendererInterface->destroyTexture(m_blackTexture);

        m_RendererInterface->destroyPerformanceQuery(m_pqFrame);
        m_RendererInterface->destroyPerformanceQuery(m_pqRenderScene);
        m_RendererInterface->destroyPerformanceQuery(m_pqRenderShadowMap);
        m_RendererInterface->destroyPerformanceQuery(m_pqDrawSplits);
        m_RendererInterface->destroyPerformanceQuery(m_pqBlit);
        m_RendererInterface->destroyPerformanceQuery(m_pqSSAO);
        m_RendererInterface->destroyPerformanceQuery(m_pqTemporalAA);
        m_RendererInterface->destroyPerformanceQuery(m_pqDrawSafeZone);
        m_RendererInterface->destroyPerformanceQuery(m_pqFlattenImage);

		delete m_pShaderState;
		delete m_pShaderFactory;

#if USE_D3D12
		DxcHelper_Release();
#endif
		delete g_pRendererInterface;
		g_pRendererInterface = nullptr;
	}

	virtual void BackBufferResized(uint32_t width, uint32_t height, uint32_t sampleCount) override
	{
		(sampleCount);

		m_windowSize.x = width;
		m_windowSize.y = height;
		
		TwWindowSize(width, height);

		char buf[100];
		int x = width - 330;
		sprintf_s(buf, "Multi-Res position='%d 10'", x);
		TwDefine(buf);
		sprintf_s(buf, "LensMatchedShading position='%d 220'", x);
		TwDefine(buf);

		x = (width - 500) / 2;
		int y = (height - 120) / 2;
		sprintf_s(buf, "MessageBox size='500 120' position='%d %d'", x, y);
		TwDefine(buf);
	}



	void AllocateResources()
	{
		NVRHI::BufferDesc linesVertexBufferDesc;
		linesVertexBufferDesc.isVertexBuffer = true;
		linesVertexBufferDesc.isCPUWritable = true;
		linesVertexBufferDesc.byteSize = m_lineVerticesPerDraw * sizeof(LineVertex);
		m_pBufLineVertices = m_RendererInterface->createBuffer(linesVertexBufferDesc, nullptr);


		NVRHI::SamplerDesc samplerDesc;
		samplerDesc.minFilter = samplerDesc.magFilter = true;
		samplerDesc.wrapMode[0] = samplerDesc.wrapMode[1] = samplerDesc.wrapMode[2] = NVRHI::SamplerDesc::WRAP_MODE_WRAP;
		m_pSsBilinearClamp = m_RendererInterface->createSampler(samplerDesc);

		samplerDesc.mipFilter = true;
		samplerDesc.anisotropy = 16;
		m_pSsTrilinearRepeatAniso = m_RendererInterface->createSampler(samplerDesc);

		samplerDesc = NVRHI::SamplerDesc();
		samplerDesc.wrapMode[0] = samplerDesc.wrapMode[1] = samplerDesc.wrapMode[2] = NVRHI::SamplerDesc::WRAP_MODE_BORDER;
		samplerDesc.shadowCompare = true;
		samplerDesc.borderColor = NVRHI::Color(1.0f, 1.0f, 1.0f, 1.0f);
		m_pSsPCF = m_RendererInterface->createSampler(samplerDesc);

		// Init constant buffers
		m_cbFrame = m_RendererInterface->createConstantBuffer(NVRHI::ConstantBufferDesc(sizeof(CBFrame), nullptr), nullptr);
		m_cbVRData = m_RendererInterface->createConstantBuffer(NVRHI::ConstantBufferDesc(sizeof(CBVRData), nullptr), nullptr);

        // Create the perf queries
        m_pqFrame = m_RendererInterface->createPerformanceQuery("Frame");
        m_pqRenderScene = m_RendererInterface->createPerformanceQuery("RenderScene");
        m_pqRenderShadowMap = m_RendererInterface->createPerformanceQuery("RenderShadowMap");
        m_pqDrawSplits = m_RendererInterface->createPerformanceQuery("DrawSplits");
        m_pqBlit = m_RendererInterface->createPerformanceQuery("Blit");
        m_pqSSAO = m_RendererInterface->createPerformanceQuery("SSAO");
        m_pqTemporalAA = m_RendererInterface->createPerformanceQuery("TemporalAA");
        m_pqDrawSafeZone = m_RendererInterface->createPerformanceQuery("DrawSafeZone");
        m_pqFlattenImage = m_RendererInterface->createPerformanceQuery("FlattenImage");
	}

	void LoadSceneAsync()
	{
		for (auto pScene : m_scenes)
			delete pScene;

		m_scenes.clear();

		m_shadowMap.Invalidate();

		std::string rootPath = "media/";
		std::string scenePath;

		for (int depth = 0; depth < 5; depth++)
		{
			scenePath = rootPath + g_sceneName;

			if (PathFileExistsA(scenePath.c_str()))
				break;

			rootPath = "../" + rootPath;
		}

		m_bLoadingScene = true;
		memset(&m_SceneLoadingStats, 0, sizeof(Scene::LoadingStats));

		m_pSceneLoadingThread = new std::thread([this, rootPath, scenePath]() 
		{
			std::ifstream sceneFile(scenePath);

			Json::Reader reader;
			Json::Value root;
			bool parsingSuccessful = reader.parse(sceneFile, root, false);
			if (!parsingSuccessful)
			{
				ERR("Couldn't load the scene description (json) file");
				return;
			}

			concurrency::task_group taskGroup;
			for (UINT index = 0; index < root.size(); index++)
			{
				m_scenes.push_back(LoadObject(rootPath, root[index], taskGroup));
			}
			taskGroup.wait();

			m_bLoadingScene = false;
		});
	}

	Scene* LoadObject(const std::string& rootPath, Json::Value& node, concurrency::task_group& taskGroup)
	{
		InterlockedIncrement(&m_SceneLoadingStats.ObjectsTotal);

		Scene* object = new Scene();

		Json::Value& instances = node["instances"];
		for (UINT index = 0; index < instances.size(); index++)
		{
			Json::Value& instanceNode = instances[index];
			float4x4 matrix;
			for (UINT component = 0; component < 16; component++)
				matrix.m_data[component] = float(instanceNode[component].asDouble());
			object->AddInstance(matrix);
		}

		std::string fileName = rootPath + node["file"].asString();

		taskGroup.run([this, &taskGroup, fileName, object]()
		{
			if (FAILED(object->Load(fileName.c_str())))
			{
				ERR("Couldn't load scene file %s", fileName.c_str());
				return;
			}

			object->UpdateBounds();

			if (FAILED(object->InitResources(m_RendererInterface, m_SceneLoadingStats, taskGroup)))
			{
				ERR("Couldn't load the textures for scene file %s", fileName.c_str());
				return;
			}

			InterlockedIncrement(&m_SceneLoadingStats.ObjectsLoaded);
		});

		return object;
	}

	void VerifyShaders()
	{
		if (m_bCompilingShaders)
			return;

		if (m_pCompilingThread && !m_bCompilingShaders)
		{
			m_pCompilingThread->join();
			delete m_pCompilingThread;
			m_pCompilingThread = nullptr;
		}

		ShaderState::Effect effect;
		effect.value = 0;

		effect.projection =
			g_multiResEnabled ? uint(Nv::VR::Projection::MULTI_RES) :
			g_lensMatchedShadingEnabled ? uint(Nv::VR::Projection::LENS_MATCHED) :
			uint(Nv::VR::Projection::PLANAR);

		effect.stereoMode = uint(g_stereoMode);
		effect.featureLevel = uint(g_featureLevel);
		effect.msaaSampleCount = g_msaaSampleCount;
		effect.temporalAA = g_temporalAA;

		if (!m_pShaderState || effect.value != m_pShaderState->m_Effect.value)
		{
			delete m_pShaderState;
			m_pShaderState = nullptr;
			m_bCompilingShaders = true;

			m_pCompilingThread = new std::thread([this, effect]()
			{
				m_pShaderState = new ShaderState(m_pShaderFactory, m_RendererInterface, effect);
				m_bCompilingShaders = false;
			});
		}
	}

	void VerifyRenderTargetDims()
	{
		int2 requiredSceneSize;
		int2 requiredUpscaledSize;

		if (IsVRActive())
		{
			requiredUpscaledSize = m_vr->GetSwapChainSize();
		}
		else
		{
			requiredUpscaledSize = m_windowSize;
		}

		{
			Nv::VR::Float2 projectionSize;
			Nv::VR::Float2 scaledFlattenedSize = Nv::VR::Float2(requiredUpscaledSize.x * g_resolutionScale, requiredUpscaledSize.y * g_resolutionScale);

			if (IsVROrFakeVRActive())
				scaledFlattenedSize.x *= 0.5f;

			if (g_lensMatchedShadingEnabled)
			{
				projectionSize = CalculateProjectionSize(scaledFlattenedSize, g_lensMatchedConfig);
			}
			else if (g_multiResEnabled)
			{
				projectionSize = CalculateProjectionSize(scaledFlattenedSize, g_multiResConfig);
			}
			else
			{
				projectionSize = scaledFlattenedSize;
			}

			if (IsVROrFakeVRActive())
				projectionSize.x *= 2.f;

			requiredSceneSize = makeint2(int(projectionSize.x), int(projectionSize.y));
		}

		int samples = g_msaaSampleCount;
		NVRHI::Format::Enum rtSceneFormat = g_temporalAA ? NVRHI::Format::RGBA16_FLOAT : NVRHI::Format::SRGBA8_UNORM;
		NVRHI::Format::Enum depthFormat = NVRHI::Format::D32;

		bool recreate = false;
		if (!m_rtSceneMSAA || !m_rtUpscaled || !m_dstSceneMSAA || !m_rtScene)
		{
			recreate = true;
		}
		if (!recreate)
		{
			NVRHI::TextureDesc desc = m_RendererInterface->describeTexture(m_rtSceneMSAA);
			recreate = (desc.width != (uint32_t)requiredSceneSize.x || desc.height != (uint32_t)requiredSceneSize.y) ||
				desc.sampleCount != (uint32_t)samples;
		}
		if (!recreate)
		{
			NVRHI::TextureDesc desc = m_RendererInterface->describeTexture(m_rtUpscaled);
			recreate = desc.width != (uint32_t)requiredUpscaledSize.x || desc.height != (uint32_t)requiredUpscaledSize.y;
		}
		if (!recreate)
		{
			NVRHI::TextureDesc desc = m_RendererInterface->describeTexture(m_dstSceneMSAA);
			recreate = desc.format != depthFormat;
		}
		if (!recreate)
		{
			NVRHI::TextureDesc desc = m_RendererInterface->describeTexture(m_rtScene);
			recreate = desc.format != rtSceneFormat;
		}

		if (!recreate)
		{
			// All parameters match, no need to change anything
			return;
		}

		g_suppressTemporalAA = true;

		DESTROY_TEXTURE(m_rtSceneMSAA);
		DESTROY_TEXTURE(m_rtMotionVectorsMSAA);
		DESTROY_TEXTURE(m_rtNormalsMSAA);
		DESTROY_TEXTURE(m_rtScene);
		DESTROY_TEXTURE(m_rtScenePrev);
		DESTROY_TEXTURE(m_dstSceneMSAA);
		DESTROY_TEXTURE(m_rtUpscaled);

		NVRHI::TextureDesc textureDesc;
		textureDesc.width = requiredSceneSize.x;
		textureDesc.height = requiredSceneSize.y;
		textureDesc.isRenderTarget = true;
		textureDesc.sampleCount = samples;
		textureDesc.disableGPUsSync = true;
        textureDesc.useClearValue = true;
        textureDesc.clearValue = NVRHI::Color(0.f);

		textureDesc.format = NVRHI::Format::SRGBA8_UNORM;
        textureDesc.debugName = "SceneColorMSAA";
		m_rtSceneMSAA = m_RendererInterface->createTexture(textureDesc, NULL);

		textureDesc.format = NVRHI::Format::RG16_FLOAT;
        textureDesc.debugName = "SceneMotionVectorsMSAA";
        m_rtMotionVectorsMSAA = m_RendererInterface->createTexture(textureDesc, NULL);

		textureDesc.format = NVRHI::Format::RGBA8_SNORM;
        textureDesc.debugName = "SceneNormalsMSAA";
        m_rtNormalsMSAA = m_RendererInterface->createTexture(textureDesc, NULL);

		textureDesc.format = depthFormat;
        textureDesc.debugName = "SceneDepthMSAA";
        textureDesc.clearValue = NVRHI::Color(g_lensMatchedShadingEnabled ? 1.f : 0.f, 0.f, 0.f, 0.f);
        m_dstSceneMSAA = m_RendererInterface->createTexture(textureDesc, NULL);

		textureDesc.format = rtSceneFormat;
		textureDesc.isUAV = true;
		textureDesc.sampleCount = 1;
        textureDesc.debugName = "SceneResolved1";
        textureDesc.clearValue = NVRHI::Color(0.f);
        m_rtScene = m_RendererInterface->createTexture(textureDesc, NULL);
        textureDesc.debugName = "SceneResolved2";
        m_rtScenePrev = m_RendererInterface->createTexture(textureDesc, NULL);

		textureDesc.width = requiredUpscaledSize.x;
		textureDesc.height = requiredUpscaledSize.y;
		textureDesc.isUAV = false;
		textureDesc.format = NVRHI::Format::SRGBA8_UNORM;
        textureDesc.debugName = "SceneUpscaled";
        m_rtUpscaled = m_RendererInterface->createTexture(textureDesc, NULL);
	}
	

	void DrawSplits(const Nv::VR::Data& data, NVRHI::TextureHandle rt, int Width, int Height)
	{
		static const rgba s_rgbaSplits = { 0.0f, 1.0f, 1.0f, 1.0f };
		NVRHI::TextureDesc desc = m_RendererInterface->describeTexture(rt);
		int rtX = desc.width;
		int rtY = desc.height;

		auto ScreenToClip = [rtX, rtY](int x, int y)
		{
			return makepoint2(float(x) / float(rtX) * 2.f - 1.f, -float(y) / float(rtY) * 2.f + 1.f);
		};

		for (int x = 1; x < Width; x++)
		{
			for (int y = 1; y < Height; y++)
			{
				const Nv::VR::ScissorRect& scissor = data.Viewports.Scissors[y * Width + x];
				AddDebugLine(ScreenToClip(data.Viewports.BoundingRect.Left, scissor.Top), ScreenToClip(data.Viewports.BoundingRect.Right, scissor.Top), s_rgbaSplits);
				AddDebugLine(ScreenToClip(scissor.Left, data.Viewports.BoundingRect.Top), ScreenToClip(scissor.Left, data.Viewports.BoundingRect.Bottom), s_rgbaSplits);
			}
		}

		DrawDebugLines(rt);
	}

	void DrawSplits(NVRHI::TextureHandle rt)
	{
		int WidthOrHeight = g_multiResEnabled ? 3 : 2;

		if (g_drawMultiResSplits && g_multiResEnabled || g_drawLMSSplits && g_lensMatchedShadingEnabled)
		{
			PERF_SECTION(m_pqDrawSplits);

			if (IsVROrFakeVRActive())
			{
				DrawSplits(g_projectionDataVR[0], rt, WidthOrHeight, WidthOrHeight);
				DrawSplits(g_projectionDataVR[1], rt, WidthOrHeight, WidthOrHeight);
			}
			else
			{
				DrawSplits(g_projectionData, rt, WidthOrHeight, WidthOrHeight);
			}
		}
	}

	void AddDebugLine(point2_arg p0, point2_arg p1, rgba_arg rgba)
	{
		LineVertex verts[2] =
		{
			{ rgba,{ p0.x, p0.y, 0.0f, 1.0f }, },
			{ rgba,{ p1.x, p1.y, 0.0f, 1.0f }, },
		};
		m_lineVertices.insert(m_lineVertices.end(), &verts[0], &verts[dim(verts)]);
	}

	void DrawDebugLines(NVRHI::TextureHandle rt)
	{
		// Batch up into draw calls based on how many vertices the buffer can hold

		if (m_lineVertices.empty())
			return;
		
		NVRHI::TextureDesc desc = m_RendererInterface->describeTexture(rt);

		NVRHI::DrawCallState state;
		state.primType = NVRHI::PrimitiveType::LINE_LIST;
		state.VS.shader = m_pShaderState->m_pVsLines;
		state.PS.shader = m_pShaderState->m_pPsLines;
		state.inputLayout = m_pShaderState->m_pInputLayoutLines;
		state.renderState.targetCount = 1;
		state.renderState.targets[0] = rt;
		state.renderState.viewportCount = 1;
		state.renderState.viewports[0] = NVRHI::Viewport(float(desc.width), float(desc.height));
		state.renderState.depthStencilState.depthEnable = false;
		state.renderState.rasterState.cullMode = NVRHI::RasterState::CULL_NONE;
		state.vertexBufferCount = 1;
		state.vertexBuffers[0].buffer = m_pBufLineVertices;
		state.vertexBuffers[0].stride = sizeof(LineVertex);

		int numVerts = int(m_lineVertices.size());
		for (int baseVert = 0; baseVert < numVerts; baseVert += m_lineVerticesPerDraw)
		{
			int numVertsThisDraw = min(numVerts - baseVert, m_lineVerticesPerDraw);

			m_RendererInterface->writeBuffer(m_pBufLineVertices, &m_lineVertices[baseVert], numVertsThisDraw * sizeof(LineVertex));

			NVRHI::DrawArguments args;
			args.vertexCount = numVertsThisDraw;
			m_RendererInterface->draw(state, &args, 1);
		}

		m_lineVertices.clear();
	}


	void BindConstantBuffer(NVRHI::DrawCallState& state, uint32_t slot, NVRHI::ConstantBufferHandle buffer)
	{
		NVRHI::BindConstantBuffer(state.VS, slot, buffer);
		NVRHI::BindConstantBuffer(state.HS, slot, buffer);
		NVRHI::BindConstantBuffer(state.DS, slot, buffer);
		NVRHI::BindConstantBuffer(state.GS, slot, buffer);
		NVRHI::BindConstantBuffer(state.PS, slot, buffer);
	}

	void Blit(NVRHI::TextureHandle pSource, NVRHI::TextureHandle pDest)
	{
		NVRHI::DrawCallState state;

		state.primType = NVRHI::PrimitiveType::TRIANGLE_STRIP;
		state.VS.shader = m_pShaderState->m_pVsFullscreen;
		state.PS.shader = m_pShaderState->m_pPsBlit;

		NVRHI::TextureDesc desc = m_RendererInterface->describeTexture(pDest);

		state.renderState.targetCount = 1;
		state.renderState.targets[0] = pDest;
		state.renderState.viewportCount = 1;
		state.renderState.viewports[0] = NVRHI::Viewport(float(desc.width), float(desc.height));
		state.renderState.depthStencilState.depthEnable = false;
		state.renderState.rasterState.cullMode = NVRHI::RasterState::CULL_NONE;

		NVRHI::BindTexture(state.PS, 0, pSource);
        NVRHI::BindSampler(state.PS, 0, m_pSsBilinearClamp);

		NVRHI::DrawArguments args;
		args.vertexCount = 4;
		m_RendererInterface->draw(state, &args, 1);
	}

	void DrawFullscreen(NVRHI::DrawCallState& state)
	{
		state.primType = NVRHI::PrimitiveType::TRIANGLE_STRIP;
		state.VS.shader = m_pShaderState->m_pVsFullscreen;
		state.renderState.rasterState.cullMode = NVRHI::RasterState::CULL_NONE;

		NVRHI::DrawArguments args;
		args.vertexCount = 4;
		m_RendererInterface->draw(state, &args, 1);
	}
};



StereoMode VRWorksSample::GetCurrentStereoMode()
{
	StereoMode output = StereoMode::NONE;

	if (g_instancedStereoEnabled)
		output = StereoMode::INSTANCED;

	if (g_singlePassStereoEnabled)
		output = StereoMode::SINGLE_PASS;

	if (g_MultiViewEnabled)
		output = StereoMode::MULTI_VIEW;

	return output;
}

void VRWorksSample::ResetCamera()
{
	m_camera.LookAt(
				makepoint3(-6.8f, 1.6f, 13.8f),
				makepoint3(-5.8f, 1.6f, 13.8f));
}



void VRWorksSample::OnRender(NVRHI::TextureHandle& mainRenderTarget)
{
	if (!IsVROrFakeVRActive() && (g_singlePassStereoEnabled || g_instancedStereoEnabled || g_MultiViewEnabled))
	{
		g_singlePassStereoEnabled = false;
		g_instancedStereoEnabled = false;
		g_MultiViewEnabled = false;
		g_message_to_show = err_StereoNotEnabled;
	}
	if (g_lensMatchedShadingEnabled && g_multiResEnabled)
	{
		g_multiResEnabled = false;
		g_message_to_show = err_MRSPlusLMSNotSuppored;
	}
	if (g_singlePassStereoEnabled && g_multiResEnabled)
	{
		g_multiResEnabled = false;
		g_message_to_show = err_MRSPlusSPSNotSupported;
	}
	if (g_singlePassStereoEnabled && g_convergenceAngle != 0)
	{
		g_singlePassStereoEnabled = false;
		g_message_to_show = err_ConvergencePlusSPSNotSupported;
	}
	if (g_MultiViewEnabled && g_multiResEnabled)
	{
		g_multiResEnabled = false;
		g_message_to_show = err_MRSPlusMultiViewNotSupported;
	}
	if (IsVRActive() && g_convergenceAngle != 0)
	{
		g_convergenceAngle = 0;
		g_message_to_show = err_ConvergenceNotSupported;
	}

	if (g_instancedStereoEnabled && (g_multiResEnabled || g_lensMatchedShadingEnabled))
	{
		g_multiResEnabled = false;
		g_lensMatchedShadingEnabled = false;
		g_message_to_show = err_InstancedPlanarOnly;
	}

#if USE_D3D12
	if (g_MultiViewEnabled && g_lensMatchedShadingEnabled)
	{
		g_lensMatchedShadingEnabled = false;
		g_message_to_show = err_LMSPlusViewInstancingNotSupported;
	}	
	
	if (g_MultiViewEnabled && g_ViewInstancingTier == D3D12_VIEW_INSTANCING_TIER_NOT_SUPPORTED)
	{
		g_MultiViewEnabled = false;
		g_message_to_show = err_ViewInstancingNotSupported;
	}	
	
	if (g_MultiViewEnabled)
	{
		HRESULT hr = D3D12EnableExperimentalFeatures(1, &D3D12ExperimentalShaderModels, NULL, NULL);
		if (FAILED(hr))
		{
			g_MultiViewEnabled = false;
			g_message_to_show = err_ExperimentalShaderModelsNotSupported;
		}
	}	
#endif

	g_stereoMode = GetCurrentStereoMode();

	VerifyShaders();

	// Make sure that the shader factory doesn't call any NvAPI functions on the device/context while rendering happens.
	// These functions are not free-threaded.
	m_ContextMutex.lock();

	if (IsVRActive())
	{
		// Force camera pitch to zero, so pitch is only from HMD orientation
		m_camera.m_pitch = 0.0f;
		m_camera.UpdateOrientation();

		// Retrieve head-tracking information from the VR API
		m_vr->AcquirePose();
		m_vr->SetOculusPerfHudMode(g_perfHudMode);
	}

	NVRHI::TextureHandle pRtFinal = nullptr;

	if (m_pSceneLoadingThread && !m_bLoadingScene)
	{
		m_pSceneLoadingThread->join();
		delete m_pSceneLoadingThread;
		m_pSceneLoadingThread = nullptr;

		for (auto scene : m_scenes)
			scene->FinalizeInit();
	}

	if (m_pShaderState && !m_bLoadingScene)
	{
		PERF_SECTION(m_pqFrame);

		RenderShadowMap();

		VerifyRenderTargetDims();

		RecalcVRData();

		RenderScene();

		pRtFinal = (g_msaaSampleCount > 1 || g_temporalAA) ? m_rtScene : m_rtSceneMSAA;

		DrawSplits(pRtFinal);

		if (g_multiResEnabled && !g_disableUpscale || g_lensMatchedShadingEnabled && !g_disableUnwarp)
		{
			FlattenImage();
			pRtFinal = m_rtUpscaled;
		}

		ProcessSaveCommand(pRtFinal);
	}

	if (pRtFinal && IsVRActive())
	{
		PresentToHMD(pRtFinal);
	}

	if (pRtFinal)
	{
		PERF_SECTION(m_pqBlit);
		Blit(pRtFinal, mainRenderTarget);
	}

	// No vsync in VR mode; assume the VR API will take care of that
	bool vsync = (g_vsync || m_bLoadingScene) && !IsVRActive();
	g_DeviceManager->SetVsyncEnabled(vsync);

	m_ContextMutex.unlock();
}

void VRWorksSample::PresentToHMD(NVRHI::TextureHandle pRT)
{
	bool vrDisplayLost = false;
	switch (m_vr->GetApi())
	{
	case VrApi::OculusVR:
	{
		// Blit the frame to the Oculus swap texture set (would have rendered directly to it,
		// but then it seems you can't blit from it to the back buffer - we just get black).
		// this was the case in the 0.8 SDK, but not sure if it's still true in 1.3

		uint32_t currentIndex = m_vr->GetCurrentSwapChainBuffer();
		NVRHI::TextureHandle oculusTexture = m_oculusSwapTextures[currentIndex];

		NVRHI::DrawCallState state;
		state.renderState.targetCount = 1;
		state.renderState.targets[0] = oculusTexture;

		NVRHI::TextureDesc desc = m_RendererInterface->describeTexture(pRT);

		// make sure that pRtFinal is transferred 1:1 onto the swap chain
		int2 swapChainSize = m_vr->GetSwapChainSize();
		state.renderState.viewportCount = 1;
		state.renderState.viewports[0] = NVRHI::Viewport(float(swapChainSize.x), float(swapChainSize.y));
		state.renderState.scissorRects[0] = NVRHI::Rect(swapChainSize.x, swapChainSize.y);
		state.renderState.depthStencilState.depthEnable = false;

		NVRHI::BindTexture(state.PS, 0, pRT);
		NVRHI::BindSampler(state.PS, 0, m_pSsBilinearClamp);
		state.PS.shader = m_pShaderState->m_pPsBlit;

		DrawFullscreen(state);

#if USE_D3D12
		g_pRendererInterface->requireTextureState(oculusTexture, 0, 0, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		g_pRendererInterface->commitBarriers();
		g_pRendererInterface->flushCommandList();
#endif

		VrResult result = m_vr->PresentOculusVR();

		if (result == VrResult::DisplayLost)
		{
			// Display was powered off, or something. Set flag to turn off VR mode next frame.
			vrDisplayLost = true;
		}

		break;
	}
	case VrApi::OpenVR:
	{
		NVRHI::TextureHandle target = pRT;

		if (pRT != m_rtUpscaled && m_RendererInterface->describeTexture(pRT).format == NVRHI::Format::RGBA16_FLOAT)
		{
			// If the final image is in RGBA16F format, copy it into m_rtUpscaled to avoid a DX runtime error when OpenVR calls CopySubresourceRegion.
			Blit(pRT, m_rtUpscaled);
			target = m_rtUpscaled;
		}

		// Submit the frame to the OpenVR runtime
#if USE_D3D11
		ID3D11Resource* pTextureResource = g_pRendererInterface->getResourceForTexture(target);
		m_vr->PresentOpenVR(pTextureResource);
#endif
		break;
	}
	}

	// Turn off VR mode if we lost the headset. 
	if (vrDisplayLost)
	{
		g_message_to_show = err_VrDisplayLost;
		DeactivateVR();
	}
}

void VRWorksSample::CalcEyeMatrices(bool vrActive, int eye, float4x4* ref_worldToClip, point3* ref_cameraPos, affine3* ref_eyeToWorld)
{
	if (vrActive)
	{
		// Figure out the camera pose for this eye, from the VR tracking system
		affine3 eyeToCamera = m_vr->GetEyeToOriginTransform(eye);

		// Calculate world-to-clip matrix for this eye
		affine3 eyeToWorld = eyeToCamera * m_camera.m_viewToWorld;
		affine3 worldToEye = transpose(eyeToWorld);

		if (ref_eyeToWorld)
			*ref_eyeToWorld = eyeToWorld;

		if (ref_worldToClip)
			*ref_worldToClip = affineToHomogeneous(worldToEye) * m_matProjVR[eye];

		if (ref_cameraPos)
		{
			ref_cameraPos->x = eyeToWorld.m_translation.x;
			ref_cameraPos->y = eyeToWorld.m_translation.y;
			ref_cameraPos->z = eyeToWorld.m_translation.z;
		}
	}
	else
	{
		float4x4 matProjVR;

		// tangents for DK2
		float UpTan = 1.3316f;
		float DownTan = 1.3316f;
		float LeftTan = eye == 0 ? 1.0586f : 1.0924f;
		float RightTan = eye != 0 ? 1.0586f : 1.0924f;

		matProjVR = perspProjD3DStyleReverse(-LeftTan, RightTan, -DownTan, UpTan, g_zNear);

		float shift = 0.0320000015f;
		float3 right = m_camera.m_viewToWorld.m_linear[0] * shift;

		point3 cameraPos = m_camera.m_pos;
		if (eye == 0)
		{
			cameraPos -= right;
		}
		else
		{
			cameraPos += right;
		}

		affine3 cameraRot;
		float convergenceAngle = clamp(g_convergenceAngle, -45.0f, 45.0f) * pi / 180.0f;
		if (eye == 0)
		{
			cameraRot = rotation(makefloat3(0, convergenceAngle, 0));
		}
		else
		{
			cameraRot = rotation(makefloat3(0, -convergenceAngle, 0));
		}

		affine3 viewToWorld = cameraRot * m_camera.m_viewToWorld;
		viewToWorld.m_translation = makefloat3(cameraPos.x, cameraPos.y, cameraPos.z);
		affine3 worldToView = transpose(viewToWorld);

		if (ref_eyeToWorld)
			*ref_eyeToWorld = viewToWorld;

		if (ref_worldToClip)
			*ref_worldToClip = affineToHomogeneous(worldToView) * matProjVR;

		if (ref_cameraPos)
			*ref_cameraPos = cameraPos;
	}
}

void VRWorksSample::FrustumCull(const float4x4& matWorldToClip)
{
	Frustum frustum(matWorldToClip);

	int maxObjects = (int)m_scenes.size();
	for (int i = 0; i < maxObjects; ++i)
	{
		Scene* pScene = m_scenes[i];
		pScene->FrustumCull(frustum, g_frustumCulling);
	}
}

void VRWorksSample::FrustumCullStereo(const float4x4& matWorldToClipLeft, const float4x4& matWorldToClipRight)
{
	Frustum frustumLeft(matWorldToClipLeft);
	Frustum frustumRight(matWorldToClipRight);

	if (g_convergenceAngle == 0)
	{
		Frustum frustum = frustumLeft;
		frustum.planes[Frustum::RIGHT_PLANE] = frustumRight.planes[Frustum::RIGHT_PLANE];

		int maxObjects = (int)m_scenes.size();
		for (int i = 0; i < maxObjects; ++i)
		{
			Scene* pScene = m_scenes[i];
			pScene->FrustumCull(frustum, g_frustumCulling);
		}
	}
	else
	{
		int maxObjects = (int)m_scenes.size();
		for (int i = 0; i < maxObjects; ++i)
		{
			Scene* pScene = m_scenes[i];
			pScene->FrustumCullStereo(frustumLeft, frustumRight, g_frustumCulling);
		}
	}
}



void VRWorksSample::DrawRepeatedScene(NVRHI::DrawCallState& drawCallState)
{
	drawCallState.VS.shader = m_pShaderState->m_pVsWorld;
	drawCallState.PS.shader = m_pShaderState->m_pGsWorld;
	drawCallState.renderState.depthStencilState.depthFunc = NVRHI::DepthStencilState::COMPARISON_GREATER_EQUAL;

	for (int i = 0; i < g_repeatRenderingCount; ++i)
	{
		DrawObjects(drawCallState, m_pShaderState->m_pPsForward, m_pShaderState->m_pPsForward, false);
	}
}

void VRWorksSample::DrawObjects(NVRHI::DrawCallState& drawCallState, NVRHI::ShaderHandle pPs, NVRHI::ShaderHandle pPsAlphaTest, bool bShadowMapPass)
{
	int maxObjects = (int)m_scenes.size();
	for (int i = 0; i < maxObjects; ++i)
	{
		Scene* pScene = m_scenes[i];
		DrawObjectInstances(drawCallState, pPs, pPsAlphaTest, pScene, bShadowMapPass);
	}
}

void VRWorksSample::DrawObjectInstances(NVRHI::DrawCallState& drawCallState, NVRHI::ShaderHandle pPs, NVRHI::ShaderHandle pPsAlphaTest, Scene* pScene, bool bShadowMapPass)
{
	drawCallState.primType = NVRHI::PrimitiveType::TRIANGLE_LIST;
	drawCallState.inputLayout = m_pShaderState->m_pInputLayout;

	{
		UINT offset;
		drawCallState.vertexBufferCount = 1;
		drawCallState.vertexBuffers[0].buffer = pScene->GetVertexBuffer_rhi(0, offset);
		drawCallState.vertexBuffers[0].stride = sizeof(Vertex);
		drawCallState.indexBuffer = pScene->GetIndexBuffer_rhi(0, offset);
		drawCallState.indexBufferFormat = NVRHI::Format::R32_UINT;
	}

	UINT numMeshes = pScene->GetMeshesNum();
	const Material* lastMaterial = NULL;
    std::vector<NVRHI::DrawArguments> drawCalls;

    if (pScene->IsSingleInstanceBuffer())
        NVRHI::BindBuffer(drawCallState.VS, 0, pScene->GetInstanceBuffer_rhi(0));

	for (UINT i = 0; i < numMeshes; ++i)
	{
		const Material* material = pScene->GetMaterial(i);

		if (material == NULL)
			continue;

		UINT numInstances = pScene->GetCulledInstancesNum(i);

		if (numInstances == 0)
			continue;

        if(!pScene->IsSingleInstanceBuffer())
		    NVRHI::BindBuffer(drawCallState.VS, 0, pScene->GetInstanceBuffer_rhi(i));

		if (material != lastMaterial)
		{
            if (drawCalls.size() > 0)
            {
                m_RendererInterface->drawIndexed(drawCallState, &drawCalls[0], uint32_t(drawCalls.size()));
                drawCalls.clear();
            }

			drawCallState.PS.textureBindingCount = 0;

            // bind all mip levels: ~0u
            NVRHI::BindTexture(drawCallState.PS, TEX_DIFFUSE, material->m_DiffuseTexture ? material->m_DiffuseTexture->Texture : m_grayTexture, false, NVRHI::Format::UNKNOWN, ~0u);

			if (!bShadowMapPass)
			{
                // bind all mip levels: ~0u
                NVRHI::BindTexture(drawCallState.PS, TEX_NORMALS, material->m_NormalsTexture ? material->m_NormalsTexture->Texture : m_blackTexture, false, NVRHI::Format::UNKNOWN, ~0u);
                // specular is not used by the shader
				// NVRHI::BindTexture(drawCallState.PS, TEX_SPECULAR, material->m_SpecularTexture ? material->m_SpecularTexture->Texture : m_blackTexture, false, NVRHI::Format::UNKNOWN, ~0u);
				NVRHI::BindTexture(drawCallState.PS, TEX_EMISSIVE, material->m_EmissiveTexture ? material->m_EmissiveTexture->Texture : m_blackTexture, false, NVRHI::Format::UNKNOWN, ~0u);
				NVRHI::BindTexture(drawCallState.PS, TEX_SHADOW, m_shadowMap.m_shadowTexture);
			}

			if (lastMaterial == NULL || material->m_AlphaTested != lastMaterial->m_AlphaTested)
			{
				if (material->m_AlphaTested)
				{
					drawCallState.renderState.rasterState.cullMode = NVRHI::RasterState::CULL_NONE;
					drawCallState.renderState.rasterState.frontCounterClockwise = true;
					drawCallState.renderState.blendState.alphaToCoverage = true;
					drawCallState.PS.shader = pPsAlphaTest;
				}
				else
				{
					drawCallState.renderState.rasterState.cullMode = NVRHI::RasterState::CULL_BACK;
					drawCallState.renderState.rasterState.frontCounterClockwise = true;
					drawCallState.renderState.blendState.alphaToCoverage = false;
					drawCallState.PS.shader = pPs;
				}
			}

			lastMaterial = material;
		}
		
		UINT indexOffset, vertexOffset;
		pScene->GetIndexBuffer_rhi(i, indexOffset);
		pScene->GetVertexBuffer_rhi(i, vertexOffset);
		
		if (g_instancedStereoEnabled)
		{
			numInstances *= 2;
		}
		int indexCount = pScene->GetMeshIndicesNum(i);

		NVRHI::DrawArguments args;
		args.vertexCount = indexCount;
		args.instanceCount = numInstances;
		args.startVertexLocation = vertexOffset;
		args.startIndexLocation = indexOffset;

        if (pScene->IsSingleInstanceBuffer())
            drawCalls.push_back(args);
        else
		    m_RendererInterface->drawIndexed(drawCallState, &args, 1);

		int triangleCount = indexCount * numInstances / 3;
		g_triangleCounter += triangleCount;
	}

    if (drawCalls.size() > 0)
    {
        m_RendererInterface->drawIndexed(drawCallState, &drawCalls[0], uint32_t(drawCalls.size()));
        drawCalls.clear();
    }
}

void VRWorksSample::RenderShadowMap()
{
	NVRHI::DrawCallState drawCallState;

	// Calculate shadow map matrices
	float4x4 matWorldToClipPrev = m_shadowMap.m_matWorldToClip;
	m_shadowMap.m_vecLight = g_vecDirectionalLight;

	// hardcode some limits in order not to calculate the min/max from all objects' instances
	m_shadowMap.m_boundsScene.m_mins.x = -20.f;
	m_shadowMap.m_boundsScene.m_mins.y = -10.f;
	m_shadowMap.m_boundsScene.m_mins.z = 0.f;
	m_shadowMap.m_boundsScene.m_maxs.x = 10.f;
	m_shadowMap.m_boundsScene.m_maxs.y = 20.f;
	m_shadowMap.m_boundsScene.m_maxs.z = 30.f;
	m_shadowMap.UpdateMatrix();

	// Only re-render if the matrix changed, to save perf
	if (all(isnear(m_shadowMap.m_matWorldToClip, matWorldToClipPrev)))
	{
		return;
	}

	PERF_SECTION(m_pqRenderShadowMap);

	drawCallState.renderState.depthStencilState.depthFunc = NVRHI::DepthStencilState::COMPARISON_LESS_EQUAL;

	// Set up constant buffer for rendering to shadow map
	CBFrame cbFrame =
	{
		m_shadowMap.m_matWorldToClip,
	};
	m_RendererInterface->writeConstantBuffer(m_cbFrame, &cbFrame, sizeof(cbFrame));
	
	drawCallState.VS.constantBufferBindingCount = 1;
	drawCallState.VS.constantBuffers[0].buffer = m_cbFrame;
	drawCallState.VS.constantBuffers[0].slot = CB_FRAME;

	m_RendererInterface->clearTextureFloat(m_shadowMap.m_shadowTexture, NVRHI::Color(1.0f, 0.0f, 0.0f, 0.0f));
	
	drawCallState.renderState.viewportCount = 1;
	drawCallState.renderState.viewports[0] = NVRHI::Viewport((float)m_shadowMap.m_width, (float)m_shadowMap.m_height);
	drawCallState.renderState.depthTarget = m_shadowMap.m_shadowTexture;

	drawCallState.VS.shader = m_pShaderState->m_pVsShadow;
	drawCallState.PS.textureSamplerBindingCount = 1;
	drawCallState.PS.textureSamplers[0].sampler = m_pSsTrilinearRepeatAniso;
	drawCallState.PS.textureSamplers[0].slot = SAMP_DEFAULT;

	FrustumCull(cbFrame.m_matWorldToClip);
	DrawObjects(drawCallState, nullptr, m_pShaderState->m_pPsShadowAlphaTest, true);
}

void VRWorksSample::RenderScene()
{
	CalculateProjectionMatrices();

	float2 taaViewportOffset = makefloat2(0.f);
	if (g_temporalAA)
	{
		NVRHI::TextureHandle tmp = m_rtScene;
		m_rtScene = m_rtScenePrev;
		m_rtScenePrev = tmp;

		const float2 offsets[] = {
			makefloat2(0.0625f, -0.1875f), makefloat2(-0.0625f, 0.1875f), makefloat2(0.3125f, 0.0625f), makefloat2(-0.1875f, -0.3125f),
			makefloat2(-0.3125f, 0.3125f), makefloat2(-0.4375f, 0.0625f), makefloat2(0.1875f, 0.4375f), makefloat2(0.4375f, -0.4375f)
		};

		static int frameIndex = 0;
		frameIndex++;
		if (frameIndex >= dim(offsets))
			frameIndex = 0;

		taaViewportOffset = offsets[frameIndex];
	}

	#pragma region Shaders / state setup

	NVRHI::TextureDesc rtSceneDesc = m_RendererInterface->describeTexture(m_rtScene);

	// Reset counter before drawing anything
	g_triangleCounter = 0;

	// Set up constant buffer for rendering scene
	CBFrame cbFrame;
	memset(&cbFrame, 0, sizeof(cbFrame));
	cbFrame.m_matWorldToClipPrev = m_matWorldToClipPrev;
	cbFrame.m_matWorldToClipPrevR = m_matWorldToClipPrevR;
	cbFrame.m_matWorldToUvzwShadow = m_shadowMap.m_matWorldToUvzw;
	cbFrame.m_matWorldToUvzShadowNormal = makefloat3x4(m_shadowMap.m_matWorldToUvzNormal);
	cbFrame.m_vecDirectionalLight = g_vecDirectionalLight;
	cbFrame.m_rgbDirectionalLight = g_rgbDirectionalLight;
	cbFrame.m_dimsShadowMap = makefloat2(float(m_shadowMap.m_width), float(m_shadowMap.m_height));
	cbFrame.m_normalOffsetShadow = g_normalOffsetShadow;
	cbFrame.m_shadowSharpening = 1.0f;
	cbFrame.m_screenSize = makefloat4(float(rtSceneDesc.width), float(rtSceneDesc.height), 1.0f / float(rtSceneDesc.width), 1.0f / float(rtSceneDesc.height));
	cbFrame.m_temporalAAClampingFactor = 1.0f;
	cbFrame.m_temporalAANewFrameWeight = g_suppressTemporalAA ? 1.f : 0.1f;
	cbFrame.m_textureLodBias = g_temporalAA ? -0.5f : 0.f;
	cbFrame.m_randomOffset = makefloat2(float(rand()), float(rand()));
	CBVRData cbVRData;

	NVRHI::DrawCallState drawCallState;
	BindConstantBuffer(drawCallState, CB_FRAME, m_cbFrame);
	BindConstantBuffer(drawCallState, CB_VR, m_cbVRData);
	
	float depthNear = 1.f; // 1/W projection
	float depthFar = 0.f;

	// For lens-matched shading, clear with the near plane to create a no-draw zone around the LMS boundary octagon.
	// Before rendering, the octagon will be drawn, overwriting the central area with the warped far plane depth (DrawSafeZone).
	m_RendererInterface->clearTextureFloat(m_dstSceneMSAA, NVRHI::Color(g_lensMatchedShadingEnabled ? depthNear : depthFar, 0, 0, 0));

	m_RendererInterface->clearTextureFloat(m_rtSceneMSAA, NVRHI::Color(0));
	m_RendererInterface->clearTextureFloat(m_rtNormalsMSAA, NVRHI::Color(0));

	if (g_temporalAA)
	{
		drawCallState.renderState.targetCount = 3;
		drawCallState.renderState.targets[0] = m_rtSceneMSAA;
		drawCallState.renderState.targets[1] = m_rtNormalsMSAA;
		drawCallState.renderState.targets[2] = m_rtMotionVectorsMSAA;
		drawCallState.renderState.depthTarget = m_dstSceneMSAA;		
	}
	else
	{
		drawCallState.renderState.targetCount = 2;
		drawCallState.renderState.targets[0] = m_rtSceneMSAA;
		drawCallState.renderState.targets[1] = m_rtNormalsMSAA;
		drawCallState.renderState.depthTarget = m_dstSceneMSAA;
	}

	NVRHI::BindSampler(drawCallState.PS, SAMP_DEFAULT, m_pSsTrilinearRepeatAniso);
	NVRHI::BindSampler(drawCallState.PS, SAMP_SHADOW, m_pSsPCF);

	drawCallState.GS.shader = m_pShaderState->m_pGsWorld;

	#pragma endregion

	#pragma region Regular Rendering

	if (!IsVROrFakeVRActive())
	{
		PERF_SECTION(m_pqRenderScene);

		SetVRCBDataMono(&cbVRData);
		SetVRViewportsMono(drawCallState, taaViewportOffset);

		m_RendererInterface->writeConstantBuffer(m_cbVRData, &cbVRData, sizeof(cbVRData));

		if (g_lensMatchedShadingEnabled)
		{
			EnableLensMatchedShading(&g_lensMatchedConfig);
			DrawSafeZone(drawCallState);
		}

		cbFrame.m_matWorldToClip = m_camera.m_worldToClip;

		FrustumCull(cbFrame.m_matWorldToClip);

		m_RendererInterface->writeConstantBuffer(m_cbFrame, &cbFrame, sizeof(cbFrame));

		m_matWorldToClipPrev = cbFrame.m_matWorldToClip;
		m_matWorldToClipPrevR = cbFrame.m_matWorldToClipR;

		DrawRepeatedScene(drawCallState);
	}

	#pragma endregion

	#pragma region VR/FakeVR Rendering

	else
	{
        PERF_SECTION(m_pqRenderScene);

        if (g_stereoMode == StereoMode::NONE)
		{ // render two eyes sequentially
			for (int eye = 0; eye < 2; ++eye)
			{
				float4x4 worldToClip;
				point3 cameraPos;
				CalcEyeMatrices(IsVRActive(), eye, &worldToClip, &cameraPos, nullptr);

				SetVRCBDataSingleEye(eye, &cbVRData);
				SetVRViewportsSingleEye(drawCallState, eye, taaViewportOffset);
				
				m_RendererInterface->writeConstantBuffer(m_cbVRData, &cbVRData, sizeof(cbVRData));

				if (g_lensMatchedShadingEnabled)
				{
					EnableLensMatchedShading(&g_lensMatchedConfigVR[eye]);
					DrawSafeZone(drawCallState);
				}

				cbFrame.m_matWorldToClip = worldToClip;
				
				if (eye == 0)
				{
					cbFrame.m_matWorldToClipPrev = m_matWorldToClipPrev;
					m_matWorldToClipPrev = cbFrame.m_matWorldToClip;
				}
				else
				{
					cbFrame.m_matWorldToClipPrev = m_matWorldToClipPrevR;
					m_matWorldToClipPrevR = cbFrame.m_matWorldToClip;
				}

				m_RendererInterface->writeConstantBuffer(m_cbFrame, &cbFrame, sizeof(cbFrame));

				FrustumCull(cbFrame.m_matWorldToClip);

				DrawRepeatedScene(drawCallState);
			}
		}
		else if (g_singlePassStereoEnabled || g_instancedStereoEnabled || g_MultiViewEnabled)
		{
			if (g_lensMatchedShadingEnabled && (g_singlePassStereoEnabled || g_MultiViewEnabled))
			{
				for (int eye = 0; eye < 2; ++eye)
				{
					SetVRCBDataSingleEye(eye, &cbVRData);
					SetVRViewportsSingleEye(drawCallState, eye, taaViewportOffset);
					m_RendererInterface->writeConstantBuffer(m_cbVRData, &cbVRData, sizeof(cbVRData));

					EnableLensMatchedShading(&g_lensMatchedConfigVR[eye]);
					DrawSafeZone(drawCallState);
				}

                if (g_singlePassStereoEnabled && IsSinglePassStereoSupported())
                {
                    m_RendererInterface->setSinglePassStereoMode(true, 0, true);
                }

				if (g_MultiViewEnabled && IsMultiViewSupported())
				{
					m_RendererInterface->setMultiViewMode(true, &g_ViewInstancingDesc);
				}

				EnableLensMatchedShadingWithSinglePassStereo(&g_lensMatchedConfigVR[0], &g_lensMatchedConfigVR[1]);
			}
			else if (g_singlePassStereoEnabled && IsSinglePassStereoSupported())
			{
				m_RendererInterface->setSinglePassStereoMode(true, 0, true);
			}
			else if (g_MultiViewEnabled && IsMultiViewSupported())
			{
				m_RendererInterface->setMultiViewMode(true, &g_ViewInstancingDesc);
			}

			SetVRViewportsStereo(drawCallState, taaViewportOffset);

			SetVRCBDataStereo(&cbVRData);
			m_RendererInterface->writeConstantBuffer(m_cbVRData, &cbVRData, sizeof(cbVRData));

			float4x4 worldToClip;
			point3 camPos;

			CalcEyeMatrices(IsVRActive(), 0, &worldToClip, &camPos, nullptr);
			cbFrame.m_matWorldToClip = worldToClip;
			
			CalcEyeMatrices(IsVRActive(), 1, &worldToClip, &camPos, nullptr);
			cbFrame.m_matWorldToClipR = worldToClip;

			m_RendererInterface->writeConstantBuffer(m_cbFrame, &cbFrame, sizeof(cbFrame));

			m_matWorldToClipPrev = cbFrame.m_matWorldToClip;
			m_matWorldToClipPrevR = cbFrame.m_matWorldToClipR;

			FrustumCullStereo(cbFrame.m_matWorldToClip, cbFrame.m_matWorldToClipR);

			DrawRepeatedScene(drawCallState);
		}
	}

	#pragma endregion
	
	// Disable multiview
	if (g_singlePassStereoEnabled && IsSinglePassStereoSupported())
	{
		m_RendererInterface->setSinglePassStereoMode(false, 0, false);
	}

	// Disable multiview (Multi-View)
	if (g_MultiViewEnabled && IsMultiViewSupported())
	{
		m_RendererInterface->setMultiViewMode(false, NULL);
	}

	// Disable lens-matched shading
	if (g_lensMatchedShadingEnabled)
	{
		DisableLensMatchedShading();
	}

	if (g_enableSSAO)
	{
		PERF_SECTION(m_pqSSAO);

		NVRHI::DrawCallState ssaoDrawCallState;

		NVRHI::TextureDesc sceneDesc = m_RendererInterface->describeTexture(m_rtSceneMSAA);

		ssaoDrawCallState.renderState.targetCount = 1;
		ssaoDrawCallState.renderState.targets[0] = m_rtSceneMSAA;
		ssaoDrawCallState.renderState.viewportCount = 1;
		ssaoDrawCallState.renderState.viewports[0] = NVRHI::Viewport(float(sceneDesc.width), (float)sceneDesc.height);
		ssaoDrawCallState.renderState.depthStencilState.depthEnable = false;
		ssaoDrawCallState.PS.shader = m_pShaderState->m_pPsAo;
		ssaoDrawCallState.PS.constantBufferBindingCount = 2;
		ssaoDrawCallState.PS.constantBuffers[0].buffer = m_cbFrame;
		ssaoDrawCallState.PS.constantBuffers[0].slot = CB_FRAME;
		ssaoDrawCallState.PS.constantBuffers[1].buffer = m_cbVRData;
		ssaoDrawCallState.PS.constantBuffers[1].slot = CB_VR;
		NVRHI::BindTexture(ssaoDrawCallState.PS, 0, m_dstSceneMSAA);
		NVRHI::BindTexture(ssaoDrawCallState.PS, 1, m_rtNormalsMSAA);

		ssaoDrawCallState.renderState.blendState.blendEnable[0] = true;
		ssaoDrawCallState.renderState.blendState.srcBlend[0] = NVRHI::BlendState::BLEND_DEST_COLOR;
		ssaoDrawCallState.renderState.blendState.destBlend[0] = NVRHI::BlendState::BLEND_ZERO;
		ssaoDrawCallState.renderState.blendState.blendOp[0] = NVRHI::BlendState::BLEND_OP_ADD;
		ssaoDrawCallState.renderState.blendState.srcBlendAlpha[0] = NVRHI::BlendState::BLEND_DEST_ALPHA;
		ssaoDrawCallState.renderState.blendState.destBlendAlpha[0] = NVRHI::BlendState::BLEND_ZERO;
		ssaoDrawCallState.renderState.blendState.blendOpAlpha[0] = NVRHI::BlendState::BLEND_OP_ADD;

		if (IsVROrFakeVRActive())
		{
			for (int eye = 0; eye < 2; eye++)
			{
				SetVRCBDataSingleEye(eye, &cbVRData);

				affine3 eyeToWorld;
				CalcEyeMatrices(IsVRActive(), eye, nullptr, nullptr, &eyeToWorld);
				cbFrame.m_matWorldToViewNormal = transpose(affineToHomogeneous(eyeToWorld));

				if (IsVRActive())
				{
					cbFrame.m_matProjInv = inverse(m_matProjVR[eye]);
				}
				else
				{
					cbFrame.m_matProjInv = inverse(m_camera.m_projection);
				}

				m_RendererInterface->writeConstantBuffer(m_cbVRData, &cbVRData, sizeof(CBVRData));
				m_RendererInterface->writeConstantBuffer(m_cbFrame, &cbFrame, sizeof(CBFrame));

				// use the planar viewport to cover all cases
				SetVRViewports(ssaoDrawCallState, g_planarDataVR[eye].Viewports, taaViewportOffset);

				DrawFullscreen(ssaoDrawCallState);
			}
		}
		else
		{
			cbFrame.m_matWorldToViewNormal = transpose(affineToHomogeneous(m_camera.m_viewToWorld));
			cbFrame.m_matProjInv = inverse(m_camera.m_projection);
			
			m_RendererInterface->writeConstantBuffer(m_cbFrame, &cbFrame, sizeof(CBFrame));

			DrawFullscreen(ssaoDrawCallState);
		}
	}

	if (g_temporalAA)
	{
		PERF_SECTION(m_pqTemporalAA);

		if (g_multiResEnabled || g_lensMatchedShadingEnabled)
		{
			m_RendererInterface->clearTextureFloat(m_rtScene, NVRHI::Color(0));
		}

		NVRHI::DispatchState taaDispatchState;
		
		NVRHI::BindTexture(taaDispatchState, 0, m_rtScene, true);

		NVRHI::BindTexture(taaDispatchState, 0, m_rtSceneMSAA);
		NVRHI::BindTexture(taaDispatchState, 1, m_rtMotionVectorsMSAA);
		NVRHI::BindTexture(taaDispatchState, 2, m_rtScenePrev);
		
		taaDispatchState.shader = m_pShaderState->m_pCsTemporalAA;
		NVRHI::BindSampler(taaDispatchState, 0, m_pSsBilinearClamp);

		NVRHI::BindConstantBuffer(taaDispatchState, CB_FRAME, m_cbFrame);
		NVRHI::BindConstantBuffer(taaDispatchState, CB_VR, m_cbVRData);

		if (IsVROrFakeVRActive() && (g_lensMatchedShadingEnabled || g_multiResEnabled))
		{
			// Two dispatches, one per eye
			for (int eye = 0; eye < 2; eye++)
			{
				SetVRCBDataSingleEye(eye, &cbVRData);

				m_RendererInterface->writeConstantBuffer(m_cbVRData, &cbVRData, sizeof(CBVRData));

				int2 viewportSize = makeint2(rtSceneDesc.width / 2, rtSceneDesc.height);

				cbFrame.m_viewportOrigin = makefloat2(float(viewportSize.x) * eye, 0);
				m_RendererInterface->writeConstantBuffer(m_cbFrame, &cbFrame, sizeof(CBFrame));
				
				m_RendererInterface->dispatch(taaDispatchState, (viewportSize.x + 15) / 16, (viewportSize.y + 15) / 16, 1);
			}
		}
		else
		{
			cbFrame.m_viewportOrigin = makefloat2(0, 0);
			m_RendererInterface->writeConstantBuffer(m_cbFrame, &cbFrame, sizeof(CBFrame));

			m_RendererInterface->dispatch(taaDispatchState, (rtSceneDesc.width + 15) / 16, (rtSceneDesc.height + 15) / 16, 1);
		}
	}
	else if(g_msaaSampleCount > 1)
	{
		// Resolve the MSAA buffer
		m_RendererInterface->resolveTexture(m_rtScene, m_rtSceneMSAA, NVRHI::Format::SRGBA8_UNORM);
	}
		
	g_suppressTemporalAA = false;
}



void VRWorksSample::RecalcVRData()
{
	g_projectionDataPrev = g_projectionData;
	g_projectionDataVRPrev[0] = g_projectionDataVR[0];
	g_projectionDataVRPrev[1] = g_projectionDataVR[1];

	NVRHI::TextureDesc rtSceneDesc = m_RendererInterface->describeTexture(m_rtScene);
	NVRHI::TextureDesc rtUpscaledDesc = m_RendererInterface->describeTexture(m_rtUpscaled);

	Nv::VR::Viewport boundingBox(0, 0, rtSceneDesc.width, rtSceneDesc.height);
	Nv::VR::Viewport boundingBoxVR[2] =
	{
		Nv::VR::Viewport(0.f,                   0.f, boundingBox.Width / 2, boundingBox.Height),
		Nv::VR::Viewport(boundingBox.Width / 2, 0.f, boundingBox.Width / 2, boundingBox.Height)
	};

	Nv::VR::Float2 flattenedSize = Nv::VR::Float2(float(rtUpscaledDesc.width), float(rtUpscaledDesc.height));
	flattenedSize.x *= g_resolutionScale;
	flattenedSize.y *= g_resolutionScale;

	Nv::VR::Float2 flattenedSizeVR = flattenedSize;
	flattenedSizeVR.x *= 0.5f;

	// Planar
	Nv::VR::Planar::Configuration dummyConfig;
	CalculateViewportsAndBufferData(flattenedSize, boundingBox, dummyConfig, g_planarData);
	CalculateViewportsAndBufferData(flattenedSizeVR, boundingBoxVR[0], dummyConfig, g_planarDataVR[0]);
	CalculateViewportsAndBufferData(flattenedSizeVR, boundingBoxVR[1], dummyConfig, g_planarDataVR[1]);

	if (g_multiResEnabled)
	{
		// Multi-Res Shading
		CalculateViewportsAndBufferData(flattenedSize, boundingBox, g_multiResConfig, g_projectionData);
		CalculateMirroredConfig(g_multiResConfigVR[0] = g_multiResConfig, g_multiResConfigVR[1]);
		CalculateViewportsAndBufferData(flattenedSizeVR, boundingBoxVR[0], g_multiResConfigVR[0], g_projectionDataVR[0]);
		CalculateViewportsAndBufferData(flattenedSizeVR, boundingBoxVR[1], g_multiResConfigVR[1], g_projectionDataVR[1]);
	}
	else if (g_lensMatchedShadingEnabled)
	{
		// Lens-Matched Shading
		CalculateViewportsAndBufferData(flattenedSize, boundingBox, g_lensMatchedConfig, g_projectionData);
		CalculateMirroredConfig(g_lensMatchedConfigVR[0] = g_lensMatchedConfig, g_lensMatchedConfigVR[1]);
		CalculateViewportsAndBufferData(flattenedSizeVR, boundingBoxVR[0], g_lensMatchedConfigVR[0], g_projectionDataVR[0]);
		CalculateViewportsAndBufferData(flattenedSizeVR, boundingBoxVR[1], g_lensMatchedConfigVR[1], g_projectionDataVR[1]);
	}
	else
	{
		g_projectionData = g_planarData;
		g_projectionDataVR[0] = g_planarDataVR[0];
		g_projectionDataVR[1] = g_planarDataVR[1];
	}

#if 0
	// Test the MapClipToWindow / MapWindowToClip functions
	POINT cursorPos;
	if (GetCursorPos(&cursorPos) && ScreenToClient(g_DeviceManager->GetHWND(), &cursorPos))
	{
		Float2 windowPos{ float(cursorPos.x), float(cursorPos.y) };
		Float2 clipPos = MapWindowToClip(g_lensMatchedData, windowPos);
		Float2 reconstructedWindowPos = MapClipToWindow(g_lensMatchedData, clipPos);
		ASSERT_ERR(abs(windowPos.x - reconstructedWindowPos.x) < 0.001f && abs(windowPos.y - reconstructedWindowPos.y) < 0.001f);
	}
#endif
}

void VRWorksSample::SetVRCBData(const Nv::VR::Data& data, const Nv::VR::Data& dataR, const Nv::VR::Data& dataPrev, const Nv::VR::Data& dataPrevR, const int2_arg dimsRt, CBVRData* ref_cbData)
{
	ASSERT_ERR(all(dimsRt > 0));
	ASSERT_ERR(ref_cbData);

	ref_cbData->m_vrFastGSCBData = data.FastGsCbData;
	ref_cbData->m_vrRemapCBData = data.RemapCbData;

	ref_cbData->m_vrFastGSCBDataR = dataR.FastGsCbData;
	ref_cbData->m_vrRemapCBDataR = dataR.RemapCbData;

	ref_cbData->m_vrRemapCBDataPrev = dataPrev.RemapCbData;
	ref_cbData->m_vrRemapCBDataPrevR = dataPrevR.RemapCbData;
}

void VRWorksSample::SetVRViewports(NVRHI::DrawCallState& state, const Nv::VR::ProjectionViewports& viewports, float2 viewportOffset)
{
	state.renderState.rasterState.scissorEnable = true;
	state.renderState.viewportCount = viewports.NumViewports;

	for (int i = 0; i < viewports.NumViewports; ++i)
	{
		state.renderState.viewports[i] = NVRHI::Viewport(
			viewports.Viewports[i].TopLeftX + viewportOffset.x,
			viewports.Viewports[i].TopLeftX + viewportOffset.x + viewports.Viewports[i].Width,
			viewports.Viewports[i].TopLeftY + viewportOffset.y,
			viewports.Viewports[i].TopLeftY + viewportOffset.y + viewports.Viewports[i].Height,
			viewports.Viewports[i].MinDepth,
			viewports.Viewports[i].MaxDepth);

		state.renderState.scissorRects[i] = NVRHI::Rect(
			viewports.Scissors[i].Left,
			viewports.Scissors[i].Right,
			viewports.Scissors[i].Top,
			viewports.Scissors[i].Bottom);
	}
}

void VRWorksSample::SetVRCBDataMono(CBVRData* ref_cbData)
{
	NVRHI::TextureDesc desc = m_RendererInterface->describeTexture(m_rtScene);
	SetVRCBData(g_projectionData, g_projectionData, g_projectionDataPrev, g_projectionDataPrev, makeint2(desc.width, desc.height), ref_cbData);
}

void VRWorksSample::SetVRCBDataSingleEye(int eye, CBVRData* ref_cbData)
{
	NVRHI::TextureDesc desc = m_RendererInterface->describeTexture(m_rtScene);
	SetVRCBData(g_projectionDataVR[eye], g_projectionDataVR[eye], g_projectionDataVRPrev[eye], g_projectionDataVRPrev[eye], makeint2(desc.width, desc.height), ref_cbData);
}

void VRWorksSample::SetVRCBDataStereo(CBVRData* ref_cbData)
{
	NVRHI::TextureDesc desc = m_RendererInterface->describeTexture(m_rtScene);
	SetVRCBData(g_projectionDataVR[0], g_projectionDataVR[1], g_projectionDataVRPrev[0], g_projectionDataVRPrev[1], makeint2(desc.width, desc.height), ref_cbData);
}

void VRWorksSample::SetVRViewportsMono(NVRHI::DrawCallState& state, float2 viewportOffset)
{
	SetVRViewports(state, g_projectionData.Viewports, viewportOffset);
}

void VRWorksSample::SetVRViewportsSingleEye(NVRHI::DrawCallState& state, int eye, float2 viewportOffset)
{
	SetVRViewports(state, g_projectionDataVR[eye].Viewports, viewportOffset);
}

void VRWorksSample::SetVRViewportsStereo(NVRHI::DrawCallState& state, float2 viewportOffset)
{
	if (g_instancedStereoEnabled)
	{
		SetVRViewports(state, g_planarData.Viewports, viewportOffset);
	}
	else
	{
		Nv::VR::ProjectionViewports stereoViewports;
		Nv::VR::ProjectionViewports::Merge(g_projectionDataVR[0].Viewports, g_projectionDataVR[1].Viewports, stereoViewports);
		SetVRViewports(state, stereoViewports, viewportOffset);
	}
}

void VRWorksSample::EnableLensMatchedShading(const Nv::VR::LensMatched::Configuration* config)
{
	if (!IsLensMatchedShadingSupported() || IsMultiViewEmulationEnabled())
		return;
	
	float fA[4] = { -config->WarpLeft, config->WarpRight, -config->WarpLeft, config->WarpRight };
	float fB[4] = { config->WarpUp, config->WarpUp, -config->WarpDown, -config->WarpDown };

	m_RendererInterface->setModifiedWMode(true, 4, fA, fB);
}

void VRWorksSample::EnableLensMatchedShadingWithSinglePassStereo(const Nv::VR::LensMatched::Configuration* leftConfig, const Nv::VR::LensMatched::Configuration* rightConfig)
{
	if (!IsLensMatchedShadingSupported() || IsMultiViewEmulationEnabled())
		return;

	float fA[8] = { 
		-leftConfig->WarpLeft, leftConfig->WarpRight, -leftConfig->WarpLeft, leftConfig->WarpRight,
		-rightConfig->WarpLeft, rightConfig->WarpRight, -rightConfig->WarpLeft, rightConfig->WarpRight };

	float fB[8] = {
		leftConfig->WarpUp, leftConfig->WarpUp, -leftConfig->WarpDown, -leftConfig->WarpDown,
		rightConfig->WarpUp, rightConfig->WarpUp, -rightConfig->WarpDown, -rightConfig->WarpDown };
	
	m_RendererInterface->setModifiedWMode(true, 8, fA, fB);
}

void VRWorksSample::DisableLensMatchedShading()
{
	if (!IsLensMatchedShadingSupported() || IsMultiViewEmulationEnabled())
		return;

	m_RendererInterface->setModifiedWMode(false, 0, nullptr, nullptr);
}

void VRWorksSample::DrawSafeZone(const NVRHI::DrawCallState& drawCallState)
{
	PERF_SECTION(m_pqDrawSafeZone);

	NVRHI::DrawCallState state = drawCallState;
	state.inputLayout = nullptr;
	state.indexBuffer = nullptr;
	state.vertexBufferCount = 0;
	state.VS.shader = m_pShaderState->m_pVsSafeZone;
	state.GS.shader = m_pShaderState->m_pGsSafeZone;
	state.PS.shader = nullptr;
	state.primType = NVRHI::PrimitiveType::TRIANGLE_LIST;
	state.renderState.depthStencilState.depthEnable = true;
	state.renderState.depthStencilState.depthFunc = NVRHI::DepthStencilState::COMPARISON_ALWAYS;
	state.renderState.targetCount = 0;
	state.renderState.rasterState.frontCounterClockwise = true;

	NVRHI::DrawArguments args;
	args.vertexCount = 6;
	m_RendererInterface->draw(state, &args, 1);
}

void VRWorksSample::FlattenImage()
{
	PERF_SECTION(m_pqFlattenImage);

	m_RendererInterface->clearTextureFloat(m_rtUpscaled, NVRHI::Color(0.f));

	NVRHI::DrawCallState state;
	state.renderState.targetCount = 1;
	state.renderState.targets[0] = m_rtUpscaled;
	state.renderState.depthStencilState.depthEnable = false;
	state.PS.shader = m_pShaderState->m_pPsFlatten;
	if (g_temporalAA || g_msaaSampleCount > 1)
		BindTexture(state.PS, 0, m_rtScene);
	else
		BindTexture(state.PS, 0, m_rtSceneMSAA);
	BindSampler(state.PS, 0, m_pSsBilinearClamp);
	BindConstantBuffer(state, CB_FRAME, m_cbFrame);
	BindConstantBuffer(state, CB_VR, m_cbVRData);
	state.renderState.viewportCount = 1;

	NVRHI::TextureDesc desc = m_RendererInterface->describeTexture(m_rtUpscaled);
	state.renderState.viewports[0] = NVRHI::Viewport(float(desc.width), float(desc.height));

	if (!IsVROrFakeVRActive())
	{
		CBVRData cbVR;
		SetVRCBDataMono(&cbVR);
		m_RendererInterface->writeConstantBuffer(m_cbVRData, &cbVR, sizeof(cbVR));

		DrawFullscreen(state);
	}
	else
	{
		for (int eye = 0; eye < 2; ++eye)
		{
			CBVRData cbVR;
			SetVRCBDataSingleEye(eye, &cbVR);
			m_RendererInterface->writeConstantBuffer(m_cbVRData, &cbVR, sizeof(cbVR));

			float width = float(desc.width) * 0.5f;
			state.renderState.viewports[0] = NVRHI::Viewport(width * eye, width * (eye + 1), 0.f, float(desc.height), 0.f, 1.f);

			DrawFullscreen(state);
		}
	}	
}

void VRWorksSample::CalculateProjectionMatrices()
{
	if (IsVRActive())
	{
		// Set new projection matrices
		for (int i = 0; i < 2; ++i)
		{
			m_matProjVR[i] = m_vr->GetReverseProjectionMatrix(i, g_zNear);
		}
	}
	else
	{
		NVRHI::TextureDesc rtUpscaledDesc = m_RendererInterface->describeTexture(m_rtUpscaled);

		// Update projection matrix for new aspect ratio
		float fov = 1.0f;
		float aspect = float(rtUpscaledDesc.width) / float(rtUpscaledDesc.height);

		m_camera.SetReverseProjection(fov, aspect, g_zNear);
	}
}

void VRWorksSample::TryActivateVR()
{
	if(!IsVRActive())
	{
		VrSystem* pVR = nullptr;
#if USE_D3D11
		switch (VrSystem::CreateD3D11(g_DeviceManager->GetDevice(), &pVR))
#elif USE_D3D12
		switch (VrSystem::CreateD3D12(g_DeviceManager->GetDXGIAdapter(), g_DeviceManager->GetDefaultQueue(), &pVR))
#endif
		{
		case VrResult::OK:
			m_vr = std::unique_ptr<VrSystem>(pVR);
			CreateVRSwapChainTextures();
			g_DeviceManager->SetRenderWhileMinimized(true);
			break;
		case VrResult::WrongAdapter:
			g_message_to_show = err_VrDisplayWrongAdapter;
			break;
		default:
			g_message_to_show = err_NoVrHeadset;
			break;
		}
	}
}

void VRWorksSample::DeactivateVR()
{
	ReleaseVRSwapChainTextures();

	g_DeviceManager->SetRenderWhileMinimized(false);

	m_vr.reset();

	if (!IsVROrFakeVRActive())
	{
		g_instancedStereoEnabled = false;
		g_singlePassStereoEnabled = false;
		g_MultiViewEnabled = false;
	}
}

void VRWorksSample::CreateVRSwapChainTextures()
{
	if (!m_vr)
		return;

	ReleaseVRSwapChainTextures();

	int swapLength = m_vr->GetSwapChainBufferCount();
	m_oculusSwapTextures.resize(swapLength);
	for (int nTexture = 0; nTexture < swapLength; nTexture++)
	{
#if USE_D3D11
		ID3D11Texture2D* swapTex = m_vr->GetSwapChainBufferD3D11(nTexture);
		m_oculusSwapTextures[nTexture] = g_pRendererInterface->getHandleForTexture(swapTex, NVRHI::Format::SRGBA8_UNORM);
#elif USE_D3D12
		ID3D12Resource* swapTex = m_vr->GetSwapChainBufferD3D12(nTexture);
		m_oculusSwapTextures[nTexture] = g_pRendererInterface->getHandleForTexture(swapTex, NVRHI::Format::SRGBA8_UNORM);
#endif
	}
}

void VRWorksSample::ReleaseVRSwapChainTextures()
{
	m_oculusSwapTextures.clear();
}

void WriteBMPToFile(
	const byte4 * pPixels,
	int2_arg dims,
	const char* path)
{
	ASSERT_ERR(pPixels);
	ASSERT_ERR(all(dims > 0));

	FILE * pFile = nullptr;
	if (fopen_s(&pFile, path, "wb") != 0)
		return;

	// Compose the .bmp headers
	BITMAPFILEHEADER bfh =
	{
		0x4d42,		// "BM"
		0, 0, 0,
		sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER),
	};
	BITMAPINFOHEADER bih =
	{
		sizeof(BITMAPINFOHEADER),
		dims.x, -dims.y,	// Negative height makes it go top-down
		1, 32, BI_RGB,
	};

	fwrite(&bfh, sizeof(bfh), 1, pFile);
	fwrite(&bih, sizeof(bih), 1, pFile);

	for (int i = 0, c = dims.x * dims.y; i < c; ++i)
	{
		byte4 rgba = pPixels[i];
		byte4 bgra = { rgba.b, rgba.g, rgba.r, rgba.a };
		fwrite(&bgra, sizeof(bgra), 1, pFile);
	}

	fclose(pFile);
}


void VRWorksSample::ProcessSaveCommand(NVRHI::TextureHandle pRtFinal)
{
	if (g_captureFrame <= 0)
		return;

	if (g_captureFrame > 1)
	{
		g_captureFrame -= 1;
		return;
	}
	
	g_captureFrame = 0;

	if (pRtFinal != m_rtUpscaled)
	{
		Blit(pRtFinal, m_rtUpscaled);
	}

	NVRHI::TextureDesc desc = m_RendererInterface->describeTexture(m_rtUpscaled);
	byte4* pBitmap = new byte4[desc.width * desc.height];

	if (!m_RendererInterface->readTexture(m_rtUpscaled, pBitmap, desc.width * 4))
	{
		delete[] pBitmap;
		return;
	}
	
	WriteBMPToFile(pBitmap, makeint2(desc.width, desc.height), g_captureFileName.c_str());

	delete[] pBitmap;

	ExitProcess(0);
}


class AntTweakBarVisualController : public IVisualController
{
private:
	VRWorksSample* m_sample;

public:
	AntTweakBarVisualController(VRWorksSample* sample) :
		m_sample(sample)
	{
	}

	void ShowErrorMessage()
	{
		int message = g_message_to_show;
		g_message_to_show = err_none;
		switch (message)
		{
		case err_none: break;
		case err_MRSPlusLMSNotSuppored:
			TwDefine("MessageBox/Line1 label='Only multi-res or Lens Matched Shading can be enabled at same time.'");
			TwDefine("MessageBox/Line2 label='Disabling mutires-res rendering.'");
			TwDefine("MessageBox visible=true");
			break;
		case err_MRSPlusSPSNotSupported:
			TwDefine("MessageBox/Line1 label='Multi-res is not compatible with Single Pass Stereo.'");
			TwDefine("MessageBox/Line2 label='Disabling Multi-res.'");
			TwDefine("MessageBox visible=true");
			break;
		case err_ConvergencePlusSPSNotSupported:
			TwDefine("MessageBox/Line1 label='Please set Eye Convergence to 0 before enabling Single Pass Stereo.'");
			TwDefine("MessageBox/Line2 label='Disabling Single Pass Stereo.'");
			TwDefine("MessageBox visible=true");
			break;
		case err_ConvergenceNotSupported:
			TwDefine("MessageBox/Line1 label='Customized Eye Convergence is not supported.'");
			TwDefine("MessageBox/Line2 label='Setting Eye Convergence to 0.'");
			TwDefine("MessageBox visible=true");
			break;
		case err_MRSPlusMultiViewNotSupported:
#if USE_D3D11
			TwDefine("MessageBox/Line1 label='Multi-res is not compatible with Multi View Rendering.'");
#elif USE_D3D12
			TwDefine("MessageBox/Line1 label='Multi-res is not compatible with View Instancing.'");
#endif
			TwDefine("MessageBox/Line2 label='Disabling Multi-res.'");
			TwDefine("MessageBox visible=true");
			break;
		case err_LMSPlusViewInstancingNotSupported:
			TwDefine("MessageBox/Line1 label='Lens Matched Shading is temporarily not compatible with View '");
			TwDefine("MessageBox/Line2 label='Instancing. Disabling Lens Matched Shading.'");
			TwDefine("MessageBox visible=true");
			break;
		case err_ViewInstancingNotSupported:
			TwDefine("MessageBox/Line1 label='View Instancing is not supported.'");
			TwDefine("MessageBox/Line2 label='Please update your windows to Win10 RS3 or higher.'");
			TwDefine("MessageBox visible=true");
			break;
		case err_ExperimentalShaderModelsNotSupported:
			TwDefine("MessageBox/Line1 label='Experimental Shader Models are not supported.'");
			TwDefine("MessageBox/Line2 label='Please enable windows developer mode and restart this sample.'");
			TwDefine("MessageBox visible=true");
			break;
		case err_StereoNotEnabled:
			TwDefine("MessageBox/Line1 label='Please enable stereo before trying to change stereo rendering mode.'");
			TwDefine("MessageBox/Line2 label=' '");
			TwDefine("MessageBox visible=true");
			break;
		case err_InstancedPlanarOnly:
			TwDefine("MessageBox/Line1 label='Instanced Stereo mode is incompatible with either Multi-Res Shading'");
			TwDefine("MessageBox/Line2 label='or Lens Matched Shading. Disabling both.'");
			TwDefine("MessageBox visible=true");
			break;
		case err_NoVrHeadset:
#if USE_D3D11
			TwDefine("MessageBox/Line1 label='Unable to activate an HMD device using Oculus API or OpenVR.'");
			TwDefine("MessageBox/Line2 label=' '");
#elif USE_D3D12
			TwDefine("MessageBox/Line1 label='Unable to activate an HMD device using Oculus API.'");
			TwDefine("MessageBox/Line2 label='Please note that OpenVR is unsupported on DX12.'");
#endif
			TwDefine("MessageBox visible=true");
			break;
		case err_VrDisplayLost:
			TwDefine("MessageBox/Line1 label='The VR headset has been disconnected. VR mode is deactivated.'");
			TwDefine("MessageBox/Line2 label=' '");
			TwDefine("MessageBox visible=true");
			break;
		case err_VrDisplayWrongAdapter:
			TwDefine("MessageBox/Line1 label='The VR headset is not connected to the GPU used by the application.'");
			TwDefine("MessageBox/Line2 label='Please relaunch the application to activate VR mode.'");
			TwDefine("MessageBox visible=true");
			break;
		}
	}

	virtual LRESULT MsgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override
	{
		if (g_message_to_show)
			ShowErrorMessage();

		if ((g_drawUI && !m_sample->m_bLoadingScene) || uMsg == WM_KEYDOWN || uMsg == WM_CHAR)
			if (TwEventWin(hWnd, uMsg, wParam, lParam))
			{
				return 0; // Event has been handled by AntTweakBar
			}

		return 1;
	}

	void RenderText()
	{
		const char* text = "Loading the scene, please wait...";
		int width = 0, height = 0;
		TwMeasureTextLine(text, &width, &height);
		TwBeginText((m_sample->m_windowSize.x - width) / 2, (m_sample->m_windowSize.y - height) / 2, 0, 0);
		TwAddTextLine(text, ~0u, 0);
		char buf[100];
		sprintf_s(buf, "Objects: %d/%d, Textures: %d/%d",
			m_sample->m_SceneLoadingStats.ObjectsLoaded, m_sample->m_SceneLoadingStats.ObjectsTotal,
			m_sample->m_SceneLoadingStats.TexturesLoaded, m_sample->m_SceneLoadingStats.TexturesTotal);
		TwAddTextLine(buf, ~0u, 0);
		TwEndText();
	}

	virtual void Render(RenderTargetView RTV) override
	{
#if USE_D3D12
		TwSetD3D12RenderTargetView((void*)RTV.ptr);
#else
		(void)RTV;
#endif

		if (g_drawUI)
		{
			if (m_sample->m_bLoadingScene)
			{
				RenderText();
			}
			else
			{
				CHECK_WARN(TwDraw());
			}
		}
	}

	virtual HRESULT DeviceCreated() override
	{
#if USE_D3D11
		TwInit(TW_DIRECT3D11, g_DeviceManager->GetDevice());
#elif USE_D3D12
		TwD3D12DeviceInfo info;
		info.Device = g_DeviceManager->GetDevice();
		info.DefaultCommandQueue = g_DeviceManager->GetDefaultQueue();
		info.RenderTargetFormat = SWAP_CHAIN_FORMAT;
		info.RenderTargetSampleCount = 1;
		info.RenderTargetSampleQuality = 0;
		info.UploadBufferSize = 1024 * 1024;
		TwInit(TW_DIRECT3D12, &info);
#elif USE_GL4
		TwInit(TW_OPENGL_CORE, nullptr);
#endif
		InitDialogs();
		return S_OK;
	}

	virtual void DeviceDestroyed() override
	{
		TwTerminate();
	}

	virtual void BackBufferResized(uint32_t width, uint32_t height, uint32_t sampleCount) override
	{
		(void)sampleCount;
		TwWindowSize(width, height);
	}

	static void TW_CALL SetSceneCB(const void *value, void *userData)
	{
		int valueInt = *(const int*)value;

		switch (valueInt)
		{
		case 0:
			g_sceneName = "sponza.json";
			g_sceneIndex = 0;
			break;
		case 1:
			g_sceneName = "san-miguel.json";
			g_sceneIndex = 1;
			break;
		}

		VRWorksSample* sample = (VRWorksSample*)userData;
		sample->LoadSceneAsync();
		sample->ResetCamera();
	}

	static void TW_CALL GetSceneCB(void *value, void *)
	{
		*(int*)value = g_sceneIndex;
	}

	void InitDialogs()
	{
		// Automatically use the biggest font size
		TwDefine("GLOBAL fontsize=3 fontresizable=false mouserepeat=false");

		#pragma region FPS

		// Create bar for FPS display
		TwBar * pTwBarFPS = TwNewBar("FPS");
		TwDefine("FPS position='10 10' size='320 170' valueswidth=80 refresh=0.5");
		TwAddVarCB(
				pTwBarFPS, "FPS", TW_TYPE_FLOAT,
				nullptr,
				[](void * value, void *) { 
					*(float *)value = 1.0f / float(g_DeviceManager->GetAverageFrameTime());
				},
				nullptr,
				"precision=1");
		TwAddVarCB(
				pTwBarFPS, "CPU frame time (ms)", TW_TYPE_FLOAT,
				nullptr,
				[](void * value, void *) { 
					*(float *)value = 1000.0f * float(g_DeviceManager->GetAverageFrameTime());
				},
				nullptr,
				"precision=2");

        // Perf query implementation in NVRHI doesn't have any N-frame-behind support, so it may slow rendering down
        TwAddVarRW(pTwBarFPS, "Measure GPU times", TW_TYPE_BOOLCPP, &g_enablePerfQueries, "");

        auto getTimer = [](void * value, void * arg) {
            // The query is passed by pointer because by the time TwAddVarCB below are executed the query handles are NULL
            NVRHI::PerformanceQueryHandle query = *((NVRHI::PerformanceQueryHandle*)arg);
            if (g_enablePerfQueries && query)
                *(float *)value = g_pRendererInterface->getPerformanceQueryTimeMS(query);
            else
                *(float *)value = 0.f;
        };

        TwAddVarCB(pTwBarFPS, "GPU frame time (ms)", TW_TYPE_FLOAT, nullptr, getTimer, &m_sample->m_pqFrame, "precision=2");
        TwAddVarCB(pTwBarFPS, "Scene render time (ms)", TW_TYPE_FLOAT, nullptr, getTimer, &m_sample->m_pqRenderScene, "precision=2");

		TwAddVarCB(
				pTwBarFPS, "Triangle Count", TW_TYPE_INT32,
				nullptr,
				[](void * value, void * gpup) {
					(gpup);
					*(int*)value = g_triangleCounter;
				},
				nullptr,
			"precision=2");
	
		TwAddVarCB(
			pTwBarFPS, "Rendered MPixels", TW_TYPE_FLOAT,
			nullptr,
			[](void * value, void * demo) {
				float area = 0;
				bool vr = ((VRWorksSample*)demo)->IsVROrFakeVRActive();

				if (g_multiResEnabled)
				{
					if (vr)
						area = CalculateRenderedArea(g_multiResConfigVR[0], g_projectionDataVR[0].Viewports) + CalculateRenderedArea(g_multiResConfigVR[1], g_projectionDataVR[1].Viewports);
					else
						area = CalculateRenderedArea(g_multiResConfig, g_projectionData.Viewports);
				}
				else if (g_lensMatchedShadingEnabled)
				{
					if (vr)
						area = CalculateRenderedArea(g_lensMatchedConfigVR[0], g_projectionDataVR[0].Viewports) + CalculateRenderedArea(g_lensMatchedConfigVR[1], g_projectionDataVR[1].Viewports);
					else
					{
						area = CalculateRenderedArea(g_lensMatchedConfig, g_projectionData.Viewports);
					}
				}
				else
				{
					Nv::VR::Planar::Configuration config;
					if (vr)
						area = CalculateRenderedArea(config, g_projectionDataVR[0].Viewports) + CalculateRenderedArea(config, g_projectionDataVR[1].Viewports);
					else
					{
						area = CalculateRenderedArea(config, g_projectionData.Viewports);
					}
				}

				*(float*)value = area * 1e-6f;
			}, m_sample, "precision=2");

		#pragma endregion

		#pragma region Rendering

		// Create bar for rendering options
		TwBar* pTwBarRendering = TwNewBar("Rendering");
		TwDefine("Rendering position='10 190' size='320 210' valueswidth=80");

		TwAddButton(pTwBarRendering, "Reload Shaders", 
			[](void* demo) {
			auto pDemo = (VRWorksSample*)demo;
			pDemo->m_pShaderFactory->ClearCache();
			delete pDemo->m_pShaderState;
			pDemo->m_pShaderState = nullptr;
			}, m_sample, nullptr);

		TwAddVarRW(pTwBarRendering, "VSync", TW_TYPE_BOOLCPP, &g_vsync, nullptr);
	
		{
			TwEnumVal msaaEV[] =
			{
				{ 1, "No MSAA" },
				{ 2, "2x MSAA" },
				{ 4, "4x MSAA" },
				{ 8, "8x MSAA" },
			};
			TwType msaaModes = TwDefineEnum("MSAA Mode", msaaEV, sizeof(msaaEV) / sizeof(msaaEV[0]));
			TwAddVarRW(pTwBarRendering, "MSAA Mode", msaaModes, &g_msaaSampleCount, nullptr);
		}

		TwAddVarRW(pTwBarRendering, "Repeat Rendering", TW_TYPE_INT32, &g_repeatRenderingCount, "min=1 max=1000");

		TwAddVarRW(pTwBarRendering, "Resolution Scale", TW_TYPE_FLOAT, &g_resolutionScale, "min=0.1 max=3 step=0.01 precision=2");

		TwAddVarRW(pTwBarRendering, "SSAO", TW_TYPE_BOOLCPP, &g_enableSSAO, "");

		TwAddVarRW(pTwBarRendering, "Temporal AA", TW_TYPE_BOOLCPP, &g_temporalAA, "");

		TwAddVarRW(pTwBarRendering, "Frustum Culling", TW_TYPE_BOOLCPP, &g_frustumCulling, "");

		{   // Scene selection
			TwEnumVal sceneEV[] = {
				{ 0, "Sponza" },
				{ 1, "San Miguel" }
			};
			TwType sceneType = TwDefineEnum("Scene", sceneEV, sizeof(sceneEV) / sizeof(sceneEV[0]));
			TwAddVarCB(pTwBarRendering, "Scene", sceneType, SetSceneCB, GetSceneCB, m_sample, "");
		}

		#pragma endregion

		#pragma region MultiRes

		// Create bar for multi-res options and readouts
		TwBar* pTwBarMultiRes = TwNewBar("Multi-Res");
		TwDefine("Multi-Res size='320 200' valueswidth=160"); // position is updated in OnResize
	
		TwAddVarCB(pTwBarMultiRes, "Enable Multi-Res", TW_TYPE_BOOLCPP, 
			[](const void* value, void* demo) // Set callback
			{
				(demo);
				g_multiResEnabled = *(bool*)value;
				g_suppressTemporalAA = true;
			},
			[](void* value, void* demo) // get callback
			{
				(demo);
				*(bool*)value = g_multiResEnabled;
			}, 
			m_sample, nullptr);

		TwAddVarRW(pTwBarMultiRes, "Disable Flattening", TW_TYPE_BOOLCPP, &g_disableUpscale, nullptr);
	
		TwAddVarRW(pTwBarMultiRes, "Draw Splits", TW_TYPE_BOOLCPP, &g_drawMultiResSplits, nullptr);

		{
			TwEnumVal presetEV[] = {
				{ 0, "---" },
				{ 1, "Generic Balanced" },
				{ 2, "Generic Aggressive" },
				{ 3, "Oculus Rift Conservative" },
				{ 4, "Oculus Rift Balanced" },
				{ 5, "Oculus Rift Aggressive" },
				{ 6, "HTC Vive Conservative" },
				{ 7, "HTC Vive Balanced" },
				{ 8, "HTC Vive Aggressive" },
				{ 9, "HTC Vive Pro Conservative" },
				{ 10, "HTC Vive Pro Balanced" },
				{ 11, "HTC Vive Pro Aggressive" }
			};
			TwType presetType = TwDefineEnum("MRS preset", presetEV, dim(presetEV));
			TwAddVarCB(pTwBarMultiRes, "Preset Selection", presetType, 
				[](const void* value, void*) // Set callback
				{
					int index = *((int*)value);
					if (index > 0 && index < dim(MultiResPresets))
					{
						g_multiResConfig = *MultiResPresets[index];
						g_multiResConfigIndex = index;
					}
				},
					[](void* value, void*) // get callback
				{
					if (g_multiResConfigIndex > 0 && g_multiResConfigIndex < dim(MultiResPresets))
					{
						if (*MultiResPresets[g_multiResConfigIndex] != g_multiResConfig)
							g_multiResConfigIndex = 0;
					}

					*((int*)value) = g_multiResConfigIndex;
				}, m_sample, nullptr);
		}

		TwAddSeparator(pTwBarMultiRes, nullptr, nullptr);
	
		TwAddVarRW(pTwBarMultiRes, "Center Width", TW_TYPE_FLOAT, &g_multiResConfig.CenterWidth, "group='Manual Control' min=0.01 max=1.00 step=0.01 precision=2");
		TwAddVarRW(pTwBarMultiRes, "Center Height", TW_TYPE_FLOAT, &g_multiResConfig.CenterHeight, "group='Manual Control' min=0.01 max=1.00 step=0.01 precision=2");
		TwAddVarRW(pTwBarMultiRes, "Center X", TW_TYPE_FLOAT, &g_multiResConfig.CenterX, "group='Manual Control' min=0.00 max=1.00 step=0.01 precision=2");
		TwAddVarRW(pTwBarMultiRes, "Center Y", TW_TYPE_FLOAT, &g_multiResConfig.CenterY, "group='Manual Control' min=0.00 max=1.00 step=0.01 precision=2");

		TwAddSeparator(pTwBarMultiRes, nullptr, "group='Manual Control'");

		const char* resolutionDef = "group='Manual Control' min=0.05 max=2.0 step=0.01 precision=2";
		TwAddVarRW(pTwBarMultiRes, "DensityScaleX[0]", TW_TYPE_FLOAT, &g_multiResConfig.DensityScaleX[0], resolutionDef);
		TwAddVarRW(pTwBarMultiRes, "DensityScaleX[1]", TW_TYPE_FLOAT, &g_multiResConfig.DensityScaleX[1], resolutionDef);
		TwAddVarRW(pTwBarMultiRes, "DensityScaleX[2]", TW_TYPE_FLOAT, &g_multiResConfig.DensityScaleX[2], resolutionDef);
		TwAddVarRW(pTwBarMultiRes, "DensityScaleY[0]", TW_TYPE_FLOAT, &g_multiResConfig.DensityScaleY[0], resolutionDef);
		TwAddVarRW(pTwBarMultiRes, "DensityScaleY[1]", TW_TYPE_FLOAT, &g_multiResConfig.DensityScaleY[1], resolutionDef);
		TwAddVarRW(pTwBarMultiRes, "DensityScaleY[2]", TW_TYPE_FLOAT, &g_multiResConfig.DensityScaleY[2], resolutionDef);

		TwDefine("Multi-Res/'Manual Control' opened=false");

		#pragma endregion

		#pragma region Lens Matched Shading

		TwBar* pTwBarPascalVR = TwNewBar("LensMatchedShading");
		TwDefine("LensMatchedShading size='320 200' valueswidth=160"); // position is updated in OnResize
		
		TwAddVarRW(pTwBarPascalVR, "Enable Lens Matched Shading", TW_TYPE_BOOLCPP, &g_lensMatchedShadingEnabled, nullptr);
	
		TwAddVarRW(pTwBarPascalVR, "Disable Flattening", TW_TYPE_BOOLCPP, &g_disableUnwarp, nullptr);

		TwAddVarRW(pTwBarPascalVR, "Draw Splits", TW_TYPE_BOOLCPP, &g_drawLMSSplits, nullptr);

		{
			TwEnumVal presetEV[] = {
				{ 0, "---" },
				{ 1, "Oculus Rift Conservative" },
				{ 2, "Oculus Rift Balanced" },
				{ 3, "Oculus Rift Aggressive" },
				{ 4, "HTC Vive Conservative" },
				{ 5, "HTC Vive Balanced" },
				{ 6, "HTC Vive Aggressive" },
				{ 7, "HTC Vive Pro Conservative" },
				{ 8, "HTC Vive Pro Balanced" },
				{ 9, "HTC Vive Pro Aggressive" },
				{ 10, "Generic Conservative" },
				{ 11, "Generic Balanced" },
				{ 12, "Generic Aggressive" }
			};
			TwType presetType = TwDefineEnum("LMS preset", presetEV, dim(presetEV));
			TwAddVarCB(pTwBarPascalVR, "Preset Selection", presetType, 
				[](const void* value, void*) // Set callback
				{
					int index = *((int*)value);
					if (index > 0 && index < dim(LensMatchedPresets))
					{
						g_lensMatchedConfig = *LensMatchedPresets[index];
						g_lensMatchedConfigIndex = index;
					}
				},
				[](void* value, void*) // get callback
				{
					if (g_lensMatchedConfigIndex > 0 && g_lensMatchedConfigIndex < dim(LensMatchedPresets))
					{
						if (*LensMatchedPresets[g_lensMatchedConfigIndex] != g_lensMatchedConfig)
							g_lensMatchedConfigIndex = 0;
					}

					*((int*)value) = g_lensMatchedConfigIndex;
				}, m_sample, nullptr);
		}

		TwAddSeparator(pTwBarPascalVR, nullptr, nullptr);

		TwAddVarRW(pTwBarPascalVR, "Warp Left", TW_TYPE_FLOAT, &g_lensMatchedConfig.WarpLeft, "group='Manual Control' min=0.0 max=10 step=0.01 precision=2");
		TwAddVarRW(pTwBarPascalVR, "Warp Right", TW_TYPE_FLOAT, &g_lensMatchedConfig.WarpRight, "group='Manual Control' min=0.0 max=10 step=0.01 precision=2");
		TwAddVarRW(pTwBarPascalVR, "Warp Up", TW_TYPE_FLOAT, &g_lensMatchedConfig.WarpUp, "group='Manual Control' min=0.0 max=10 step=0.01 precision=2");
		TwAddVarRW(pTwBarPascalVR, "Warp Down", TW_TYPE_FLOAT, &g_lensMatchedConfig.WarpDown, "group='Manual Control' min=0.0 max=10 step=0.01 precision=2");

		TwAddVarCB(pTwBarPascalVR, "Warp All", TW_TYPE_FLOAT,
			[](const void* value, void* demo) // Set callback
			{
				(demo);
				g_lensMatchedConfig.WarpUp = g_lensMatchedConfig.WarpDown = g_lensMatchedConfig.WarpLeft = g_lensMatchedConfig.WarpRight = *(float*)value;
			},
			[](void* value, void* demo) // get callback
			{
				(demo);
				*(float*)value = g_lensMatchedConfig.WarpLeft;
			},
			m_sample, "min=0.0 max=10 step=0.01 precision=2");

		TwAddVarRW(pTwBarPascalVR, "Size Left", TW_TYPE_FLOAT, &g_lensMatchedConfig.RelativeSizeLeft, "group='Manual Control' min=0.05 max=0.5 step=0.01 precision=2");
		TwAddVarRW(pTwBarPascalVR, "Size Right", TW_TYPE_FLOAT, &g_lensMatchedConfig.RelativeSizeRight, "group='Manual Control' min=0.05 max=0.5 step=0.01 precision=2");
		TwAddVarRW(pTwBarPascalVR, "Size Up", TW_TYPE_FLOAT, &g_lensMatchedConfig.RelativeSizeUp, "group='Manual Control' min=0.05 max=0.5 step=0.01 precision=2");
		TwAddVarRW(pTwBarPascalVR, "Size Down", TW_TYPE_FLOAT, &g_lensMatchedConfig.RelativeSizeDown, "group='Manual Control' min=0.05 max=0.5 step=0.01 precision=2");

		TwDefine("LensMatchedShading/'Manual Control' opened=false");

		#pragma endregion

		#pragma region VR

		// Create bar for VR headset activation
		TwBar * pTwBarVR = TwNewBar("VR and Stereo");
		TwDefine("'VR and Stereo' position='10 410' size='320 240' valueswidth=80");
	
		TwAddButton(
			pTwBarVR, "Activate VR",
			[](void* window)
			{
				g_fakeVREnabled = false;
				((VRWorksSample *)window)->TryActivateVR();
			}, m_sample, nullptr);

		TwAddButton(
			pTwBarVR, "Activate Desktop Stereo",
			[](void* window)
			{
				(window);
				g_fakeVREnabled = true;
				((VRWorksSample *)window)->DeactivateVR();
			}, m_sample, nullptr);

		TwAddButton(
			pTwBarVR, "Deactivate VR/Stereo",
			[](void* window)
			{
				g_fakeVREnabled = false;
				((VRWorksSample *)window)->DeactivateVR();
			}, m_sample, nullptr);

		TwAddSeparator(pTwBarVR, nullptr, nullptr);

		TwAddVarCB(pTwBarVR, "Instanced Stereo", TW_TYPE_BOOLCPP,
			[](const void* value, void* demo) // Set callback
			{
				(demo);
				g_instancedStereoEnabled = *(bool*)value;
				g_singlePassStereoEnabled = false;
				g_MultiViewEnabled = false;
			},
			[](void* value, void* demo) // get callback
			{
				(demo);
				*(bool*)value = g_instancedStereoEnabled;
			},
				m_sample, "");

		TwAddVarCB(pTwBarVR, "Single Pass Stereo", TW_TYPE_BOOLCPP,
			[](const void* value, void* demo) // Set callback
			{
				(demo);
				g_instancedStereoEnabled = false;
				g_singlePassStereoEnabled = *(bool*)value;
				g_MultiViewEnabled = false;
			},
			[](void* value, void* demo) // get callback
			{
				(demo);
				*(bool*)value = g_singlePassStereoEnabled;
			},
				m_sample, "");

		TwAddVarCB(pTwBarVR, "Multi View Rendering", TW_TYPE_BOOLCPP,
			[](const void* value, void* demo) // Set callback
			{
				(demo);
				g_instancedStereoEnabled = false;
				g_singlePassStereoEnabled = false;
				g_MultiViewEnabled = *(bool*)value;
			},
			[](void* value, void* demo) // get callback
			{
				(demo);
				*(bool*)value = g_MultiViewEnabled;
			},
			this, "");

		TwAddSeparator(pTwBarVR, nullptr, nullptr);

		TwAddVarRW(pTwBarVR, "Eye Convergence (deg.)", TW_TYPE_FLOAT, &g_convergenceAngle, "min=-45.0 max=45.0 step=0.1 precision=1");

		TwAddSeparator(pTwBarVR, nullptr, nullptr);

		{
			TwEnumVal perfHudModeEV[] = {
				{ 0, "Off" },
				{ 1, "Perf summary" },
				{ 2, "Latency timing" },
				{ 3, "App render timing" },
				{ 4, "Compositor timing" },
				{ 5, "Version info" },
			};
			TwType perfHudModeType = TwDefineEnum("perfHudMode", perfHudModeEV, sizeof(perfHudModeEV) / sizeof(perfHudModeEV[0]));
			TwAddVarRW(pTwBarVR, "Perf HUD mode", perfHudModeType, &g_perfHudMode, "keyIncr=h keyDecr=H");
		}

		#pragma endregion

		#pragma region MessageBox

		TwBar * pTwBarMBox = TwNewBar("MessageBox");
		TwDefine("MessageBox label='Error Message' visible=false movable=false");
		TwAddButton(pTwBarMBox, "Line1", nullptr, nullptr, "");
		TwAddButton(pTwBarMBox, "Line2", nullptr, nullptr, "");
		TwAddSeparator(pTwBarMBox, "", nullptr);
		TwAddButton(pTwBarMBox, "OK", [](void*)
		{
			TwDefine("MessageBox visible=false");
		}, nullptr, "");

		#pragma endregion
	}

	void UpdateFeatureLevelLabels()
	{
		if (!IsMultiResShadingSupported())
			TwDefine("Multi-Res label='Multi-Res Shading (EMULATED)'");
		else
			TwDefine("Multi-Res label='Multi-Res Shading'");

		if (!IsLensMatchedShadingSupported())
			TwDefine("LensMatchedShading label='Lens Matched Shading (EMULATED)'");
		else
			TwDefine("LensMatchedShading label='Lens Matched Shading'");

		if (!IsSinglePassStereoSupported())
			TwDefine("'VR and Stereo'/'Single Pass Stereo' label='Single Pass Stereo (EMULATED)'");

#if USE_D3D11
		if (!IsMultiViewSupported())
			TwDefine("'VR and Stereo'/'Multi View Rendering' label='Multi View Rendering (EMULATED)'");
#elif USE_D3D12
		switch (g_ViewInstancingTier)
		{
		case D3D12_VIEW_INSTANCING_TIER_1:
			TwDefine("'VR and Stereo'/'Multi View Rendering' label='View Instancing (Tier 1)'");
			break;
		case D3D12_VIEW_INSTANCING_TIER_2:
			TwDefine("'VR and Stereo'/'Multi View Rendering' label='View Instancing (Tier 2)'");
			break;
		case D3D12_VIEW_INSTANCING_TIER_3:
			TwDefine("'VR and Stereo'/'Multi View Rendering' label='View Instancing (Tier 3)'");
			break;
		default:
			TwDefine("'VR and Stereo'/'Multi View Rendering' label='View Instancing (Not supported)'");
			break;
		}
#endif
	}
};

bool ProcessCommandLine()
{
	using namespace cxxopts;
	using std::string;

	// Get argc and argv using WinAPI functions
	int argc = 0;
	const LPWSTR* argvw = CommandLineToArgvW(GetCommandLineW(), &argc);

	if (argc == 0 || argvw == nullptr)
	{
		MessageBoxA(NULL, "Failed to process the command line.", "VRWorks Sample", MB_ICONERROR);
		return false;
	}

	// We need just a char** argv, but use a vector of strings for automatic memory management
	std::vector<std::string> arguments(argc);
	std::vector<char*> argv(argc);
	for (int i = 0; i < argc; i++)
	{
		int length = int(wcslen(argvw[i]));
		arguments[i].resize(length);
		argv[i] = &arguments[i][0];
		WideCharToMultiByte(CP_ACP, 0, argvw[i], length, argv[i], length, NULL, NULL);
	}
	LocalFree(HLOCAL(argvw));

	// Process the command line options using cxxopts
	Options options(arguments[0]);
	try
	{
		string preset = "";
		string sceneName = "";
		bool notaa = false;
		bool nossao = false;
		bool help = false;

		options.add_options()
			("mrs", "Enable Multi-Res Shading", value<bool>(g_multiResEnabled))
			("lms", "Enable Lens-Matched Shading", value<bool>(g_lensMatchedShadingEnabled))
			("preset", "Preset: {desktop,oculus,vive}-{balanced,conservative,aggressive}", value<string>(preset))
			("scale", "Resolution scale, default 1.0", value<float>(g_resolutionScale))
			("fakevr", "Enable desktop stereo (FakeVR) rendering", value<bool>(g_fakeVREnabled))
			("instanced", "Enable instanced stereo", value<bool>(g_instancedStereoEnabled))
			("sps", "Enable Single Pass Stereo", value<bool>(g_singlePassStereoEnabled))
			("mvr", "Enable Multi View Rendering", value<bool>(g_MultiViewEnabled))
			("notaa", "Disable Temporal AA", value<bool>(notaa))
			("nossao", "Disable SSAO", value<bool>(nossao))
			("scene", "Scene file name: sponza, san-miguel", value<string>(sceneName))
			("capture-frame", "Capture frame N, 1-based", value<int>(g_captureFrame))
			("capture-file", "File name to write the captured frame into", value<string>(g_captureFileName))
			("silent", "Suppress message boxes", value<bool>(g_suppressMessageBox))
			("h,help", "Show this help message", value<bool>(help));

		char** argvptr = &argv[0];
		options.parse(argc, argvptr);

		if (g_multiResEnabled && g_lensMatchedShadingEnabled)
		{
			throw OptionException("--mrs and --lms options cannot be used at the same time");
		}

		std::map<string, Nv::VR::MultiRes::Configuration> mrsPresets;
		mrsPresets["desktop-balanced"] = Nv::VR::MultiRes::Configuration_Balanced;
		mrsPresets["desktop-aggressive"] = Nv::VR::MultiRes::Configuration_Aggressive;
		mrsPresets["oculus-conservative"] = Nv::VR::MultiRes::ConfigurationSet_OculusRift_CV1[0];
		mrsPresets["oculus-balanced"] = Nv::VR::MultiRes::ConfigurationSet_OculusRift_CV1[1];
		mrsPresets["oculus-aggressive"] = Nv::VR::MultiRes::ConfigurationSet_OculusRift_CV1[2];
		mrsPresets["vive-conservative"] = Nv::VR::MultiRes::ConfigurationSet_HTC_Vive[0];
		mrsPresets["vive-balanced"] = Nv::VR::MultiRes::ConfigurationSet_HTC_Vive[1];
		mrsPresets["vive-aggressive"] = Nv::VR::MultiRes::ConfigurationSet_HTC_Vive[2];

		std::map<string, Nv::VR::LensMatched::Configuration> lmsPresets;
		lmsPresets["oculus-conservative"] = Nv::VR::LensMatched::ConfigurationSet_OculusRift_CV1[0];
		lmsPresets["oculus-balanced"] = Nv::VR::LensMatched::ConfigurationSet_OculusRift_CV1[1];
		lmsPresets["oculus-aggressive"] = Nv::VR::LensMatched::ConfigurationSet_OculusRift_CV1[2];
		lmsPresets["vive-conservative"] = Nv::VR::LensMatched::ConfigurationSet_HTC_Vive[0];
		lmsPresets["vive-balanced"] = Nv::VR::LensMatched::ConfigurationSet_HTC_Vive[1];
		lmsPresets["vive-aggressive"] = Nv::VR::LensMatched::ConfigurationSet_HTC_Vive[2];

		if (preset != "")
		{
			if (g_multiResEnabled)
			{
				if (mrsPresets.find(preset) == mrsPresets.end())
					throw OptionException("Preset " + preset + " is not defined for MRS");

				g_multiResConfig = mrsPresets[preset];
			}
			else if (g_lensMatchedShadingEnabled)
			{
				if (lmsPresets.find(preset) == lmsPresets.end())
					throw OptionException("Preset " + preset + " is not defined for LMS");

				g_lensMatchedConfig = lmsPresets[preset];
			}
			else
			{
				throw OptionException("--preset can only be specified together with --mrs or --lms");
			}
		}

		if (sceneName != "")
			g_sceneName = sceneName + ".json";

		g_temporalAA = !notaa;
		g_enableSSAO = !nossao;

		if (g_captureFrame > 0 && g_captureFileName == "")
		{
			throw OptionException("--capture-frame requires --capture-file to specified as well");
		}

		if (help)
        {
            MessageBoxA(NULL, options.help().c_str(), "VRWorks Sample", MB_ICONINFORMATION);
            return false;
		}
	}
	catch (OptionException ex)
	{
		string message = ex.what();
		MessageBoxA(NULL, message.c_str(), "VRWorks Sample", MB_ICONERROR);
		return false;
	}

	return true;
}


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	(hInstance);
	(void)hPrevInstance;
	(void)lpCmdLine;
	(void)nCmdShow;

	if (!ProcessCommandLine())
		return 1;

	g_DeviceManager = new DeviceManager();
	float color[] = { 0, 0, 0, 0 };
	g_DeviceManager->SetPrimaryRenderTargetClearColor(true, color);

	VRWorksSample sceneController;
	AntTweakBarVisualController atbController(&sceneController);
	g_DeviceManager->AddControllerToFront(&sceneController);
	g_DeviceManager->AddControllerToFront(&atbController);

    NvAPI_Initialize();

    DeviceCreationParameters deviceParams;
    
	deviceParams.adapter = VrSystem::GetRequiredAdapter();
	deviceParams.backBufferWidth = 1920;
	deviceParams.backBufferHeight = 1080;
#if !USE_GL4
	deviceParams.swapChainFormat = SWAP_CHAIN_FORMAT;
	deviceParams.swapChainSampleCount = 1;
	deviceParams.swapChainBufferCount = 2;
	deviceParams.startFullscreen = false;
#endif
#ifdef _DEBUG
	deviceParams.enableDebugRuntime = true;
#endif

	if (FAILED(g_DeviceManager->CreateWindowDeviceAndSwapChain(deviceParams, L"NVIDIA VRWorks Sample (" API_STRING ")")))
	{
		MessageBox(NULL, L"Cannot initialize the " API_STRING " device with the requested parameters", L"Error", MB_OK | MB_ICONERROR);
		return 1;
	}

	if (g_bInitialized)
	{
		atbController.UpdateFeatureLevelLabels();

		g_DeviceManager->MessageLoop();
	}

	delete g_DeviceManager;

	return 0;
}
