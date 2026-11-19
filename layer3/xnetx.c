/* xnetx.c - the game's socket and XNet calls (s9u).
 *
 * The game's own code calls 34 socket and XNet functions by name; nettramp.S gives each name a jump through
 * h2n_net_ptr[], set here before the game runs:
 *   - by default (as s9t): Windows' Winsock for the socket calls, xnet.c's PC versions for socket() and the XNet calls,
 *     and the stand-ins below for the five XNet calls nothing on a PC answers (QoS, TsAddr). Retail's copies of these
 *     functions jump to the same names (retail.c), as before.
 *   - H2N_XNET=retail: the XBE's own network library (retail's copies, left as they are). It is the console's: the
 *     secure, encrypted system link that a real Xbox and xemu speak. It runs on the kernel services in xkernel.c and
 *     reaches the wire through the path the console's debug monitor uses: the library asks the processor block
 *     (fs:[0x20]) for the monitor's network interface and, when one is there, sends and receives Ethernet frames
 *     through it instead of driving the network chip. Here that interface is this file: every frame goes out as one UDP
 *     datagram to H2N_XNET_PEER, and every datagram received on H2N_XNET_PORT comes in as one frame - xemu's "UDP
 *     tunnel" (one raw Ethernet frame per datagram).
 *
 * The interface (the library's side, retail v1.0): NicInit 0x3d6726 reads [prcb+0x24c]; the object it finds has its
 * table at +0, 'XBD6' at +4 ('XBDV' at +8 and a version at +12 would ask for two more calls, 0x1c and 0x20; zero here).
 * Its calls, all with the object in ecx, callee-popped:
 *   +0x04 attach(a, b, BYTE mac[6] out, DWORD *out) -> HRESULT    NicInit 0x3d67a6; *out != 0 sets another init flag
 *   +0x08 start(client)                                            0x3d602c; client = nic + 0x264
 *   +0x0c detach()                                                 0x3d65a8
 *   +0x14 send(packet, frame, length)                              0x3d62da; frame = the Ethernet header (0x3d03ea)
 *   +0x18 can_send() -> BOOL                                       0x3d5fe9 (its caller 0x3dce4e stops sending on 0)
 * The client's calls (the library's, retail code, thiscall on the client):
 *   0x3d6660 sent(packet)          -> the packet's own completion routine [packet+0x10] (ecx = nic)
 *   0x3d6671 received(broadcast, data after the 14-byte header, data length, the header's type word as stored)
 *   0x3d66b3 push()                -> 0x3dce32, which sends what waited for can_send()
 * Everything the client is called with runs at DPC level (xkernel.c's DPC thread), as the console's NIC DPC does. */
#include <winsock2.h>
#ifndef H2N_VA                           /* (s10r) one program for both game versions: the address is picked at run time */
extern int h2n_v15;                     /* main.c: 0 = game version 1.0 (the disc's default.xbe), 1 = 1.5 (the title update's) */
#define H2N_VA(v10, v15) (h2n_v15 ? (v15) : (v10))
#define H2N_VA2(v10, v15) { (v10), (v15) }   /* in a table: both, picked with [h2n_v15] */
#endif
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void h2n_log(const char *format, ...);
void h2n_fatal(const char *format, ...);
unsigned h2n_next_sequence(void);
int h2n_env_int(const char *name, int otherwise);
int h2n_retail_code_loaded(void);
int h2n_have_certificate_key(void);
void h2n_dpc_wake(void);

/* ---- which network ---- */
int h2n_xnet_retail(void)
{
	static int mode = -1;
	if (mode < 0) { const char *v = getenv("H2N_XNET"); mode = v && !_stricmp(v, "retail"); }
	return mode;
}

/* ---- the game's net calls (nettramp.S jumps through h2n_net_ptr[i]) ---- */
SOCKET __stdcall pc_socket(int, int, int) __asm__("_h2n_pc_socket@12");
INT __stdcall pc_XNetStartup(const void *) __asm__("_h2n_pc_XNetStartup@4");
INT __stdcall pc_XNetCleanup(void) __asm__("_h2n_pc_XNetCleanup@0");
DWORD __stdcall pc_XNetGetTitleXnAddr(void *) __asm__("_h2n_pc_XNetGetTitleXnAddr@4");
DWORD __stdcall pc_XNetGetEthernetLinkStatus(void) __asm__("_h2n_pc_XNetGetEthernetLinkStatus@0");
INT __stdcall pc_XNetCreateKey(void *, void *) __asm__("_h2n_pc_XNetCreateKey@8");
INT __stdcall pc_XNetRegisterKey(const void *, const void *) __asm__("_h2n_pc_XNetRegisterKey@8");
INT __stdcall pc_XNetUnregisterKey(const void *) __asm__("_h2n_pc_XNetUnregisterKey@4");
INT __stdcall pc_XNetXnAddrToInAddr(const void *, const void *, DWORD *) __asm__("_h2n_pc_XNetXnAddrToInAddr@12");
INT __stdcall pc_XNetInAddrToXnAddr(DWORD, void *, void *) __asm__("_h2n_pc_XNetInAddrToXnAddr@12");
INT __stdcall pc_XNetConnect(DWORD) __asm__("_h2n_pc_XNetConnect@4");
DWORD __stdcall pc_XNetGetConnectStatus(DWORD) __asm__("_h2n_pc_XNetGetConnectStatus@4");
int __stdcall pc_XNetRandom(BYTE *, UINT) __asm__("_h2n_pc_XNetRandom@8");

/* the five XNet calls nothing answers on a PC: as the generated stand-ins answered them before s9u (0, or WSAENETDOWN),
   now also clearing what they hand back */
