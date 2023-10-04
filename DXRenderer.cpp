#include "DXRenderer.h"

#include <vector>
#include <string>
#include <sstream>
#include <cassert>
#include "DXUtil.h"
#include <malloc.h>
#include <format>
#include <windowsx.h>
#include "DXException.h"

using namespace Microsoft::WRL;

HANDLE DXRenderer::mStandardOutput = nullptr;

DXRenderer::DXRenderer(HINSTANCE hInstance)
	:
	mhInstance(hInstance)
{
#ifdef _DEBUG
	ComPtr<ID3D12Debug> debug;
	D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
	debug->EnableDebugLayer();

	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
	AllocConsole();

	mStandardOutput = GetStdHandle(STD_OUTPUT_HANDLE);
#endif
	InitWindow();
	CreateDXDevice();
	mEventHandle = CreateEventA(nullptr, FALSE, FALSE, nullptr);
	mFence = CreateFence();
	mRtvDescriptorHeapSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	mDsvDescriptorHeapSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	mCbvSrvDescriptorHeapSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	CheckMSAAQualitySupport();
	CreateCommandObjects(false);
	CreateSwapChain();
	CreateRtvAndDsvDescriptorHeaps();
	OnResize();
}

DXRenderer::~DXRenderer()
{
	ClearCommandQueue();
	CloseHandle(mEventHandle);
	FreeConsole();
}

void DXRenderer::ClearCommandQueue()
{
	ThrowIfFailed(mCmdQueue->Signal(mFence.Get(), ++mCurrentFence));
	FlushCommandQueue();
}


int DXRenderer::Run()
{
	MSG msg = {};

	mTimer.Reset();

	while (msg.message != WM_QUIT)
	{
		if (mHasException)
		{
			std::stringstream oss;
			while (!mExceptionSettings.empty())
			{
				ExceptionSettings s = mExceptionSettings.front();
				oss << s._s0 << s._s1 << "\n";
				mExceptionSettings.pop();
			}
			throw DXException("", oss.str().c_str());
		}
		// If there are Window messages then process them.
		if (PeekMessage(&msg, NULL, NULL, NULL, PM_REMOVE))
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

	mClientWidth = 1280;
	mClientHeight = 720;

	// Compute window rectangle dimensions based on requested client area dimensions.
	//RECT R = { 0, 0, mClientWidth, mClientHeight };
	//AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);
	//int width = R.right - R.left;
	//int height = R.bottom - R.top;

	mHwnd = CreateWindowExA(WS_EX_APPWINDOW | WS_EX_CLIENTEDGE, "MainWnd", "DirectX12 Window",
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, mClientWidth, mClientHeight, 0, 0, mhInstance, this);
	if (!mHwnd)
	{
		MessageBox(0, L"CreateWindow Failed.", 0, 0);
	}

	ShowWindow(mHwnd, SW_SHOW);
	UpdateWindow(mHwnd);
}

LRESULT DXRenderer::MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_NCCREATE)
	{
		LPCREATESTRUCTA createStruct = (LPCREATESTRUCTA)lParam;
		SetWindowLongPtrA(hWnd, GWLP_USERDATA, (LONG_PTR)createStruct->lpCreateParams);
	}
	DXRenderer* r = (DXRenderer*)GetWindowLongPtrA(hWnd, GWLP_USERDATA);
	return r->WndProc(hWnd, msg, wParam, lParam);
}

