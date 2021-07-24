#pragma once
#include "D3D12Common.h"
#include "Device.h"

class Device;

struct DeviceOptions
{
	bool EnableDebugLayer;
	bool EnableGpuBasedValidation;
	bool EnableAutoDebugName;
};

// Can't use DeviceCapabilities, it conflicts with a macro
// Specify whether or not you want to require the device to have these features
struct DeviceFeatures
{
	D3D_FEATURE_LEVEL FeatureLevel;
	bool			  WaveOperation;
	bool			  Raytracing;
	bool			  DynamicResources;
};

class Adapter
{
public:
	static void ReportLiveObjects();

	Adapter();
	~Adapter();

	void Initialize(const DeviceOptions& Options);
	void InitializeDevice(const DeviceFeatures& Features);

	void OnBeginFrame() { Profiler.OnBeginFrame(); }
	void OnEndFrame() { Profiler.OnEndFrame(); }

	IDXGIFactory6* GetFactory6() const noexcept { return Factory6.Get(); }

	IDXGIAdapter4* GetAdapter4() const noexcept { return Adapter4.Get(); }

	ID3D12Device*  GetD3D12Device() const noexcept { return D3D12Device.Get(); }
	ID3D12Device5* GetD3D12Device5() const noexcept { return D3D12Device5.Get(); }

	UINT GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE Type) const noexcept
	{
		return DescriptorHandleIncrementSizeCache[Type];
	}

	void RegisterMessageCallback(D3D12MessageFunc CallbackFunc)
	{
		if (D3D12InfoQueue1)
		{
			ASSERTD3D12APISUCCEEDED(D3D12InfoQueue1->RegisterMessageCallback(
				CallbackFunc,
				D3D12_MESSAGE_CALLBACK_FLAG_NONE,
				nullptr,
				&CallbackCookie));
		}
	}

	void UnregisterMessageCallback()
	{
		if (D3D12InfoQueue1)
		{
			ASSERTD3D12APISUCCEEDED(D3D12InfoQueue1->UnregisterMessageCallback(CallbackCookie));
		}
	}

	Device* GetDevice() noexcept { return &Device; }

private:
	static void OnDeviceRemoved(PVOID Context, BOOLEAN);

	void InitializeDXGIObjects(bool Debug);

private:
	Microsoft::WRL::ComPtr<IDXGIFactory6> Factory6;

	AftermathCrashTracker AftermathCrashTracker;

	Microsoft::WRL::ComPtr<IDXGIAdapter4> Adapter4;
	DXGI_ADAPTER_DESC3					  AdapterDesc = {};

	Microsoft::WRL::ComPtr<ID3D12Device>  D3D12Device;
	Microsoft::WRL::ComPtr<ID3D12Device5> D3D12Device5;

	UINT DescriptorHandleIncrementSizeCache[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];

	Microsoft::WRL::ComPtr<ID3D12InfoQueue1> D3D12InfoQueue1;
	DWORD									 CallbackCookie = 0;

	Microsoft::WRL::ComPtr<ID3D12Fence> DeviceRemovedFence;
	HANDLE								DeviceRemovedWaitHandle = nullptr;
	wil::unique_event					DeviceRemovedEvent;

	Device Device;

	Profiler Profiler;
};