static void stand_in(const char *name, LONG *told) { if (InterlockedIncrement(told) == 1) h2n_log("[%4u] network: %s (not on a PC)\n", h2n_next_sequence(), name); }
static INT WINAPI pc_XNetQosListen(const void *kid, const BYTE *data, UINT size, DWORD bits, DWORD flags)
{ static LONG told; (void)kid; (void)data; (void)size; (void)bits; (void)flags; stand_in("XNetQosListen", &told); return 0; }
static INT WINAPI pc_XNetQosLookup(UINT n, const void **addresses, const void **kids, const void **keys, UINT m, const void *ina,
	const DWORD *services, UINT probes, DWORD bits, DWORD flags, HANDLE event, void **qos)
{
	static LONG told;
	(void)n; (void)addresses; (void)kids; (void)keys; (void)m; (void)ina; (void)services; (void)probes; (void)bits; (void)flags; (void)event;
	stand_in("XNetQosLookup", &told);
	if (qos) *qos = NULL;
	return WSAENETDOWN;
}
static INT WINAPI pc_XNetQosRelease(void *qos) { static LONG told; (void)qos; stand_in("XNetQosRelease", &told); return 0; }
static INT WINAPI pc_XNetQosServiceLookup(DWORD flags, HANDLE event, void **qos)
{ static LONG told; (void)flags; (void)event; stand_in("XNetQosServiceLookup", &told); if (qos) *qos = NULL; return WSAENETDOWN; }
static INT WINAPI pc_XNetTsAddrToInAddr(const void *ts, DWORD service, const void *kid, DWORD *ina)
{ static LONG told; (void)ts; (void)service; (void)kid; stand_in("XNetTsAddrToInAddr", &told); if (ina) *ina = 0; return WSAENETDOWN; }

#define NET_COUNT 34
void *h2n_net_ptr[NET_COUNT];
#undef H2N_VA
#define H2N_VA(v10, v15) { (v10), (v15) }      /* (s10r) in this table: both versions' addresses, picked with [h2n_v15] */
static const struct { const char *name; const char *winsock; void *pc; DWORD retail[2]; } g_net[NET_COUNT] = {
	{ "socket", NULL, pc_socket, H2N_VA(0x3cd188, 0x3d7268) }, { "bind", "bind", NULL, H2N_VA(0x3cd1d2, 0x3d72b2) }, { "sendto", "sendto", NULL, H2N_VA(0x3cd254, 0x3d7334) },
	{ "recvfrom", "recvfrom", NULL, H2N_VA(0x3cd22b, 0x3d730b) }, { "recv", "recv", NULL, H2N_VA(0x3cd211, 0x3d72f1) }, { "send", "send", NULL, H2N_VA(0x3cd23a, 0x3d731a) },
	{ "select", "select", NULL, H2N_VA(0x3cd1e8, 0x3d72c8) }, { "ioctlsocket", "ioctlsocket", NULL, H2N_VA(0x3cd1a9, 0x3d7289) }, { "setsockopt", "setsockopt", NULL, H2N_VA(0x3cd1b4, 0x3d7294) },
	{ "getsockopt", "getsockopt", NULL, H2N_VA(0x3cd1c3, 0x3d72a3) }, { "closesocket", "closesocket", NULL, H2N_VA(0x3cd193, 0x3d7273) }, { "connect", "connect", NULL, H2N_VA(0x3cd1dd, 0x3d72bd) },
	{ "shutdown", "shutdown", NULL, H2N_VA(0x3cd19e, 0x3d727e) }, { "__WSAFDIsSet", "__WSAFDIsSet", NULL, H2N_VA(0x3cd71d, 0x3d77fd) }, { "WSAStartup", "WSAStartup", NULL, H2N_VA(0x3cd58d, 0x3d766d) },
	{ "WSACleanup", "WSACleanup", NULL, H2N_VA(0x3cd688, 0x3d7768) }, { "WSAGetLastError", "WSAGetLastError", NULL, H2N_VA(0x3cd718, 0x3d77f8) },
	{ "XNetStartup", NULL, pc_XNetStartup, H2N_VA(0x3cd576, 0x3d7656) }, { "XNetCleanup", NULL, pc_XNetCleanup, H2N_VA(0x3cd67b, 0x3d775b) },
	{ "XNetGetTitleXnAddr", NULL, pc_XNetGetTitleXnAddr, H2N_VA(0x3cd17d, 0x3d725d) }, { "XNetGetEthernetLinkStatus", NULL, pc_XNetGetEthernetLinkStatus, H2N_VA(0x36c47d, 0x37654d) },
	{ "XNetCreateKey", NULL, pc_XNetCreateKey, H2N_VA(0x3cd0cb, 0x3d71ab) }, { "XNetRegisterKey", NULL, pc_XNetRegisterKey, H2N_VA(0x3cd0d6, 0x3d71b6) },
	{ "XNetUnregisterKey", NULL, pc_XNetUnregisterKey, H2N_VA(0x3cd0e1, 0x3d71c1) }, { "XNetXnAddrToInAddr", NULL, pc_XNetXnAddrToInAddr, H2N_VA(0x3cd0ec, 0x3d71cc) },
	{ "XNetInAddrToXnAddr", NULL, pc_XNetInAddrToXnAddr, H2N_VA(0x3cd32d, 0x3d740d) },
	/* retail's XNetConnect is 0x3cd34f (it starts the key exchange: 0x3d3a13); 0x3cd344, which the library list also names
	   XNetConnect, is XNetUnregisterInAddr (0x3d3699 drops the association). The game calls only XNetConnect. */
	{ "XNetConnect", NULL, pc_XNetConnect, H2N_VA(0x3cd34f, 0x3d742f) }, { "XNetGetConnectStatus", NULL, pc_XNetGetConnectStatus, H2N_VA(0x3cd35a, 0x3d743a) },
	{ "XNetQosListen", NULL, pc_XNetQosListen, H2N_VA(0x3cd147, 0x3d7227) }, { "XNetQosLookup", NULL, pc_XNetQosLookup, H2N_VA(0x3cd156, 0x3d7236) },
	{ "XNetQosRelease", NULL, pc_XNetQosRelease, H2N_VA(0x3cd172, 0x3d7252) }, { "XNetQosServiceLookup", NULL, pc_XNetQosServiceLookup, H2N_VA(0x3cd365, 0x3d7445) },
	{ "XNetTsAddrToInAddr", NULL, pc_XNetTsAddrToInAddr, H2N_VA(0x3cd103, 0x3d71e3) }, { "XNetRandom", NULL, pc_XNetRandom, H2N_VA(0x3cd0c0, 0x3d71a0) },
};
#undef H2N_VA
#define H2N_VA(v10, v15) (h2n_v15 ? (v15) : (v10))