LRESULT DXRenderer::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
		// WM_ACTIVATE is sent when the window is activated or deactivated.  
		// We pause the game when the window is deactivated and unpause it 
		// when it becomes active.  
	case WM_ACTIVATE:
		if (LOWORD(wParam) == WA_INACTIVE)
		{
			mAppPaused = true;
			mTimer.Stop();
		}
		else
		{
			mAppPaused = false;
			mTimer.Start();
		}
		return 0;

		// WM_SIZE is sent when the user resizes the window.  
	case WM_SIZE:
		// Save the new client area dimensions.
		mClientWidth = LOWORD(lParam);
		mClientHeight = HIWORD(lParam);
		if (this)
		{
			//Log(std::format("Width: {}, Height: {}\n", mClientWidth, mClientHeight).c_str());
		}
		if (mDevice)
		{
			if (wParam == SIZE_MINIMIZED)
			{
				mAppPaused = true;
				mMinimized = true;
				mMaximized = false;
			}
			else if (wParam == SIZE_MAXIMIZED)
			{
				mAppPaused = false;
				mMinimized = false;
				mMaximized = true;
				OnResize();
			}
			else if (wParam == SIZE_RESTORED)
			{

				// Restoring from minimized state?
				if (mMinimized)
				{
					mAppPaused = false;
					mMinimized = false;
					OnResize();
				}

				// Restoring from maximized state?
				else if (mMaximized)
				{
					mAppPaused = false;
					mMaximized = false;
					OnResize();
				}
				else if (mResizing)
				{
					// If user is dragging the resize bars, we do not resize 
					// the buffers here because as the user continuously 
					// drags the resize bars, a stream of WM_SIZE messages are
					// sent to the window, and it would be pointless (and slow)
					// to resize for each WM_SIZE message received from dragging
					// the resize bars.  So instead, we reset after the user is 
					// done resizing the window and releases the resize bars, which 
					// sends a WM_EXITSIZEMOVE message.
				}
				else // API call such as SetWindowPos or mSwapChain->SetFullscreenState.
				{
					OnResize();
				}
			}
		}
		return 0;

		// WM_EXITSIZEMOVE is sent when the user grabs the resize bars.
	case WM_ENTERSIZEMOVE:
		mAppPaused = true;
		mResizing = true;
		mTimer.Stop();
		return 0;

		// WM_EXITSIZEMOVE is sent when the user releases the resize bars.
		// Here we reset everything based on the new window dimensions.
	case WM_EXITSIZEMOVE:
		mAppPaused = false;
		mResizing = false;
		mTimer.Start();
		OnResize();
		return 0;

		// WM_DESTROY is sent when the window is being destroyed.
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

		// The WM_MENUCHAR message is sent when a menu is active and the user presses 
		// a key that does not correspond to any mnemonic or accelerator key. 
	case WM_MENUCHAR:
		// Don't beep when we alt-enter.
		return MAKELRESULT(0, MNC_CLOSE);

		// Catch this message so to prevent the window from becoming too small.
	case WM_GETMINMAXINFO:
		((MINMAXINFO*)lParam)->ptMinTrackSize.x = 200;
		((MINMAXINFO*)lParam)->ptMinTrackSize.y = 200;
		return 0;

	case WM_LBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_RBUTTONDOWN:
		OnMouseDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	case WM_LBUTTONUP:
	case WM_MBUTTONUP:
	case WM_RBUTTONUP:
		OnMouseUp(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	case WM_MOUSEMOVE:
		OnMouseMove(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	case WM_KEYUP:
		if (wParam == VK_ESCAPE)
		{
			PostQuitMessage(0);
		}
		/*else if ((int)wParam == VK_F2)
			Set4xMsaaState(!m4xMsaaState);*/
		return 0;
	default:
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}

	return DefWindowProc(hWnd, msg, wParam, lParam);
}

void DXRenderer::CalculateFrameStats()
{
	static int frameCount = 0;
	static float timeElapsed = 0.0f;
	frameCount++;

	if ((mTimer.TotalTime() - timeElapsed) >= 1.0f)
	{
		float fps = (float)frameCount;
		float mspf = 1000.f / fps;

		std::string windowText = std::format("FPS: {} Frametime: {}", fps, mspf);
		SetWindowTextA(mHwnd, windowText.c_str());

		frameCount = 0;
		timeElapsed += 1.0f;
	}
}

