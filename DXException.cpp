#include "DXException.h"

#include <sstream>
#include <format>

#define HrToString(hr) #hr

DXException::DXException(ID3D12InfoQueue* d3d12InfoQueue, HRESULT hr, const char* fileName, const char* line, int number)
{
	std::stringstream oss;
	oss << "DXException: " << std::hex << "0x" << hr << "\n";
	oss << "[FILE]: " << fileName << ":" << number << "\n\n";
	oss << std::format("[CODE SECTION]: {}\n", line);
	oss << "[ERROR]: ";

	UINT64 messageCount = d3d12InfoQueue->GetNumStoredMessages();
	for (UINT64 i = 0; i < messageCount; i++) {
		SIZE_T messageLen = 0;
		d3d12InfoQueue->GetMessageW(i, nullptr, &messageLen);
		D3D12_MESSAGE* msg = (D3D12_MESSAGE*)malloc(messageLen);
		d3d12InfoQueue->GetMessageW(i, msg, &messageLen);
		if (messageLen > 0)
			oss << msg->pDescription << "\n";
		oss << msg->pDescription;
		free((void*)msg);
	}
	errorMessage = oss.str();
}

DXException::DXException(const char* customTag, const char* message)
{
	std::stringstream oss;
	oss << customTag << message;
	errorMessage = oss.str();
}