/* retail.c: in retail mode retail's copies of these stay retail's (the library patch is not made) */
int h2n_xnet_keeps_retail(const char *decorated)
{
	char name[48];
	const char *p = decorated + (decorated[0] == '_');
	size_t n = strcspn(p, "@");
	if (!h2n_xnet_retail() || n >= sizeof(name)) return 0;
	memcpy(name, p, n); name[n] = 0;
	for (unsigned i = 0; i < NET_COUNT; i++) if (!strcmp(g_net[i].name, name)) return 1;
	return 0;
}

/* ---- the tunnel ---- */
typedef SOCKET (WSAAPI *socket_fn)(int, int, int);
typedef int (WSAAPI *bind_fn)(SOCKET, const struct sockaddr *, int);
typedef int (WSAAPI *sendto_fn)(SOCKET, const char *, int, int, const struct sockaddr *, int);
typedef int (WSAAPI *recvfrom_fn)(SOCKET, char *, int, int, struct sockaddr *, int *);
typedef int (WSAAPI *startup_fn)(WORD, WSADATA *);
typedef int (WSAAPI *lasterror_fn)(void);
typedef struct hostent *(WSAAPI *gethost_fn)(const char *);
static sendto_fn w_sendto;
static recvfrom_fn w_recvfrom;
static lasterror_fn w_lasterror;
static SOCKET g_tunnel = INVALID_SOCKET;
static struct sockaddr_in g_peer;
static BYTE g_mac[6];
static volatile LONG g_link;                    /* the tunnel or the LAN is up */
static int g_lan;                               /* (s10x) the players' programs carry each other's frames over the PC's network (xlan.c) */
static unsigned g_lan_port;                      /* this program's port there (before the console address is made) */
unsigned h2n_lan_bind(void);
int h2n_lan_open(const BYTE mac[6], void (*frame_in)(const BYTE *, int));
int h2n_lan_send(const BYTE *frame, unsigned length);
static volatile LONG g_lan_sent, g_lan_received, g_lan_noted;
static void *volatile g_client;                 /* nic + 0x264, once the library started receiving */

#define FRAME_MAX 1536
#define RX_SLOTS 256
#define TX_SLOTS 512
static CRITICAL_SECTION g_q;
static struct { WORD length; BYTE data[FRAME_MAX]; } g_rx[RX_SLOTS];
static unsigned g_rx_head, g_rx_count;
static void *g_tx[TX_SLOTS];
static unsigned g_tx_head, g_tx_count;
static volatile LONG g_sent, g_sent_bytes, g_received, g_received_bytes, g_ignored, g_dropped, g_send_failed;

DWORD h2n_xnetx_link_state(void) { return g_link ? 0x0b : 0; }     /* active, 100 Mbit/s, full duplex (XNET_ETHERNET_LINK_*) */

static void describe(char *out, size_t size, const BYTE *f, unsigned length)
{	/* one line about a frame: addresses, type, and for IPv4 the addresses and UDP ports */
	WORD type = (WORD)(f[12] << 8 | f[13]);
	int n = _snprintf(out, size, "%02x:%02x:%02x:%02x:%02x:%02x <- %02x:%02x:%02x:%02x:%02x:%02x type %04x, %u bytes",
		f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10], f[11], type, length);
	if (type == 0x0800 && length >= 34 && n > 0 && (size_t)n < size)
	{
		const BYTE *ip = f + 14;
		unsigned header = (ip[0] & 15) * 4;
		n += _snprintf(out + n, size - n, ": %u.%u.%u.%u -> %u.%u.%u.%u protocol %u", ip[12], ip[13], ip[14], ip[15], ip[16], ip[17], ip[18], ip[19], ip[9]);
		if (ip[9] == 17 && length >= 14 + header + 8 && n > 0 && (size_t)n < size)
			_snprintf(out + n, size - n, " port %u -> %u", ip[header] << 8 | ip[header + 1], ip[header + 2] << 8 | ip[header + 3]);
	}
	else if (type == 0x0806 && length >= 42 && n > 0 && (size_t)n < size)
		_snprintf(out + n, size - n, ": ARP %s %u.%u.%u.%u -> %u.%u.%u.%u", f[21] == 1 ? "who-has" : "is-at",
			f[28], f[29], f[30], f[31], f[38], f[39], f[40], f[41]);
}
static void note(const char *what, const BYTE *f, unsigned length, LONG count)
{
	char line[200];
	if (count > 40) return;
	describe(line, sizeof(line), f, length);
	h2n_log("[%4u] xnet frame: %s %s%s\n", h2n_next_sequence(), what, line, count == 40 ? " (the last one shown)" : "");
}

