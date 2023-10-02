#include "DXRenderer.h"

#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <cassert>
#include "DXUtil.h"
#include <malloc.h>

using namespace Microsoft::WRL;

DXRenderer::DXRenderer(HINSTANCE hInstance)
	:
	mhInstance(hInstance)
{
#ifdef _DEBUG
	ComPtr<ID3D12Debug> debug;
	D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
	debug->EnableDebugLayer();
#endif
	InitWindow();
	CreateDXDevice();
	CreateFence();
	mRtvDescriptorHeapSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	mDsvDescriptorHeapSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	mCbvSrvDescriptorHeapSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	CheckMSAAQualitySupport();
	CreateCommandObjects(true);
	CreateSwapChain();
	CreateRtvAndDsvDescriptorHeaps();
	CreateRenderTargetViews();
	SetViewport();
}

DXRenderer::~DXRenderer()
{
}

int DXRenderer::Run()
{
	MSG msg;

	mTimer.Reset();

	while (msg.message != WM_QUIT)
	{
		// If there are Window messages then process them.
		if (PeekMessage(&msg, mHwnd, NULL, NULL, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			mTimer.Tick();

			if (!mAppPaused)
			{
				CalculateFrameStats();
				Update(mTimer);
				Draw(mTimer);
			}
			else
			{
				Sleep(100);
			}
		}
	}
	return (int)msg.wParam;
}

void DXRenderer::InitWindow()
{
	WNDCLASS wc;
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = MainWndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = mhInstance;
	wc.hIcon = LoadIcon(0, IDI_APPLICATION);
	wc.hCursor = LoadCursor(0, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
	wc.lpszMenuName = 0;
	wc.lpszClassName = L"MainWnd";

	if (!RegisterClass(&wc))
	{
		MessageBox(0, L"RegisterClass Failed.", 0, 0);
	}

	mClientHeight = 720;
	mClientWidth = 1280;

	// Compute window rectangle dimensions based on requested client area dimensions.
	RECT R = { 0, 0, mClientWidth, mClientHeight };
	AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);
	int width = R.right - R.left;
	int height = R.bottom - R.top;

	mHwnd = CreateWindowA("MainWnd", "DirectX12 Window",
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, height, 0, 0, mhInstance, 0);
	if (!mHwnd)
	{
		MessageBox(0, L"CreateWindow Failed.", 0, 0);
	}

	ShowWindow(mHwnd, SW_SHOW);
	UpdateWindow(mHwnd);
}

LRESULT DXRenderer::MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

void DXRenderer::CreateDXDevice()
{
	ComPtr<IDXGIFactory> factory;
	ThrowIfFailed(CreateDXGIFactory(IID_PPV_ARGS(&factory)));
	ComPtr<IDXGIAdapter> adapter;
	std::vector<ComPtr<IDXGIAdapter>> adapters;

	UINT memSize = 0;

	for (UINT i = 0; factory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++)
	{
		DXGI_ADAPTER_DESC desc = {};
		adapter->GetDesc(&desc);

		std::wstring strDesc = desc.Description;

		std::wcout << L"Found adapter: " << strDesc << L"\n"
			<< L"Dedicated video memory: " << ((float)desc.DedicatedVideoMemory / 1024.f / 1024.f / 1024.f) << L"\n";

		if (desc.DedicatedVideoMemory > memSize) {
			memSize = desc.DedicatedVideoMemory;
			mAdapter = adapter;
		}

		adapters.push_back(adapter);
	}

	/*ComPtr<IDXGIOutput> monitor;
	std::vector<ComPtr<IDXGIOutput>> monitors;

	for (const auto& adap : adapters)
	{
		for (UINT i = 0; adap->EnumOutputs(i, &monitor) != DXGI_ERROR_NOT_FOUND; i++)
		{
			DXGI_OUTPUT_DESC desc = {};
			DXGI_ADAPTER_DESC adDesc = {};
			adap->GetDesc(&adDesc);
			monitor->GetDesc(&desc);

			UINT count = 0;
			monitor->GetDisplayModeList(DXGI_FORMAT_B8G8R8A8_UNORM, 0, &count, nullptr);
			std::vector<DXGI_MODE_DESC> modes(count);
			monitor->GetDisplayModeList(DXGI_FORMAT_B8G8R8A8_UNORM, 0, &count, modes.data());

			std::wstringstream buf;
			buf << L"Found in Adapter: " << adDesc.Description << L" one monitor: " << desc.DeviceName << L"\n";

			std::wstring str(buf.str());

			monitors.push_back(monitor);

			std::wcout << str;
			OutputDebugString(str.c_str());

			for (DXGI_MODE_DESC& d : modes)
			{
				std::wcout << L"\tRefresh Rate: " << (float)d.RefreshRate.Numerator / (float)d.RefreshRate.Denominator << L" Hz\n";
				std::wcout << L"\tWidth: " << d.Width << "\n";
				std::wcout << L"\tHeight: " << d.Height << "\n\n";
			}
		}
	}*/

	D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_12_0;

	ThrowIfFailed(D3D12CreateDevice(mAdapter.Get(), featureLevel, IID_PPV_ARGS(&mDevice)));

	assert(mDevice.Get() != nullptr && "Failed to Create DX Device");

#ifdef _DEBUG
	ThrowIfFailed(mDevice.As(&mInfoQueue));

	//mInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
	//mInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);

	mInfoQueue->SetBreakOnCategory(D3D12_MESSAGE_CATEGORY_EXECUTION, TRUE);

	D3D12_MESSAGE_SEVERITY msgSeverities[] =
	{
		D3D12_MESSAGE_SEVERITY_ERROR,
		D3D12_MESSAGE_SEVERITY_WARNING,
		D3D12_MESSAGE_SEVERITY_CORRUPTION
	};

	D3D12_MESSAGE_CATEGORY msgCategories[] =
	{
		D3D12_MESSAGE_CATEGORY_COMPILATION,
		D3D12_MESSAGE_CATEGORY_CLEANUP,
		D3D12_MESSAGE_CATEGORY_EXECUTION
	};

	D3D12_INFO_QUEUE_FILTER queueFilter = {};

	D3D12_INFO_QUEUE_FILTER_DESC allowList = {};
	allowList.NumCategories = (UINT)std::size(msgCategories);
	allowList.pCategoryList = msgCategories;
	allowList.NumSeverities = (UINT)std::size(msgSeverities);
	allowList.pSeverityList = msgSeverities;

	queueFilter.AllowList = allowList;

	ThrowIfFailed(mInfoQueue->PushStorageFilter(&queueFilter));
#endif
}

Microsoft::WRL::ComPtr<ID3D12Fence> DXRenderer::CreateFence() throw()
{
	ComPtr<ID3D12Fence> fence;
	mDevice->CreateFence(0u, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	return fence;
}

void DXRenderer::CheckMSAAQualitySupport()
{
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qLevels = {};
	qLevels.Format = mBackBufferFormat;
	qLevels.SampleCount = 4;
	qLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	ThrowIfFailed(mDevice->CheckFeatureSupport(
		D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
		&qLevels,
		sizeof(qLevels)
	));
	assert(qLevels.NumQualityLevels > 0 && "MSAA is not supported.");
	m4xMsaaQuality = qLevels.NumQualityLevels;
}

void DXRenderer::CreateCommandObjects(bool bReset)
{
	D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	ThrowIfFailed(mDevice->CreateCommandAllocator(type, IID_PPV_ARGS(&mCmdAllocator)));

	ThrowIfFailed(mDevice->CreateCommandList(0u, type, mCmdAllocator.Get(), nullptr, IID_PPV_ARGS(&mCmdList)));

	D3D12_COMMAND_QUEUE_DESC cmdQueueDesc = {};
	cmdQueueDesc.Type = type;
	cmdQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	cmdQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	cmdQueueDesc.NodeMask = 0u;

	ThrowIfFailed(mDevice->CreateCommandQueue(&cmdQueueDesc, IID_PPV_ARGS(&mCmdQueue)));

	ThrowIfFailed(mCmdList->Close());

	if (bReset)
		ThrowIfFailed(mCmdList->Reset(mCmdAllocator.Get(), nullptr));
}

void DXRenderer::CreateSwapChain()
{
	m4xMsaaQuality = 0;
	mSwapchain.Reset();
	ComPtr<IDXGIFactory> factory;
	CreateDXGIFactory(IID_PPV_ARGS(&factory));
	DXGI_SWAP_CHAIN_DESC desc = {};
	desc.BufferDesc.Width = mClientWidth;
	desc.BufferDesc.Height = mClientHeight;
	desc.BufferDesc.RefreshRate.Denominator = 0;
	desc.BufferDesc.RefreshRate.Numerator = 0;
	desc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	desc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	desc.BufferDesc.Format = mBackBufferFormat;
	desc.SampleDesc.Count = mMsaaCount;
	desc.SampleDesc.Quality = m4xMsaaQuality;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.OutputWindow = mHwnd;
	desc.BufferCount = mBufferCount;
	desc.Windowed = TRUE;
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

	ThrowIfFailed(factory->CreateSwapChain(mCmdQueue.Get(), &desc, mSwapchain.GetAddressOf()));
}

void DXRenderer::CreateRtvAndDsvDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC dsvDesc = {};
	dsvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvDesc.NodeMask = 0u;
	dsvDesc.NumDescriptors = 1u;
	dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;

	D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
	rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvDesc.NodeMask = 0u;
	rtvDesc.NumDescriptors = mBufferCount;
	rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	
	ThrowIfFailed(mDevice->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&mDepthDescriptorHeap)));
	ThrowIfFailed(mDevice->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&mRenderTargetViewDescriptorHeap)));
}

