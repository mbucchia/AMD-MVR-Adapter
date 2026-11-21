
#include "VrSystem.h"

#include <d3d11_1.h>
#if USE_D3D12
#include <d3d12.h>
#endif

#include <openvr.h>
#include <OVR_CAPI_D3D.h>

#include <assert.h>
#include <vector>
#include <wrl.h>
#include <memory>
#include <algorithm>

using namespace util;
using namespace Microsoft::WRL;


class OculusVrSystem : public VrSystem
{
	friend class VrSystem;
private:
	ovrSession					m_oculusSession;
	ovrTextureSwapChain			m_oculusTextureSwapChain;
	ovrFovPort					m_projectionParameters[2];
	ovrVector3f					m_eyeOffsetsOculusHMD[2];
	ovrPosef					m_poseOculusHMD[2];
	int							m_frameCount;
	int2						m_vrSwapChainSize;

	OculusVrSystem()
		: m_oculusSession(nullptr)
		, m_oculusTextureSwapChain(nullptr)
		, m_frameCount(0)
	{
		memset(m_projectionParameters, 0, sizeof(m_projectionParameters));
		memset(m_eyeOffsetsOculusHMD, 0, sizeof(m_eyeOffsetsOculusHMD));
		memset(m_poseOculusHMD, 0, sizeof(m_poseOculusHMD));
	}

	VrResult Init(DXGI_ADAPTER_DESC* pAdapterDesc, IUnknown* pDeviceOrQueue)
	{
		// Init libovr
		ovrInitParams ovrParams = {};

		if (OVR_FAILURE(ovr_Initialize(&ovrParams)))
			return VrResult::NoDevice;

		// Connect to HMD
		ovrGraphicsLuid oculusLuid = {};
		if (OVR_FAILURE(ovr_Create(&m_oculusSession, &oculusLuid)))
			return VrResult::NoDevice;

		ASSERT_ERR(m_oculusSession);
		if (!m_oculusSession)
			return VrResult::OtherError;

		if (pAdapterDesc)
		{
			static_assert(sizeof(pAdapterDesc->AdapterLuid) == sizeof(oculusLuid), "LUIDs must be of the same size");
			bool match = (memcmp(&pAdapterDesc->AdapterLuid, &oculusLuid, sizeof(pAdapterDesc->AdapterLuid)) == 0);

			if (!match)
				return VrResult::WrongAdapter;
		}

		// Create swap texture set for both eyes side-by-side
		ovrHmdDesc hmdDesc = ovr_GetHmdDesc(m_oculusSession);
		ovrSizei eyeSizeLeft = ovr_GetFovTextureSize(m_oculusSession, ovrEye_Left, hmdDesc.DefaultEyeFov[0], 1.0f);
		ovrSizei eyeSizeRight = ovr_GetFovTextureSize(m_oculusSession, ovrEye_Right, hmdDesc.DefaultEyeFov[1], 1.0f);
		m_vrSwapChainSize = { max(eyeSizeLeft.w, eyeSizeRight.w) * 2, max(eyeSizeLeft.h, eyeSizeRight.h) };

		ovrTextureSwapChainDesc swapDesc =
		{
			ovrTexture_2D,
			OVR_FORMAT_R8G8B8A8_UNORM_SRGB,
			1, m_vrSwapChainSize.x, m_vrSwapChainSize.y, 1, 1,
			ovrFalse,
			ovrTextureMisc_DX_Typeless,
			ovrTextureBind_DX_RenderTarget
		};

		if (OVR_FAILURE(ovr_CreateTextureSwapChainDX(
			m_oculusSession,
			pDeviceOrQueue,
			&swapDesc,
			&m_oculusTextureSwapChain
		)))
			return VrResult::OtherError;

		if (!m_oculusTextureSwapChain)
			return VrResult::OtherError;

		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		rtvDesc.Texture2D.MipSlice = 0;

		for (int i = 0; i < 2; ++i)
		{
			// Store the eye FOVs and offset vectors for later use
			m_projectionParameters[i] = hmdDesc.DefaultEyeFov[i];
			ovrEyeRenderDesc eyeRenderDesc = ovr_GetRenderDesc(m_oculusSession, ovrEyeType(i), hmdDesc.DefaultEyeFov[i]);
			m_eyeOffsetsOculusHMD[i] = eyeRenderDesc.HmdToEyeOffset;
		}

		return VrResult::OK;
	}
public:

