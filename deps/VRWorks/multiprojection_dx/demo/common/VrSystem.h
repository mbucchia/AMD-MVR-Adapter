#pragma once

#include <util.h>

struct IDXGIAdapter;
struct ID3D11Device;
struct ID3D12CommandQueue;
struct ID3D11Texture2D;
struct ID3D11Resource;
struct ID3D12Resource;

enum struct VrResult
{
	OK,
	NoDevice,
	WrongAdapter,
	DisplayLost,
	OtherError
};

enum struct VrApi
{
	OculusVR,
	OpenVR
};

class VrSystem
{
public:
	virtual ~VrSystem() {}

	static IDXGIAdapter* GetRequiredAdapter();
	static VrResult CreateD3D11(ID3D11Device* pDevice, VrSystem** ppSystem);
	static VrResult CreateD3D12(IDXGIAdapter* pAdapter, ID3D12CommandQueue* pCommandQueue, VrSystem** ppSystem);

	virtual VrApi GetApi() const = 0;

	virtual VrResult AcquirePose() = 0;
	virtual VrResult PresentOculusVR() = 0;
	virtual VrResult PresentOpenVR(ID3D11Resource* pTexture) = 0;
	virtual void SetOculusPerfHudMode(int mode) { (void)mode; }
	virtual void Recenter() = 0;

	virtual uint32_t GetSwapChainBufferCount() const { return 0; }
	virtual uint32_t GetCurrentSwapChainBuffer() const { return 0; }
	virtual ID3D11Texture2D* GetSwapChainBufferD3D11(uint32_t index) const { (void)index; return nullptr; }
	virtual ID3D12Resource* GetSwapChainBufferD3D12(uint32_t index) const { (void)index; return nullptr; }

	virtual util::int2 GetSwapChainSize() const = 0;
	virtual util::float4x4 GetReverseProjectionMatrix(int eye, float zNear) const = 0;
	virtual util::affine3 GetEyeToOriginTransform(int eye) const = 0;
};