D3D12_CPU_DESCRIPTOR_HANDLE DXRenderer::CurrentBackBufferView() const
{
	D3D12_CPU_DESCRIPTOR_HANDLE descHandle = mRenderTargetViewDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	descHandle.ptr += mCurrBackBuffer * mRtvDescriptorHeapSize;
	return descHandle;
}

D3D12_CPU_DESCRIPTOR_HANDLE DXRenderer::BackBufferViewByIndex(UINT index) const
{
	D3D12_CPU_DESCRIPTOR_HANDLE descHandle = mRenderTargetViewDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	descHandle.ptr += (SIZE_T)index * mRtvDescriptorHeapSize;
	return descHandle;
}

D3D12_CPU_DESCRIPTOR_HANDLE DXRenderer::DepthStencilView() const
{
	return mDepthDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
}

void DXRenderer::CreateRenderTargetViews()
{
	for (UINT i = 0; i < mBufferCount; i++)
	{
		ComPtr<ID3D12Resource> backBuffer;
		ThrowIfFailed(mSwapchain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));

		mDevice->CreateRenderTargetView(backBuffer.Get(), nullptr, BackBufferViewByIndex(i));
	}
	D3D12_RESOURCE_DESC depthDesc = {};
	depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthDesc.Format = mDepthFormat;
	depthDesc.MipLevels = 1;
	depthDesc.SampleDesc.Count = mMsaaCount;
	depthDesc.SampleDesc.Quality = m4xMsaaQuality;
	depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	depthDesc.Width = mClientWidth;
	depthDesc.Height = mClientHeight;
	depthDesc.DepthOrArraySize = 1;

	D3D12_HEAP_PROPERTIES hProps = {};
	hProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	hProps.CreationNodeMask = 0u;
	hProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	hProps.Type = D3D12_HEAP_TYPE_DEFAULT;
	hProps.VisibleNodeMask = 0u;

	D3D12_HEAP_FLAGS hFlags = D3D12_HEAP_FLAG_NONE;

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = mDepthFormat;
	clearValue.DepthStencil.Depth = 1.0f;
	clearValue.DepthStencil.Stencil = 0;

	ThrowIfFailed(mDevice->CreateCommittedResource(
		&hProps, 
		hFlags, 
		&depthDesc, 
		D3D12_RESOURCE_STATE_COMMON,
		&clearValue,
		IID_PPV_ARGS(&mDepthBuffer)
	));

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvViewDesc = {};
	dsvViewDesc.Format = mDepthFormat;
	dsvViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvViewDesc.Texture2D.MipSlice = 0;

	mDevice->CreateDepthStencilView(mDepthBuffer.Get(), &dsvViewDesc, DepthStencilView());

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = mDepthBuffer.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;

	mCmdList->ResourceBarrier(
		1U,
		&barrier
	);
}

void DXRenderer::SetViewport() const
{
	D3D12_VIEWPORT vp = {};
	vp.Height = (FLOAT)mClientHeight;
	vp.Width = (FLOAT)mClientWidth;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	vp.TopLeftX = 0.0f;
	vp.TopLeftY = 0.0f;
	
	mCmdList->RSSetViewports(1u, &vp);
}

void DXRenderer::SetScissor() const
{
	D3D12_RECT scissor = { 0, 0, mClientWidth / 2, mClientHeight / 2 };
	
	mCmdList->RSSetScissorRects(1u, &scissor);
}