	virtual ~OculusVrSystem()
	{
		if (m_oculusTextureSwapChain)
		{
			ovr_DestroyTextureSwapChain(m_oculusSession, m_oculusTextureSwapChain);
			m_oculusTextureSwapChain = nullptr;
		}

		if (m_oculusSession)
		{
			ovr_Destroy(m_oculusSession);
			m_oculusSession = nullptr;
		}

		ovr_Shutdown();
	}

	static IDXGIAdapter* GetRequiredAdapter()
	{
		ovrInitParams ovrParams = {};

		if (OVR_FAILURE(ovr_Initialize(&ovrParams)))
			return nullptr;

		ovrSession session;
		ovrGraphicsLuid oculusLuid = {};
		ovrResult result = ovr_Create(&session, &oculusLuid);
		ovr_Shutdown();

		if (OVR_FAILURE(result))
			return nullptr;

		static_assert(sizeof(LUID) == sizeof(oculusLuid), "LUIDs must be of the same size");

		ComPtr<IDXGIFactory1> pDxgiFactory;
		if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pDxgiFactory)))
		{
			UINT nAdapter = 0;
			IDXGIAdapter* pAdapter = nullptr;
			while (SUCCEEDED(pDxgiFactory->EnumAdapters(nAdapter, &pAdapter)))
			{
				DXGI_ADAPTER_DESC adapterDesc;
				pAdapter->GetDesc(&adapterDesc);

				if (memcmp(&adapterDesc.AdapterLuid, &oculusLuid, sizeof(oculusLuid)) == 0)
					return pAdapter;

				pAdapter->Release();
				pAdapter = nullptr;

				nAdapter++;
			}
		}

		return nullptr;
	}

	virtual VrApi GetApi() const override
	{
		return VrApi::OculusVR;
	}

	virtual VrResult AcquirePose() override
	{
		m_frameCount++;
		ovr_GetEyePoses(m_oculusSession, m_frameCount, true, m_eyeOffsetsOculusHMD, m_poseOculusHMD, nullptr);
		return VrResult::OK;
	}

	virtual VrResult PresentOculusVR() override
	{
		// Commit changes to the swap chain
		ovr_CommitTextureSwapChain(m_oculusSession, m_oculusTextureSwapChain);

		// Submit the frame to the Oculus runtime
		ovrResult result;

		ovrLayerEyeFov layerMain = {};
		layerMain.Header.Type = ovrLayerType_EyeFov;
		layerMain.Header.Flags = ovrLayerFlag_HighQuality;
		layerMain.ColorTexture[0] = m_oculusTextureSwapChain;
		layerMain.Viewport[ovrEye_Left].Size.w = m_vrSwapChainSize.x / 2;
		layerMain.Viewport[ovrEye_Left].Size.h = m_vrSwapChainSize.y;
		layerMain.Viewport[ovrEye_Right].Pos.x = m_vrSwapChainSize.x / 2;
		layerMain.Viewport[ovrEye_Right].Size.w = m_vrSwapChainSize.x / 2;
		layerMain.Viewport[ovrEye_Right].Size.h = m_vrSwapChainSize.y;
		layerMain.Fov[ovrEye_Left] = m_projectionParameters[ovrEye_Left];
		layerMain.Fov[ovrEye_Right] = m_projectionParameters[ovrEye_Right];
		layerMain.RenderPose[ovrEye_Left] = m_poseOculusHMD[ovrEye_Left];
		layerMain.RenderPose[ovrEye_Right] = m_poseOculusHMD[ovrEye_Right];

		ovrLayerHeader * layerList[] = { (ovrLayerHeader*)&layerMain };
		result = ovr_SubmitFrame(m_oculusSession, m_frameCount, nullptr, layerList, dim(layerList));

		if (result == ovrError_DisplayLost)
		{
			return VrResult::DisplayLost;
		}
		else if (OVR_FAILURE(result))
		{
			ovrErrorInfo errorInfo;
			ovr_GetLastErrorInfo(&errorInfo);
			WARN("ovr_SubmitFrame failed with error code: %d\nError message: %s", result, errorInfo.ErrorString);
		}

		return VrResult::OK;
	}

	virtual VrResult PresentOpenVR(ID3D11Resource* pTexture) 
	{ 
		(void)pTexture; 
		return VrResult::OtherError;
	}

	virtual void SetOculusPerfHudMode(int mode) override
	{
		ovr_SetInt(m_oculusSession, OVR_PERF_HUD_MODE, mode);
	}

	virtual void Recenter() override
	{
		ovr_RecenterTrackingOrigin(m_oculusSession);
	}

	virtual uint32_t GetSwapChainBufferCount() const override
	{
		int swapLength = 0;

		ovr_GetTextureSwapChainLength(m_oculusSession, m_oculusTextureSwapChain, &swapLength);

		return static_cast<uint32_t>(std::max(0, swapLength));
	}

	virtual uint32_t GetCurrentSwapChainBuffer() const override
	{
		int currentIndex = 0;

		ovr_GetTextureSwapChainCurrentIndex(m_oculusSession, m_oculusTextureSwapChain, &currentIndex);

		return static_cast<uint32_t>(std::max(0, currentIndex));
	}

	virtual ID3D11Texture2D* GetSwapChainBufferD3D11(uint32_t index) const override
	{
		ID3D11Texture2D* swapTex = nullptr;
		ovr_GetTextureSwapChainBufferDX(m_oculusSession, m_oculusTextureSwapChain, static_cast<int>(index), IID_PPV_ARGS(&swapTex));
		return swapTex;
	}

	virtual ID3D12Resource* GetSwapChainBufferD3D12(uint32_t index) const override
	{
		ID3D12Resource* swapTex = nullptr;
#if USE_D3D12
		ovr_GetTextureSwapChainBufferDX(m_oculusSession, m_oculusTextureSwapChain, static_cast<int>(index), IID_PPV_ARGS(&swapTex));
#else
		(void)index;
#endif
		return swapTex;
	}

	virtual util::int2 GetSwapChainSize() const override
	{
		return m_vrSwapChainSize;
	}

	virtual util::float4x4 GetReverseProjectionMatrix(int eye, float zNear) const override
	{
		const ovrFovPort& params = m_projectionParameters[eye];

		return perspProjD3DStyleReverse(-params.LeftTan, params.RightTan, -params.DownTan, params.UpTan, zNear);
	}

	virtual util::affine3 GetEyeToOriginTransform(int eye) const override
	{
		ovrPosef hmdPose = m_poseOculusHMD[eye];
		// negate Z for RH to LH coordinate space transform, also negate x and y in quaternion to account for LH vs RH rotation
		hmdPose.Position.z = -hmdPose.Position.z;

		quat hmdOrientation =
		{
			hmdPose.Orientation.w,		// Note, different layout from Oculus quaternion!
			-hmdPose.Orientation.x,
			-hmdPose.Orientation.y,
			hmdPose.Orientation.z
		};
		return makeaffine3(hmdOrientation, makefloat3(&hmdPose.Position.x));
	}
};

