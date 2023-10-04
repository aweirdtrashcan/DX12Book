#pragma once

#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <exception>
#include <queue>
#include <string>
#include "GameTimer.h"

class DXRenderer
{
public:
	DXRenderer(HINSTANCE hInstance);
	~DXRenderer();

	void ClearCommandQueue();

	int Run();

	__forceinline static void Log(const char* str)
	{
#ifdef _DEBUG
		static DWORD strS;
		strS = 0;
		for (int i = 0; str[i] != 0; i++)
			strS++;

		if (!WriteConsoleA(mStandardOutput, str, strS, NULL, NULL))
		{
			throw std::exception("Failed to write to console from DXRenderer::Log");
		}
#endif
	}

private:
	void InitWindow();
	
	inline static LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
	inline LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
	
	inline void CalculateFrameStats();
	inline void OnResize();
	inline void Update(const GameTimer& GameTimer);
	inline void Draw(const GameTimer& GameTimer);

	inline void OnMouseDown(WPARAM btnState, int x, int y);
	inline void OnMouseUp(WPARAM btnState, int x, int y);
	inline void OnMouseMove(WPARAM btnState, int x, int y);

	inline void CreateDXDevice();
	inline _NODISCARD Microsoft::WRL::ComPtr<ID3D12Fence> CreateFence();
	inline void CheckMSAAQualitySupport();
	inline void CreateCommandObjects(bool bReset);
	inline void FlushCommandQueue();
	inline void CreateSwapChain();
	inline void CreateRtvAndDsvDescriptorHeaps();
	inline D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const;
	inline D3D12_CPU_DESCRIPTOR_HANDLE BackBufferViewByIndex(UINT index) const;
	inline D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const;
	inline void CreateRenderTargetViews(bool bReset = true);

	inline float AspectRatio() const { return (float)mClientWidth / (float)mClientHeight; }

	static void __stdcall messageCallback(D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, LPCSTR pDescription, void* pContext);

	void DirectXError(const char* _s0, const char* _s1);

private:

	struct ExceptionSettings
	{
		std::string _s0;
		std::string _s1;
	};

	std::queue<ExceptionSettings> mExceptionSettings;

	bool mHasException = false;

	GameTimer mTimer;

	SIZE_T mRtvDescriptorHeapSize = 0;
	SIZE_T mDsvDescriptorHeapSize = 0;
	SIZE_T mCbvSrvDescriptorHeapSize = 0;
	UINT m4xMsaaQuality = 0;
	INT mClientWidth = INT_MAX;
	INT mClientHeight = INT_MAX;
	UINT mMsaaCount = 1;
	static constexpr UINT mBufferCount = 2;
	UINT64 mCurrentFence = 0;
	UINT mCurrBackBuffer = 0;
	HINSTANCE mhInstance = nullptr;
	HANDLE mEventHandle = nullptr;

	static HANDLE mStandardOutput;

	bool mAppPaused = false;
	bool mMinimized = false;
	bool mMaximized = false;
	bool mResizing = false;
	bool mFullscreenState = false;

	Microsoft::WRL::ComPtr<IDXGIAdapter> mAdapter;
	Microsoft::WRL::ComPtr<ID3D12Device> mDevice;
	Microsoft::WRL::ComPtr<ID3D12InfoQueue1> mInfoQueue;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> mCmdQueue;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCmdList;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> mCmdAllocator;
	Microsoft::WRL::ComPtr<ID3D12Fence> mFence;
	Microsoft::WRL::ComPtr<IDXGISwapChain> mSwapchain;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDsvHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mRtvHeap;
	Microsoft::WRL::ComPtr<ID3D12Resource> mSwapchainBuffer[mBufferCount];
	Microsoft::WRL::ComPtr<ID3D12Resource> mDepthBuffer;

	D3D12_VIEWPORT vp;
	D3D12_RECT scissor;

	DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT mDepthFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	HWND mHwnd = 0;

};

