#pragma once

#include <string>
#include <sstream>
#include <malloc.h>

#ifdef _DEBUG
#define ThrowIfFailed(x)\
{\
	HRESULT hres = (x);\
	if (FAILED(hres)) {\
		std::stringstream oss;\
		oss << "DirectX12 Runtime encountered an error: " << std::hex << "0x" << hres << " at:\n\n";\
		oss << #x << "\n\n";\
		UINT64 messageCount = mInfoQueue->GetNumStoredMessages();\
		for (UINT64 i = 0; i < messageCount; i++) {\
			SIZE_T messageLen = 0;\
			mInfoQueue->GetMessageW(i, nullptr, &messageLen);\
			D3D12_MESSAGE* msg = (D3D12_MESSAGE*)malloc(messageLen);\
			mInfoQueue->GetMessageW(i, msg, &messageLen);\
			if (messageLen > 0)\
				oss << msg->pDescription << "\n";\
			free((void*)msg);\
		}\
		throw std::exception(oss.str().c_str());\
		mInfoQueue->ClearStoredMessages();\
	}\
}
#else
#define ThrowIfFailed(x) (x)
#endif