/* a frame that arrived, from the tunnel or (lan) from the PC's network: queued for the library's DPC */
static void frame_in(const BYTE *buffer, int n, int lan)
{
	int broadcast, ours;
	if (n < 14 || n > FRAME_MAX) { InterlockedIncrement(&g_ignored); return; }
	/* the network chip takes frames for its own address and broadcasts (0x3d6726's hardware path); and not our own */
	broadcast = !memcmp(buffer, "\xff\xff\xff\xff\xff\xff", 6);
	ours = !memcmp(buffer, g_mac, 6);
	if ((!broadcast && !ours) || !memcmp(buffer + 6, g_mac, 6)) { InterlockedIncrement(&g_ignored); return; }
	InterlockedIncrement(&g_received);
	if (!lan) note("in: ", buffer, (unsigned)n, g_received);
	else
	{	/* a LAN is full of other machines' broadcasts: of those only the consoles' are noted (UDP port 3074, where system
		   link speaks), and everything sent to the library itself */
		int link = n >= 38 && buffer[12] == 0x08 && buffer[13] == 0x00 && buffer[23] == 17 && (buffer[14] & 15) == 5 && ((buffer[36] << 8 | buffer[37]) == 3074 || (buffer[34] << 8 | buffer[35]) == 3074);
		InterlockedIncrement(&g_lan_received);
		if (ours || link) note("in (LAN): ", buffer, (unsigned)n, InterlockedIncrement(&g_lan_noted));
	}
	InterlockedExchangeAdd(&g_received_bytes, n);
	EnterCriticalSection(&g_q);
	if (g_rx_count < RX_SLOTS)
	{
		unsigned i = (g_rx_head + g_rx_count++) % RX_SLOTS;
		g_rx[i].length = (WORD)n;
		memcpy(g_rx[i].data, buffer, (size_t)n);
	}
	else InterlockedIncrement(&g_dropped);
	LeaveCriticalSection(&g_q);
	if (g_client) h2n_dpc_wake();
}
static void lan_frame(const BYTE *frame, int length) { frame_in(frame, length, 1); }
static DWORD WINAPI receive_thread(void *unused)
{
	static BYTE buffer[2048];
	(void)unused;
	for (;;)
	{
		struct sockaddr_in from;
		int from_size = sizeof(from), n = w_recvfrom(g_tunnel, (char *)buffer, sizeof(buffer), 0, (struct sockaddr *)&from, &from_size);
		if (n == SOCKET_ERROR)
		{
			int e = w_lasterror();
			if (e == WSAECONNRESET || e == WSAEMSGSIZE) continue;         /* (an ICMP "port unreachable" from the peer; a jumbo datagram) */
			h2n_log("[%4u] xnet tunnel: receiving stopped (Winsock error %d)\n", h2n_next_sequence(), e);
			return 0;
		}
		frame_in(buffer, n, 0);
	}
}

/* ---- the interface object ---- */
typedef struct dm_interface { void **table; DWORD signature, extended, version; } dm_interface;
static HRESULT __thiscall dm_attach(dm_interface *self, DWORD a, DWORD b, BYTE *mac, DWORD *out)
{
	(void)self;
	memcpy(mac, g_mac, 6);
	if (out) *out = 0;
	h2n_log("[%4u] xnet: the network library attached to the link (%lu, %lu); its address %02x:%02x:%02x:%02x:%02x:%02x\n", h2n_next_sequence(),
		a, b, g_mac[0], g_mac[1], g_mac[2], g_mac[3], g_mac[4], g_mac[5]);
	return 0;
}
static HRESULT __thiscall dm_start(dm_interface *self, void *client)
{
	(void)self;
	g_client = client;
	h2n_log("[%4u] xnet: the network library is receiving (client %p)\n", h2n_next_sequence(), client);
	h2n_dpc_wake();
	return 0;
}
static void __thiscall dm_detach(dm_interface *self)
{
	(void)self;
	g_client = NULL;
	h2n_log("[%4u] xnet: the network library let go of the tunnel\n", h2n_next_sequence());
}
static void __thiscall dm_send(dm_interface *self, void *packet, BYTE *frame, DWORD length)
{
	LONG count;
	(void)self;
	if ((g_tunnel != INVALID_SOCKET || g_lan) && length <= FRAME_MAX)
	{
		BYTE padded[60];
		if (length < 60)
		{	/* the chip pads a short frame to Ethernet's least (60 bytes before the checksum) */
			memcpy(padded, frame, length); memset(padded + length, 0, 60 - length);
			frame = padded; length = 60;
		}
		if (g_tunnel != INVALID_SOCKET && w_sendto(g_tunnel, (const char *)frame, (int)length, 0, (struct sockaddr *)&g_peer, sizeof(g_peer)) == SOCKET_ERROR)
			InterlockedIncrement(&g_send_failed);
		if (g_lan) { if (h2n_lan_send(frame, length)) InterlockedIncrement(&g_lan_sent); }   /* (s10x) onto the PC's network (xlan.c counts what it could not send) */
		count = InterlockedIncrement(&g_sent);
		InterlockedExchangeAdd(&g_sent_bytes, (LONG)length);
		note("out:", frame, length, count);
	}
	/* the frame has been copied out: the packet is complete (handed back from the DPC, as the chip's interrupt does) */
	EnterCriticalSection(&g_q);
	if (g_tx_count < TX_SLOTS) g_tx[(g_tx_head + g_tx_count++) % TX_SLOTS] = packet;
	else h2n_fatal("xnet: more than %d frames sent without the DPC running", TX_SLOTS);
	LeaveCriticalSection(&g_q);
	h2n_dpc_wake();
}
static BOOL __thiscall dm_can_send(dm_interface *self) { (void)self; return TRUE; }
static void __thiscall dm_unknown(dm_interface *self) { (void)self; h2n_fatal("xnet: the network library made a call to the debug monitor's interface that is not known"); }
static void *g_dm_table[9] = { dm_unknown, dm_attach, dm_start, dm_detach, dm_unknown, dm_send, dm_can_send, dm_unknown, dm_unknown };
static dm_interface g_dm = { g_dm_table, 0x58424436, 0, 0 };     /* 'XBD6'; not 'XBDV' (no calls 0x1c, 0x20) */
static BYTE g_prcb[0x300];                                       /* what fs:[0x20] gives: the processor block; +0x24c the interface */

