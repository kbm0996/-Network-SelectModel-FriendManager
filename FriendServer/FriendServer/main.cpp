#include "NetworkProcess.h"
#include <locale.h>

extern UINT64 g_Connect;

int main()
{
	DWORD dwTick = (DWORD)GetTickCount64();
	setlocale(LC_ALL, "");

	if (!NetworkInit())
	{
		wprintf(L"Network Initialize Error\n");
		return -1;
	}

	while (1)
	{
		NetworkProcess();
		if (!ServerControl())
			break;
		if (timeGetTime() - dwTick >= 1000)
		{
			wprintf(L"Connect: %lld\n", g_Connect);
			dwTick = timeGetTime();
		}
	}

	NetworkClean();
	return 0;
}