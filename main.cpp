#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

#include "DXRenderer.h"
#include "DXException.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
	int returnValue = 0;
	DXRenderer renderer(hInstance);
	try
	{
		returnValue = renderer.Run();
	}
	catch (DXException e)
	{
		MessageBoxA(nullptr, e.what(), "DirectX Failed", MB_OK | MB_ICONEXCLAMATION);
	}
	return returnValue;
}