/* ---- at DPC level (xkernel.c's DPC thread) ---- */
typedef void (__thiscall *sent_fn)(void *client, void *packet);
typedef void (__thiscall *received_fn)(void *client, DWORD broadcast, BYTE *data, DWORD length, DWORD type);
typedef void (__thiscall *push_fn)(void *client);
void h2n_xnetx_service(void)
{
	void *client = g_client;
	static DWORD last_report;
	static BYTE frame[FRAME_MAX];
	int sent = 0;
	if (!client) return;
	for (;;)
	{
		void *packet = NULL;
		EnterCriticalSection(&g_q);
		if (g_tx_count) { packet = g_tx[g_tx_head]; g_tx_head = (g_tx_head + 1) % TX_SLOTS; g_tx_count--; }
		LeaveCriticalSection(&g_q);
		if (!packet) break;
		((sent_fn)H2N_VA(0x3d6660, 0x3e0740))(client, packet);
		sent++;
	}
	if (sent) ((push_fn)H2N_VA(0x3d66b3, 0x3e0793))(client);
	for (;;)
	{
		unsigned length = 0;
		EnterCriticalSection(&g_q);
		if (g_rx_count) { length = g_rx[g_rx_head].length; memcpy(frame, g_rx[g_rx_head].data, length); g_rx_head = (g_rx_head + 1) % RX_SLOTS; g_rx_count--; }
		LeaveCriticalSection(&g_q);
		if (!length) break;
		((received_fn)H2N_VA(0x3d6671, 0x3e0751))(client, !memcmp(frame, "\xff\xff\xff\xff\xff\xff", 6), frame + 14, length - 14, *(WORD *)(frame + 12));
	}
	if (GetTickCount() - last_report >= 30000)
	{
		static LONG reported_sent = -1, reported_received = -1;
		last_report = GetTickCount();
		if (reported_sent == g_sent && reported_received == g_received) return;     /* (s10x) nothing since the last report: no line */
		reported_sent = g_sent; reported_received = g_received;
		h2n_log("[%4u] xnet %s: %ld frames out (%ld bytes, %ld failed), %ld in (%ld bytes), %ld not for us, %ld dropped; of those on the LAN: %ld out, %ld in\n", h2n_next_sequence(),
			g_lan ? "LAN" : "tunnel", g_sent, g_sent_bytes, g_send_failed, g_received, g_received_bytes, g_ignored, g_dropped, g_lan_sent, g_lan_received);
		if (g_lan)
		{
			long c[8];
			void h2n_lan_counts(long counts[8]);
			h2n_lan_counts(c);
			h2n_log("       xnet LAN: frames sent to everyone %ld, to one player %ld (%ld failed); received %ld (left out: %ld of this program's own, %ld that came twice); passed on for a tunnel %ld; players heard %ld\n",
				c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7]);
		}
	}
}