class OpenVrSystem : public VrSystem
{
	friend class VrSystem;
private:
	vr::IVRSystem*				m_pOpenVRSystem;
	vr::IVRCompositor*			m_pOpenVRCompositor;
	vr::TrackedDevicePose_t		m_poseOpenVR;
	ovrFovPort					m_projectionParameters[2];
	int2						m_vrSwapChainSize;

	OpenVrSystem()
		: m_pOpenVRSystem(nullptr)
		, m_pOpenVRCompositor(nullptr)
	{ 
		memset(m_projectionParameters, 0, sizeof(m_projectionParameters));
		memset(&m_poseOpenVR, 0, sizeof(m_poseOpenVR));
	}
public:

	virtual ~OpenVrSystem()
	{
		if (m_pOpenVRSystem)
		{
			vr::VR_Shutdown();
			m_pOpenVRSystem = nullptr;
			m_pOpenVRCompositor = nullptr;
		}
	}

	VrResult Init(DXGI_ADAPTER_DESC* pAdapterDesc)
	{
		// Loading the SteamVR Runtime
		vr::EVRInitError initError;
		m_pOpenVRSystem = vr::VR_Init(&initError, vr::VRApplication_Scene);
		if (initError != vr::VRInitError_None)
		{
			return VrResult::NoDevice;
		}

		// Init compositor.
		m_pOpenVRCompositor = reinterpret_cast<vr::IVRCompositor*>(vr::VR_GetGenericInterface(vr::IVRCompositor_Version, &initError));
		if (initError != vr::VRInitError_None)
		{
			LOG("VR_GetGenericInterface failed with error code: %d\nError message: %s", initError, vr::VR_GetVRInitErrorAsEnglishDescription(initError));
			return VrResult::NoDevice;
		}

		// Set new render target size for side-by-side stereo
		uint32_t eyeWidth, eyeHeight;
		m_pOpenVRSystem->GetRecommendedRenderTargetSize(&eyeWidth, &eyeHeight);
		m_vrSwapChainSize = makeint2(eyeWidth * 2, eyeHeight);

		// Use GetProjectionRaw instead of GetProjectionMatrix because we use 1/W projection here
		float left[2], right[2], top[2], bottom[2];
		m_pOpenVRSystem->GetProjectionRaw(vr::EVREye(0), &left[0], &right[0], &top[0], &bottom[0]);
		m_pOpenVRSystem->GetProjectionRaw(vr::EVREye(1), &left[1], &right[1], &top[1], &bottom[1]);

		// Average the left and right versions of 'top' and 'bottom' because they're slightly
		// different, and we can't have different vertical projections in SPS mode, but we want
		// the SPS image to match with the non-SPS image.
		float avgTop = (top[0] + top[1]) * 0.5f;
		float avgBottom = (bottom[0] + bottom[1]) * 0.5f;

		// Use the same Oculus structure to store the proejction parameters, for convenience
		m_projectionParameters[0] = { -avgTop, avgBottom, -left[0], right[0] };
		m_projectionParameters[1] = { -avgTop, avgBottom, -left[1], right[1] };


		int adapterIndex;
		m_pOpenVRSystem->GetDXGIOutputInfo(&adapterIndex);

		if (pAdapterDesc)
		{
			ComPtr<IDXGIFactory1> pDxgiFactory;
			if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pDxgiFactory)))
			{
				IDXGIAdapter* pAdapter = nullptr;
				if (SUCCEEDED(pDxgiFactory->EnumAdapters(adapterIndex, &pAdapter)))
				{
					DXGI_ADAPTER_DESC openvrAdapterDesc;
					pAdapter->GetDesc(&openvrAdapterDesc);
					pAdapter->Release();

					if (openvrAdapterDesc.AdapterLuid.LowPart != pAdapterDesc->AdapterLuid.LowPart ||
						openvrAdapterDesc.AdapterLuid.HighPart != pAdapterDesc->AdapterLuid.HighPart)
						return VrResult::WrongAdapter;
				}
			}
		}

		return VrResult::OK;
	}

	static IDXGIAdapter* GetRequiredAdapter()
	{
		// Loading the SteamVR Runtime
		vr::EVRInitError initError;
		vr::IVRSystem* vrSystem = vr::VR_Init(&initError, vr::VRApplication_Scene);
		if (initError != vr::VRInitError_None)
		{
			return nullptr;
		}

		int adapterIndex = -1;
		vrSystem->GetDXGIOutputInfo(&adapterIndex);

		vr::VR_Shutdown();

		ComPtr<IDXGIFactory1> pDxgiFactory;
		if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pDxgiFactory)))
		{
			IDXGIAdapter* pAdapter = nullptr;
			if(SUCCEEDED(pDxgiFactory->EnumAdapters(adapterIndex, &pAdapter)))
			{
				return pAdapter;
			}
		}

		return nullptr;
	}

	virtual VrApi GetApi() const override
	{
		return VrApi::OpenVR;
	}

	virtual VrResult AcquirePose() override
	{
		if (m_pOpenVRCompositor->WaitGetPoses(&m_poseOpenVR, 1, nullptr, 0) == vr::VRCompositorError_None)
			return VrResult::OK;
		else
			return VrResult::OtherError;
	}

	virtual VrResult PresentOculusVR() override
	{
		return VrResult::OtherError;
	}

	virtual VrResult PresentOpenVR(ID3D11Resource* pTexture) override
	{
		vr::Texture_t tex = { pTexture, vr::TextureType_DirectX, vr::ColorSpace_Gamma };
		vr::VRTextureBounds_t bounds = { 0.0f, 0.0f, 0.5f, 1.0f };
		vr::EVRCompositorError resultLeft = m_pOpenVRCompositor->Submit(vr::Eye_Left, &tex, &bounds);
		bounds = { 0.5f, 0.0f, 1.0f, 1.0f };
		vr::EVRCompositorError resultRight = m_pOpenVRCompositor->Submit(vr::Eye_Right, &tex, &bounds);

		if (resultLeft == vr::VRCompositorError_None && resultRight == vr::VRCompositorError_None)
			return VrResult::OK;

		if (resultLeft == vr::VRCompositorError_TextureIsOnWrongDevice || resultRight == vr::VRCompositorError_TextureIsOnWrongDevice)
			return VrResult::WrongAdapter;

		return VrResult::OtherError;
	}

	virtual void Recenter() override
	{
		m_pOpenVRSystem->ResetSeatedZeroPose();
	}

	virtual util::int2 GetSwapChainSize() const override
	{
		return m_vrSwapChainSize;
	}

	virtual util::float4x4 GetReverseProjectionMatrix(int eye, float zNear) const override
	{
		const ovrFovPort& params = m_projectionParameters[eye];

		return perspProjD3DStyleReverse(-params.LeftTan, params.RightTan, -params.DownTan, params.UpTan, zNear);
	}

	virtual util::affine3 GetEyeToOriginTransform(int eye) const override
	{
		vr::HmdMatrix34_t eyeToHeadOpenVR = m_pOpenVRSystem->GetEyeToHeadTransform(vr::EVREye(eye));
		affine3 eyeToHMD =
		{	// transposed from column-vector to row-vector convention
			// negate Z to transform from RH to LH coordinates
			eyeToHeadOpenVR.m[0][0], eyeToHeadOpenVR.m[1][0], -eyeToHeadOpenVR.m[2][0],
			eyeToHeadOpenVR.m[0][1], eyeToHeadOpenVR.m[1][1], -eyeToHeadOpenVR.m[2][1],
			eyeToHeadOpenVR.m[0][2], eyeToHeadOpenVR.m[1][2], -eyeToHeadOpenVR.m[2][2],
			eyeToHeadOpenVR.m[0][3], eyeToHeadOpenVR.m[1][3], -eyeToHeadOpenVR.m[2][3],
		};

		vr::HmdMatrix34_t hmdPose = m_poseOpenVR.mDeviceToAbsoluteTracking;
		affine3 hmdToCamera =
		{	// transposed from column-vector to row-vector convention
			// negate Z to transform from RH to LH coordinates
			hmdPose.m[0][0], hmdPose.m[1][0], -hmdPose.m[2][0],
			hmdPose.m[0][1], hmdPose.m[1][1], -hmdPose.m[2][1],
			hmdPose.m[0][2], hmdPose.m[1][2], -hmdPose.m[2][2],
			hmdPose.m[0][3], hmdPose.m[1][3], -hmdPose.m[2][3],
		};

		return eyeToHMD * hmdToCamera;
	}
};

