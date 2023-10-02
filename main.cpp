#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

#include "DXRenderer.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
	int returnValue = 0;
	try
	{
		DXRenderer renderer(hInstance);
		returnValue = renderer.Run();
	}
	catch (std::exception& e)
	{
		MessageBoxA(nullptr, e.what(), "Exception", MB_OK | MB_ICONEXCLAMATION);
	}
	return returnValue;
}