void DXRenderer::OnResize()
{
	ClearCommandQueue();

	ThrowIfFailed(mCmdAllocator->Reset());
	ThrowIfFailed(mCmdList->Reset(mCmdAllocator.Get(), nullptr));

	for (int i = 0; i < mBufferCount; i++)
		mSwapchainBuffer[i].Reset();
	mDepthBuffer.Reset();

	mCurrBackBuffer = 0;
	mCurrentFence = 0;

	ThrowIfFailed(mSwapchain->ResizeBuffers(mBufferCount, mClientWidth, mClientHeight, mBackBufferFormat, DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING | DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

	Log("Resizing called\n");

	CreateRenderTargetViews(false);

	ZeroMemory(&vp, sizeof(vp));
	ZeroMemory(&scissor, sizeof(scissor));

	vp.Width = (float)mClientWidth;
	vp.Height = (float)mClientHeight;
	vp.MaxDepth = 1.0f;

	scissor = { 0, 0, mClientWidth, mClientHeight };
}

void DXRenderer::Update(const GameTimer& GameTimer)
{
}

void DXRenderer::Draw(const GameTimer& GameTimer)
{
	ThrowIfFailed(mCmdAllocator->Reset());
	ThrowIfFailed(mCmdList->Reset(mCmdAllocator.Get(), nullptr));

	D3D12_RESOURCE_BARRIER presentToRender = {};
	presentToRender.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	presentToRender.Transition.pResource = mSwapchainBuffer[mCurrBackBuffer].Get();
	presentToRender.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	presentToRender.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

	mCmdList->ResourceBarrier(1u, &presentToRender);

	mCmdList->RSSetViewports(1u, &vp);
	mCmdList->RSSetScissorRects(1u, &scissor);

	D3D12_CPU_DESCRIPTOR_HANDLE currBack = CurrentBackBufferView();
	D3D12_CPU_DESCRIPTOR_HANDLE depth = DepthStencilView();

	FLOAT col[] = { sinf(mTimer.TotalTime()), -sinf(mTimer.TotalTime()), cosf(mTimer.TotalTime()), 1.0f};
	mCmdList->ClearRenderTargetView(currBack, col, 1, &scissor);
	mCmdList->ClearDepthStencilView(depth, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	D3D12_RESOURCE_BARRIER renderToPresent = {};
	renderToPresent.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	renderToPresent.Transition.pResource = mSwapchainBuffer[mCurrBackBuffer].Get();
	renderToPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	renderToPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;

	mCmdList->ResourceBarrier(1u, &renderToPresent);

	mCmdList->OMSetRenderTargets(1u, &currBack, TRUE, &depth);

	ThrowIfFailed(mCmdList->Close());

	ComPtr<ID3D12CommandList> cmdLists[] =
	{
		mCmdList.Get()
	};

	mCmdQueue->ExecuteCommandLists((UINT)std::size(cmdLists), cmdLists->GetAddressOf());

	mCurrentFence++;
	ThrowIfFailed(mCmdQueue->Signal(mFence.Get(), mCurrentFence));

	ThrowIfFailed(mSwapchain->Present(0u, DXGI_PRESENT_ALLOW_TEARING));

	mCurrBackBuffer = (mCurrBackBuffer + 1) % mBufferCount;

	FlushCommandQueue();
}

void DXRenderer::OnMouseDown(WPARAM btnState, int x, int y)
{
}

void DXRenderer::OnMouseUp(WPARAM btnState, int x, int y)
{
}

void DXRenderer::OnMouseMove(WPARAM btnState, int x, int y)
{
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

		std::stringstream ss;
		ss << "Found adapter: ";

		for (int i = 0; i < 128; i++)
		{
			if (desc.Description[i] != 0)
			{
				ss << (char)desc.Description[i];
			}
		}

		ss << "\n"
			<< "Dedicated video memory: " << ((float)desc.DedicatedVideoMemory / 1024.f / 1024.f / 1024.f) << "\n";

		Log(ss.str().c_str());

		if (desc.DedicatedVideoMemory > memSize) {
			memSize = (UINT)desc.DedicatedVideoMemory;
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

	D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;

	ThrowIfFailed(D3D12CreateDevice(mAdapter.Get(), featureLevel, IID_PPV_ARGS(&mDevice)));

	assert(mDevice.Get() != nullptr && "Failed to Create DX Device");

#ifdef _DEBUG
	ThrowIfFailed(mDevice.As(&mInfoQueue));

	//mInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
	//mInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);

	mInfoQueue->SetBreakOnCategory(D3D12_MESSAGE_CATEGORY_EXECUTION, FALSE);
	mInfoQueue->SetBreakOnCategory(D3D12_MESSAGE_CATEGORY_CLEANUP, FALSE);

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
	DWORD v = 0;
	ThrowIfFailed(mInfoQueue->RegisterMessageCallback(&messageCallback, D3D12_MESSAGE_CALLBACK_IGNORE_FILTERS, this, &v));
#endif
}

Microsoft::WRL::ComPtr<ID3D12Fence> DXRenderer::CreateFence()
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

void DXRenderer::FlushCommandQueue()
{
	if (mFence->GetCompletedValue() < mCurrentFence)
	{
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrentFence, mEventHandle));

		if (WaitForSingleObject(mEventHandle, DWORD_MAX))
		{
			Log("Failure at waiting\n");
		}
	}
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
	desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING | DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

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
	
	ThrowIfFailed(mDevice->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&mDsvHeap)));
	ThrowIfFailed(mDevice->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&mRtvHeap)));
}

