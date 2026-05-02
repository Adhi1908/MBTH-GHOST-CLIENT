#include "main.h"

#include "base/base.h"

void Main::Init()
{
	// Wait for the game window to be created to ensure Lunar Client is fully initialized.
	// This prevents ConcurrentModificationException when accessing classloaders early.
	while (!FindWindowA("LWJGL", nullptr))
	{
		Sleep(100);
	}
	
	// Add a small delay to ensure all bootstrap threads are finished modifying classloaders.
	Sleep(3000);

	Base::Init();
}

void Main::Shutdown()
{
	Base::Shutdown();
	FreeLibraryAndExitThread(Main::hModule, 0);
}

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD dwReason, LPVOID lpReserved)
{
	if (dwReason == DLL_PROCESS_ATTACH)
	{
		Main::hModule = hModule;
		DisableThreadLibraryCalls(hModule);
		HANDLE hThread = CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(Main::Init), hModule, 0, nullptr);

		if (hThread) CloseHandle(hThread);
	}

	return TRUE;
}