#include <windows.h>
#include <stdio.h>
/* closes the game's window, so that it writes its closing report (live_stop.sh). The class is "h2classic" since s10d
 * ("h2native" before). */
int main(void)
{
	HWND w = FindWindowA("h2classic", NULL);
	if (!w) w = FindWindowA("h2native", NULL);
	printf("window %p\n", (void *)w);
	if (w) PostMessageA(w, WM_CLOSE, 0, 0);
	return 0;
}
