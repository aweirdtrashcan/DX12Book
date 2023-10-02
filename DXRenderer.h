#pragma once

#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <exception>
#include <Windows.h>

class DXRenderer
{
public:
	DXRenderer(HINSTANCE hInstance);
	~DXRenderer();

	int Run();

private:
	void InitWindow();
	
	static LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
	
	void CreateDXDevice();
	Microsoft::WRL::ComPtr<ID3D12Fence> CreateFence() throw();
	void CheckMSAAQualitySupport();
	void CreateCommandObjects(bool bReset);
	void CreateSwapChain();
	void CreateRtvAndDsvDescriptorHeaps();
	D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const;
	D3D12_CPU_DESCRIPTOR_HANDLE BackBufferViewByIndex(UINT index) const;
	D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const;
	void CreateRenderTargetViews();
	void SetViewport() const;
	void SetScissor() const;

private:
	SIZE_T mRtvDescriptorHeapSize = 0;
	SIZE_T mDsvDescriptorHeapSize = 0;
	SIZE_T mCbvSrvDescriptorHeapSize = 0;
	UINT m4xMsaaQuality = 0;
	UINT mClientWidth = UINT_MAX;
	UINT mClientHeight = UINT_MAX;
	UINT mMsaaCount = 1;
	static constexpr UINT mBufferCount = 2;
	UINT mCurrBackBuffer = 0;
	HINSTANCE mhInstance = nullptr;

	Microsoft::WRL::ComPtr<IDXGIAdapter> mAdapter;
	Microsoft::WRL::ComPtr<ID3D12Device> mDevice;
	Microsoft::WRL::ComPtr<ID3D12InfoQueue> mInfoQueue;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> mCmdQueue;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCmdList;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> mCmdAllocator;
	Microsoft::WRL::ComPtr<IDXGISwapChain> mSwapchain;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDepthDescriptorHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mRenderTargetViewDescriptorHeap;
	Microsoft::WRL::ComPtr<ID3D12Resource> mDepthBuffer;

	DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT mDepthFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	HWND mHwnd = 0;

};