/* ---- set-up, before the game runs (main.c, after retail code is loaded) ---- */
static void patch_checked(DWORD at, const char *expect, unsigned n, const BYTE *with, unsigned m, const char *what)
{
	DWORD old;
	if (memcmp((void *)(ULONG_PTR)at, expect, n)) h2n_fatal("xnet: retail code at %08lx (%s) is not what v1.0 has there", at, what);
	VirtualProtect((void *)(ULONG_PTR)at, m, PAGE_EXECUTE_READWRITE, &old);
	memcpy((void *)(ULONG_PTR)at, with, m);
	VirtualProtect((void *)(ULONG_PTR)at, m, old, &old);
}
static void jump(DWORD at, const char *expect, unsigned n, void *to, const char *what)
{
	BYTE code[5] = { 0xe9 };
	*(DWORD *)(code + 1) = (DWORD)(ULONG_PTR)to - (at + 5);
	patch_checked(at, expect, n, code, 5, what);
}
static void make_mac(void)
{
	const char *text = getenv("H2N_XNET_MAC");
	unsigned v[6];
	if (text && sscanf(text, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6)
	{
		for (int i = 0; i < 6; i++) g_mac[i] = (BYTE)v[i];
		g_mac[0] &= 0xfe;                       /* (one machine's address, not a group's) */
		return;
	}
	{	/* Microsoft's prefix (the console's) and three bytes from this PC's name, Windows' own id of this installation and
		   this program's port (the tunnel's, or the one taken on the network: two programs on one PC differ by it) */
		char name[MAX_COMPUTERNAME_LENGTH + 1] = "", id[64] = "";
		DWORD size = sizeof(name), hash = 2166136261u, id_size = sizeof(id) - 1;
		HKEY key;
		GetComputerNameA(name, &size);
		if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ | KEY_WOW64_64KEY, &key) == ERROR_SUCCESS)
		{
			if (RegQueryValueExA(key, "MachineGuid", NULL, NULL, (BYTE *)id, &id_size) != ERROR_SUCCESS) id[0] = 0;
			RegCloseKey(key);
		}
		for (const char *p = name; *p; p++) hash = (hash ^ (BYTE)*p) * 16777619u;
		for (const char *p = id; *p; p++) hash = (hash ^ (BYTE)*p) * 16777619u;
		hash = (hash ^ (DWORD)(g_lan_port ? (int)g_lan_port : h2n_env_int("H2N_XNET_PORT", 9368))) * 16777619u;
		g_mac[0] = 0x00; g_mac[1] = 0x50; g_mac[2] = 0xf2;
		g_mac[3] = (BYTE)(hash >> 16); g_mac[4] = (BYTE)(hash >> 8); g_mac[5] = (BYTE)hash;
	}
}
static void open_tunnel(void)
{
	HMODULE ws = LoadLibraryA("ws2_32.dll");
	socket_fn w_socket = (socket_fn)GetProcAddress(ws, "socket");
	bind_fn w_bind = (bind_fn)GetProcAddress(ws, "bind");
	startup_fn w_startup = (startup_fn)GetProcAddress(ws, "WSAStartup");
	gethost_fn w_gethost = (gethost_fn)GetProcAddress(ws, "gethostbyname");
	const char *peer = getenv("H2N_XNET_PEER");
	int port = h2n_env_int("H2N_XNET_PORT", 9368), peer_port = 9368, size = 1 << 20;
	char host[256] = "";
	struct sockaddr_in local;
	WSADATA data;
	w_sendto = (sendto_fn)GetProcAddress(ws, "sendto");
	w_recvfrom = (recvfrom_fn)GetProcAddress(ws, "recvfrom");
	w_lasterror = (lasterror_fn)GetProcAddress(ws, "WSAGetLastError");
	if (!peer || !*peer) { if (!g_lan) h2n_log("*** xnet: H2N_XNET_PEER is not set (where the tunnel sends the frames: host:port)\n"); return; }
	{
		const char *colon = strrchr(peer, ':');
		size_t n = colon ? (size_t)(colon - peer) : strlen(peer);
		if (n >= sizeof(host)) n = sizeof(host) - 1;
		memcpy(host, peer, n); host[n] = 0;
		if (colon) peer_port = atoi(colon + 1);
	}
	w_startup(MAKEWORD(2, 2), &data);
	memset(&g_peer, 0, sizeof(g_peer));
	g_peer.sin_family = AF_INET;
	g_peer.sin_port = (u_short)((peer_port & 0xff) << 8 | (peer_port >> 8 & 0xff));
	{
		unsigned a, b, c, d;
		if (sscanf(host, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) g_peer.sin_addr.s_addr = (DWORD)(a | b << 8 | c << 16 | d << 24);
		else
		{
			struct hostent *h = w_gethost(host);
			if (!h || h->h_length != 4) { h2n_log("*** xnet: the peer %s could not be found; the network cable reads as unplugged\n", host); return; }
			memcpy(&g_peer.sin_addr, h->h_addr_list[0], 4);
		}
	}
	g_tunnel = w_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	memset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_port = (u_short)((port & 0xff) << 8 | (port >> 8 & 0xff));
	if (g_tunnel == INVALID_SOCKET || w_bind(g_tunnel, (struct sockaddr *)&local, sizeof(local)) == SOCKET_ERROR)
	{
		h2n_log("*** xnet: the tunnel's UDP port %d could not be opened (Winsock error %d; is something else using it?)%s\n",
			port, w_lasterror(), g_lan ? "; system link is on the PC's network only" : "");
		g_tunnel = INVALID_SOCKET;
		return;
	}
	{
		typedef int (WSAAPI *setopt_fn)(SOCKET, int, int, const char *, int);
		setopt_fn w_setopt = (setopt_fn)GetProcAddress(ws, "setsockopt");
		w_setopt(g_tunnel, SOL_SOCKET, SO_RCVBUF, (const char *)&size, sizeof(size));
		w_setopt(g_tunnel, SOL_SOCKET, SO_SNDBUF, (const char *)&size, sizeof(size));
	}
	{
		HANDLE (WINAPI *create)(void *, SIZE_T, LPTHREAD_START_ROUTINE, void *, DWORD, DWORD *) =
			(void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "CreateThread");
		DWORD id;
		HANDLE t = create(NULL, 0x10000, receive_thread, NULL, 0, &id);
		SetThreadPriority(t, THREAD_PRIORITY_ABOVE_NORMAL);
	}
	g_link = 1;
	h2n_log("xnet: the tunnel sends to %u.%u.%u.%u:%d and listens on UDP port %d\n", g_peer.sin_addr.s_addr & 0xff, g_peer.sin_addr.s_addr >> 8 & 0xff,
		g_peer.sin_addr.s_addr >> 16 & 0xff, g_peer.sin_addr.s_addr >> 24, peer_port, port);
}

/* ---- H2N_XNET_TEST=1 (a test of the library, beside the game): once the game has started the network library, says when
 * it has an address, then broadcasts a numbered line to UDP port 1009 every 2 s and says what arrives there (another copy
 * with the same test shows the two libraries reach each other through the tunnel, encrypted as system link is) ---- */
typedef SOCKET (WINAPI *t_socket)(int, int, int);
typedef int (WINAPI *t_bind)(SOCKET, const struct sockaddr *, int);
typedef int (WINAPI *t_sendto)(SOCKET, const char *, int, int, const struct sockaddr *, int);
typedef int (WINAPI *t_recvfrom)(SOCKET, char *, int, int, struct sockaddr *, int *);
typedef int (WINAPI *t_setsockopt)(SOCKET, int, int, const char *, int);
typedef int (WINAPI *t_ioctlsocket)(SOCKET, long, u_long *);
typedef DWORD (WINAPI *t_xnaddr)(BYTE *);
typedef int (WINAPI *t_lasterror)(void);
static DWORD WINAPI test_thread(void *unused)
{
	BYTE xnaddr[36];
	DWORD status = 0, last = 0xffffffff, start = GetTickCount();
	SOCKET s;
	struct sockaddr_in here, all;
	int yes = 1, n = 0;
	u_long nonblocking = 1;
	(void)unused;
	while (!g_client) Sleep(100);
	for (;;)
	{
		memset(xnaddr, 0, sizeof(xnaddr));
		status = ((t_xnaddr)h2n_net_ptr[19])(xnaddr);
		if (status != last)
			h2n_log("[%4u] xnet test: XNetGetTitleXnAddr = %04lx after %lu ms: %u.%u.%u.%u, %02x:%02x:%02x:%02x:%02x:%02x\n", h2n_next_sequence(), status,
				GetTickCount() - start, xnaddr[0], xnaddr[1], xnaddr[2], xnaddr[3], xnaddr[10], xnaddr[11], xnaddr[12], xnaddr[13], xnaddr[14], xnaddr[15]);
		last = status;
		if (status != 0) break;
		Sleep(250);
	}
	s = ((t_socket)h2n_net_ptr[0])(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	memset(&here, 0, sizeof(here)); here.sin_family = AF_INET; here.sin_port = 0xf103;          /* 1009 */
	all = here; all.sin_addr.s_addr = 0xffffffff;
	h2n_log("[%4u] xnet test: socket %lx, bind %d, broadcast %d, non-blocking %d\n", h2n_next_sequence(), (unsigned long)s,
		((t_bind)h2n_net_ptr[1])(s, (struct sockaddr *)&here, sizeof(here)),
		((t_setsockopt)h2n_net_ptr[8])(s, SOL_SOCKET, SO_BROADCAST, (const char *)&yes, sizeof(yes)),
		((t_ioctlsocket)h2n_net_ptr[7])(s, FIONBIO, &nonblocking));
	for (DWORD next = 0;; Sleep(20))
	{
		char line[128];
		struct sockaddr_in from;
		int size = sizeof(from), got;
		if (GetTickCount() >= next)
		{
			int length = _snprintf(line, sizeof(line), "H2Classic xnet test %d from %02x:%02x:%02x", ++n, g_mac[3], g_mac[4], g_mac[5]);
			int sent = ((t_sendto)h2n_net_ptr[2])(s, line, length + 1, 0, (struct sockaddr *)&all, sizeof(all));
			if (n <= 3 || sent < 0) h2n_log("[%4u] xnet test: sent \"%s\": %d (%d)\n", h2n_next_sequence(), line, sent, sent < 0 ? ((t_lasterror)h2n_net_ptr[16])() : 0);
			next = GetTickCount() + 2000;
		}
		got = ((t_recvfrom)h2n_net_ptr[3])(s, line, sizeof(line) - 1, 0, (struct sockaddr *)&from, &size);
		if (got > 0)
		{
			line[got] = 0;
			h2n_log("[%4u] xnet test: received from %u.%u.%u.%u:%u: \"%s\"\n", h2n_next_sequence(), from.sin_addr.s_addr & 0xff, from.sin_addr.s_addr >> 8 & 0xff,
				from.sin_addr.s_addr >> 16 & 0xff, from.sin_addr.s_addr >> 24, (from.sin_port & 0xff) << 8 | from.sin_port >> 8, line);
		}
	}
}

/* ---- what the game itself sends and receives through the library (retail mode; logged, nothing changed): the first few
 * packets of each kind (address, port and size), whole, as the game's code sees them - after the library has decrypted
 * them. This tells a packet the library drops from one the game's own code turns away. ---- */
typedef int (WINAPI *game_sendto_fn)(SOCKET, const char *, int, int, const struct sockaddr *, int);
typedef int (WINAPI *game_recvfrom_fn)(SOCKET, char *, int, int, struct sockaddr *, int *);
static game_sendto_fn g_retail_sendto;
static game_recvfrom_fn g_retail_recvfrom;
static CRITICAL_SECTION g_seen_lock;
static struct { DWORD address; WORD port, length; BYTE way, count; } g_seen[64];
static void game_packet(const char *way, const struct sockaddr_in *peer, const BYTE *data, int length)
{
	unsigned i, n = 0;
	char line[1200];
	EnterCriticalSection(&g_seen_lock);
	for (i = 0; i < 64 && g_seen[i].count; i++)
		if (g_seen[i].way == (BYTE)way[0] && g_seen[i].address == peer->sin_addr.s_addr && g_seen[i].port == peer->sin_port && g_seen[i].length == (WORD)length) break;
	if (i == 64 || g_seen[i].count >= 3) { LeaveCriticalSection(&g_seen_lock); return; }
	g_seen[i].way = (BYTE)way[0]; g_seen[i].address = peer->sin_addr.s_addr; g_seen[i].port = peer->sin_port; g_seen[i].length = (WORD)length;
	g_seen[i].count++;
	LeaveCriticalSection(&g_seen_lock);
	for (int k = 0; k < length && k < 512 && n + 3 < sizeof(line); k++) n += (unsigned)_snprintf(line + n, sizeof(line) - n, "%02x", data[k]);
	line[n] = 0;
	h2n_log("[%4u] game network: %s %u.%u.%u.%u:%u, %d bytes: %s\n", h2n_next_sequence(), way, peer->sin_addr.s_addr & 0xff, peer->sin_addr.s_addr >> 8 & 0xff,
		peer->sin_addr.s_addr >> 16 & 0xff, peer->sin_addr.s_addr >> 24, (peer->sin_port & 0xff) << 8 | peer->sin_port >> 8, length, line);
}
static int WINAPI logged_sendto(SOCKET s, const char *data, int length, int flags, const struct sockaddr *to, int to_size)
{
	int r = g_retail_sendto(s, data, length, flags, to, to_size);
	DWORD error = GetLastError();
	if (to && to_size >= (int)sizeof(struct sockaddr_in) && length > 0) game_packet("sent to", (const struct sockaddr_in *)to, (const BYTE *)data, length);
	SetLastError(error);
	return r;
}
static int WINAPI logged_recvfrom(SOCKET s, char *data, int length, int flags, struct sockaddr *from, int *from_size)
{
	int r = g_retail_recvfrom(s, data, length, flags, from, from_size);
	DWORD error = GetLastError();
	if (r > 0 && from && from_size && *from_size >= (int)sizeof(struct sockaddr_in)) game_packet("received from", (const struct sockaddr_in *)from, (const BYTE *)data, r);
	SetLastError(error);
	return r;
}

void h2n_net_setup(const BYTE *xbe, size_t size)
{
	HMODULE ws = LoadLibraryA("ws2_32.dll");
	int retail = h2n_xnet_retail();
	if (retail && !h2n_retail_code_loaded())
	{
		h2n_log("*** H2N_XNET=retail needs retail code (H2N_RETAIL=off was given): the PC network is used\n");
		retail = 0;
	}
	for (unsigned i = 0; i < NET_COUNT; i++)
	{
		void *to = retail ? (void *)(ULONG_PTR)g_net[i].retail[h2n_v15] : g_net[i].winsock ? (void *)GetProcAddress(ws, g_net[i].winsock) : g_net[i].pc;
		if (!to) h2n_fatal("the network call %s has nothing to go to", g_net[i].name);
		h2n_net_ptr[i] = to;
	}
	if (!retail) return;
	InitializeCriticalSection(&g_q);
	InitializeCriticalSection(&g_seen_lock);
	g_retail_sendto = (game_sendto_fn)h2n_net_ptr[2]; h2n_net_ptr[2] = (void *)logged_sendto;
	g_retail_recvfrom = (game_recvfrom_fn)h2n_net_ptr[3]; h2n_net_ptr[3] = (void *)logged_recvfrom;
	/* the library reads the certificate's title id, version and LAN key through the XBE header's certificate address
	   (0x10118): main.c's copy of the XBE's headers answers those reads, as it does every other read of the headers */
	{	/* (s10x) the port on the network first: the console address is made with it */
		const char *link = getenv("H2N_XNET_LINK");
		if (!(link && !_stricmp(link, "tunnel"))) g_lan_port = h2n_lan_bind();
	}
	make_mac();
	/* the processor block the library reads with fs:[0x20] (4 places): ours, which holds the interface at +0x24c */
	*(dm_interface **)(g_prcb + 0x24c) = &g_dm;
	{
		const DWORD at[4] = { H2N_VA(0x36c491, 0x376561), H2N_VA(0x3b053e, 0x3ba61e), H2N_VA(0x3d674d, 0x3e082d), H2N_VA(0x3dcd69, 0x3e6e49) };
		static const char *const what[4] = { "XNetGetEthernetLinkStatus", "XoUpdateLaunchNewImageInternal", "NicInit", "CXnEnet::EnetRecv" };
		BYTE code[6] = { 0xb8, 0, 0, 0, 0, 0x90 };              /* mov eax, g_prcb; nop (for mov eax, fs:[0x20]) */
		*(DWORD *)(code + 1) = (DWORD)(ULONG_PTR)g_prcb;
		for (int i = 0; i < 4; i++) patch_checked(at[i], "\x64\xa1\x20\x00\x00\x00", 6, code, 6, what[i]);
	}
	/* XAPI's LocalAlloc and LocalFree (the library calls each once): on retail's heap, which is never set up here; Windows'
	   are the same calls (LMEM_ZEROINIT is retail's HEAP_ZERO_MEMORY) */
	{
		HMODULE k = GetModuleHandleA("kernel32.dll");
		jump(H2N_VA(0x2d152f, 0x2db41f), "\x8b\x44\x24\x04\xff\x74\x24\x08", 8, (void *)GetProcAddress(k, "LocalAlloc"), "LocalAlloc");
		jump(H2N_VA(0x2d154c, 0x2db43c), "\xff\x74\x24\x04\x6a\x00", 6, (void *)GetProcAddress(k, "LocalFree"), "LocalFree");
	}
	{
		/* (s10x) where the frames go. H2N_XNET_LINK (network_link in settings.ini):
		     lan (the default)  the players' programs find each other on the PC's network and carry each other's frames
		                        (xlan.c): nothing to set. An xemu whose "UDP Tunnel" points at this PC (port 9369) joins;
		     tunnel             a UDP tunnel to one peer (H2N_XNET_PEER, H2N_XNET_PORT), as before s10x. */
		const char *link = getenv("H2N_XNET_LINK");
		if (link && !_stricmp(link, "tunnel")) open_tunnel();
		else
		{
			g_lan = g_lan_port && h2n_lan_open(g_mac, lan_frame);
			if (g_lan) g_link = 1;
			else h2n_log("*** xnet: the PC's network could not be used for system link (see above); the network cable reads as unplugged\n");
		}
	}
	if (h2n_env_int("H2N_XNET_TEST", 0))
	{
		HANDLE (WINAPI *create)(void *, SIZE_T, LPTHREAD_START_ROUTINE, void *, DWORD, DWORD *) =
			(void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "CreateThread");
		DWORD id;
		create(NULL, 0x10000, test_thread, NULL, 0, &id);
	}
	h2n_log("H2N_XNET=retail: the game's network calls are the XBE's own network library (system link with consoles and xemu)%s\n",
		h2n_have_certificate_key() ? "" : "\n*** xnet: no certificate key (h2classic\\keys.bin): the system link key is not the console's, so only other H2Classic"
		" copies without a key will see this one");
}
