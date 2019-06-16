#include "Stress.h"
#include <locale.h>
int main()
{
	setlocale(LC_ALL, "");
	timeBeginPeriod(1);

	DWORD timeTickCnt;
	DWORD dwTick;

	NetworkInit();

	timeTickCnt = GetTickCount64();
	dwTick = timeTickCnt;

	while (1)
	{
		DummyConnect();
		DummyProcess();

		NetworkProcess();


		dwTick = GetTickCount64();
		if (dwTick - timeTickCnt  > 1000)
		{
			Monitoring(dwTick);

			dwTick = 0;
			timeTickCnt = GetTickCount64();
		}
	}
}