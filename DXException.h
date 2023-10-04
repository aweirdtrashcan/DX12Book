//
// Created by Diego on 04/10/2023.
//

#ifndef DIRECTX12_DXEXCEPTION_H
#define DIRECTX12_DXEXCEPTION_H

#include <exception>
#include <string>
#include <d3d12.h>
#include <d3d12sdklayers.h>

#define DXExcept(infoQueue, hr, fileName, lineNumber) { throw DXException(infoQueue, hr, fileName, #hr, lineNumber); }

class DXException : public std::exception
{
public:
    explicit DXException(ID3D12InfoQueue* d3d12InfoQueue, HRESULT hr, const char* fileName, const char* line, int number);
    explicit DXException(const char* customTag, const char* message);
    virtual ~DXException() {};

    virtual const char* what() const noexcept override { return errorMessage.c_str(); }

private:
    std::string errorMessage;
};


#endif //DIRECTX12_DXEXCEPTION_H