D3D12_CPU_DESCRIPTOR_HANDLE DXRenderer::CurrentBackBufferView() const
{
	D3D12_CPU_DESCRIPTOR_HANDLE descHandle = mRtvHeap->GetCPUDescriptorHandleForHeapStart();
	descHandle.ptr += mCurrBackBuffer * mRtvDescriptorHeapSize;
	return descHandle;
}

D3D12_CPU_DESCRIPTOR_HANDLE DXRenderer::BackBufferViewByIndex(UINT index) const
{
	D3D12_CPU_DESCRIPTOR_HANDLE descHandle = mRtvHeap->GetCPUDescriptorHandleForHeapStart();
	descHandle.ptr += (SIZE_T)index * mRtvDescriptorHeapSize;
	return descHandle;
}

D3D12_CPU_DESCRIPTOR_HANDLE DXRenderer::DepthStencilView() const
{
	return mDsvHeap->GetCPUDescriptorHandleForHeapStart();
}

void DXRenderer::CreateRenderTargetViews(bool bResetCmdList)
{
	for (UINT i = 0; i < mBufferCount; i++)
	{
		ThrowIfFailed(mSwapchain->GetBuffer(i, IID_PPV_ARGS(&mSwapchainBuffer[i])));

		mDevice->CreateRenderTargetView(mSwapchainBuffer[i].Get(), nullptr, BackBufferViewByIndex(i));
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

	mDevice->CreateDepthStencilView(mDepthBuffer.Get(), nullptr, DepthStencilView());

	D3D12_RESOURCE_BARRIER depthBarrier = {};
	depthBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	depthBarrier.Transition.pResource = mDepthBuffer.Get();
	depthBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
	depthBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;

	if (bResetCmdList)
	{
		ThrowIfFailed(mCmdAllocator->Reset());
		ThrowIfFailed(mCmdList->Reset(mCmdAllocator.Get(), nullptr));
	}

	mCmdList->ResourceBarrier(1u, &depthBarrier);

	ID3D12CommandList* cmdLists[] = {
		mCmdList.Get()
	};

	ThrowIfFailed(mCmdList->Close());

	mCmdQueue->ExecuteCommandLists(1u, cmdLists);

	mCurrentFence++;
	ThrowIfFailed(mCmdQueue->Signal(mFence.Get(), mCurrentFence));

	FlushCommandQueue();
}

void DXRenderer::messageCallback(D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, LPCSTR pDescription, void* pContext)
{
	std::stringstream oss;
	DXRenderer* r = (DXRenderer*)pContext;
	if (!mStandardOutput || !r) return;
	switch (severity)
	{
	case D3D12_MESSAGE_SEVERITY_CORRUPTION:
		r->DirectXError("DirectX Corruption: ", pDescription);
		break;
	case D3D12_MESSAGE_SEVERITY_ERROR:
		r->DirectXError("DirectX Error: ", pDescription);
		break;
	case D3D12_MESSAGE_SEVERITY_WARNING:
#ifdef _WARNINGS_AS_CRASH
		r->DirectXError("DirectX Warning: ", pDescription);
		break;
#endif
		oss << "DirectX Warning: " << pDescription << "\n";
		Log(oss.str().c_str());
		break;
	case D3D12_MESSAGE_SEVERITY_INFO:
		oss << "DirectX Info: " << pDescription << "\n";
		//Log(oss.str().c_str());
		break;
	case D3D12_MESSAGE_SEVERITY_MESSAGE:
		oss << "DirectX Message: " << pDescription << "\n";
		//Log(oss.str().c_str());
		break;
	default:
		break;
	}
}

void DXRenderer::DirectXError(const char* _s0, const char* _s1)
{
	mExceptionSettings.push({ _s0, _s1 });
	mHasException = true;
}