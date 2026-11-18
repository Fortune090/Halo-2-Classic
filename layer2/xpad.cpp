/* H2Classic: the console's controller calls. Up to four gamepads (port 1 always plugged in); their state comes from
 * input.c (a real controller or the keyboard). Compiled with the Xbox SDK's headers so the structures are the SDK's. */
#include <xtl.h>

extern "C" {
void __cdecl h2n_log(const char *format, ...);
unsigned __cdecl h2n_next_sequence(void);
struct h2n_pad { unsigned buttons; unsigned char analog[8]; short lx, ly, rx, ry; };
void __cdecl h2n_pad_read(h2n_pad *pad);

void __cdecl h2n_pad_read_port(unsigned port, h2n_pad *pad);
int __cdecl h2n_pad_connected(unsigned port);
void __cdecl h2n_pad_rumble(unsigned port, unsigned left, unsigned right);

/* Up to four gamepads, in the ports of their numbers: port 1 always (the PC's first controller, the keyboard and the test
 * presses), ports 2-4 while the PC's controllers 2-4 are connected - reported to the game as plugged in and pulled out,
 * as the console does, so split screen can have a player on each. */
static DWORD g_reported_mask;                    /* the ports the game has been told hold a gamepad */
static bool g_open[4];
static XINPUT_GAMEPAD g_last[4];
static DWORD g_packet[4];
#define PAD_HANDLE ((HANDLE)0x48324e31)          /* port 1's; port n's is this + n */
static int handle_port(HANDLE h) { DWORD d = (DWORD)h - (DWORD)PAD_HANDLE; return d < 4 ? (int)d : -1; }

void WINAPI XInitDevices(DWORD dwPreallocTypeCount, PXDEVICE_PREALLOC_TYPE PreallocTypes) { }

BOOL WINAPI XGetDeviceChanges(PXPP_DEVICE_TYPE DeviceType, PDWORD pdwInsertions, PDWORD pdwRemovals)
{
	*pdwInsertions = 0; *pdwRemovals = 0;
	if (DeviceType != XDEVICE_TYPE_GAMEPAD) return FALSE;
	DWORD now = 0;
	for (DWORD port = 0; port < 4; port++) if (h2n_pad_connected(port)) now |= 1u << port;   /* XDEVICE_PORTn_MASK: bit n */
	if (now == g_reported_mask) return FALSE;
	*pdwInsertions = now & ~g_reported_mask; *pdwRemovals = g_reported_mask & ~now;
	for (DWORD port = 0; port < 4; port++)
		if ((*pdwInsertions | *pdwRemovals) & (1u << port))
			h2n_log("[%4u] controller: a gamepad %s port %lu\n", h2n_next_sequence(), (*pdwInsertions & (1u << port)) ? "plugged into" : "pulled out of", port + 1);
	g_reported_mask = now;
	return TRUE;
}

HANDLE WINAPI XInputOpen(PXPP_DEVICE_TYPE DeviceType, DWORD dwPort, DWORD dwSlot, PXINPUT_POLLING_PARAMETERS pPollingParameters)
{
	if (DeviceType != XDEVICE_TYPE_GAMEPAD || dwPort > 3 || !(g_reported_mask & (1u << dwPort))) { SetLastError(ERROR_DEVICE_NOT_CONNECTED); return NULL; }
	g_open[dwPort] = true;
	return (HANDLE)((DWORD)PAD_HANDLE + dwPort);
}

void WINAPI XInputClose(HANDLE hDevice) { int port = handle_port(hDevice); if (port >= 0) g_open[port] = false; }

DWORD WINAPI XInputGetCapabilities(HANDLE hDevice, PXINPUT_CAPABILITIES pCapabilities)
{
	memset(pCapabilities, 0, sizeof(*pCapabilities));
	if (handle_port(hDevice) < 0) return ERROR_DEVICE_NOT_CONNECTED;
	pCapabilities->SubType = XINPUT_DEVSUBTYPE_GC_GAMEPAD;
	pCapabilities->In.Gamepad.wButtons = 0xff;
	memset(pCapabilities->In.Gamepad.bAnalogButtons, 0xff, 8);
	pCapabilities->In.Gamepad.sThumbLX = pCapabilities->In.Gamepad.sThumbLY = pCapabilities->In.Gamepad.sThumbRX = pCapabilities->In.Gamepad.sThumbRY = (SHORT)0xffff;
	pCapabilities->Out.Rumble.wLeftMotorSpeed = pCapabilities->Out.Rumble.wRightMotorSpeed = 0xffff;
	return ERROR_SUCCESS;
}

DWORD WINAPI XInputGetState(HANDLE hDevice, PXINPUT_STATE pState)
{
	h2n_pad pad;
	XINPUT_GAMEPAD now;
	int port = handle_port(hDevice);
	memset(pState, 0, sizeof(*pState));
	if (port < 0) return ERROR_DEVICE_NOT_CONNECTED;
	h2n_pad_read_port((unsigned)port, &pad);
	memset(&now, 0, sizeof(now));
	now.wButtons = (WORD)pad.buttons;
	memcpy(now.bAnalogButtons, pad.analog, 8);
	now.sThumbLX = pad.lx; now.sThumbLY = pad.ly; now.sThumbRX = pad.rx; now.sThumbRY = pad.ry;
	if (memcmp(&now, &g_last[port], sizeof(now)))
	{
		static unsigned logged;
		g_last[port] = now; g_packet[port]++;
		if (logged < 40 && (now.wButtons || now.bAnalogButtons[0] || now.bAnalogButtons[1]))
		{
			logged++;
			h2n_log("       controller %d: buttons %#x, A %u B %u X %u Y %u\n", port + 1, now.wButtons, now.bAnalogButtons[0], now.bAnalogButtons[1], now.bAnalogButtons[2], now.bAnalogButtons[3]);
		}
	}
	pState->dwPacketNumber = g_packet[port];
	pState->Gamepad = now;
	return ERROR_SUCCESS;
}

/* s10l: the console's (the SDK's xapilib, xidinp.obj; retail 0x409bdf and 0x40a8f2): a gamepad that is gone gets
 * ERROR_DEVICE_NOT_CONNECTED in the header and as the result; otherwise the motor speeds go to the controller, the header
 * holds the transfer's state (ERROR_IO_PENDING until the controller has them, then ERROR_SUCCESS) and the call returns
 * what the header holds. Here the speeds are with the PC's controller (input.c) before the call returns, so the header
 * holds ERROR_SUCCESS at once. The game sends again only once its header reads ERROR_SUCCESS (function_124ee0) and gives
 * no event to set. */
DWORD WINAPI XInputSetState(HANDLE hDevice, PXINPUT_FEEDBACK pFeedback)
{
	int port = handle_port(hDevice);
	if (port < 0 || !g_open[port] || !(g_reported_mask & (1u << port)))
	{
		pFeedback->Header.dwStatus = ERROR_DEVICE_NOT_CONNECTED;
		return ERROR_DEVICE_NOT_CONNECTED;
	}
	h2n_pad_rumble((unsigned)port, pFeedback->Rumble.wLeftMotorSpeed, pFeedback->Rumble.wRightMotorSpeed);
	pFeedback->Header.dwStatus = ERROR_SUCCESS;
	return pFeedback->Header.dwStatus;
}

}