IDXGIAdapter* VrSystem::GetRequiredAdapter()
{
	IDXGIAdapter* pAdapter = nullptr;

	pAdapter = OculusVrSystem::GetRequiredAdapter();
	if (pAdapter) return pAdapter;

	pAdapter = OpenVrSystem::GetRequiredAdapter();
	if (pAdapter) return pAdapter;

	return nullptr;
}

VrResult VrSystem::CreateD3D11(ID3D11Device* pDevice, VrSystem** ppSystem)
{
	*ppSystem = nullptr;
	VrResult result = VrResult::OK;

	IDXGIDevice* pDxgiDevice = nullptr;
	IDXGIAdapter* pAdapter = nullptr;
	pDevice->QueryInterface(&pDxgiDevice);
	pDxgiDevice->GetAdapter(&pAdapter);
	pDxgiDevice->Release();

	DXGI_ADAPTER_DESC adapterDesc;
	pAdapter->GetDesc(&adapterDesc);
	pAdapter->Release();

	OculusVrSystem* oculus = new OculusVrSystem();
	result = oculus->Init(&adapterDesc, pDevice);
	if (result == VrResult::OK)
	{
		*ppSystem = oculus;
		return VrResult::OK;
	}

	delete oculus;
	
	if (result == VrResult::WrongAdapter)
		return VrResult::WrongAdapter;
	
	OpenVrSystem* openvr = new OpenVrSystem();
	result = openvr->Init(&adapterDesc);
	if (result == VrResult::OK)
	{
		*ppSystem = openvr;
		return VrResult::OK;
	}

	delete openvr;

	return result;
}

VrResult VrSystem::CreateD3D12(IDXGIAdapter* pAdapter, ID3D12CommandQueue* pCommandQueue, VrSystem** ppSystem)
{
	*ppSystem = nullptr;
	VrResult result = VrResult::OK;

	DXGI_ADAPTER_DESC adapterDesc;
	pAdapter->GetDesc(&adapterDesc);

	OculusVrSystem* oculus = new OculusVrSystem();
	result = oculus->Init(&adapterDesc, (IUnknown*)pCommandQueue);
	if (result == VrResult::OK)
	{
		*ppSystem = oculus;
		return VrResult::OK;
	}

	delete oculus;
	return result;
}