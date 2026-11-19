/* xlan.c - system link over the PC's network with nothing to set up (s10x).
 *
 * With H2N_XNET=retail the XBE's network library hands this program whole Ethernet frames (xnetx.c) and takes whole
 * frames back, as from a cable. System link is addressed by Ethernet address alone (every packet says IP 0.0.0.1, with
 * or without a router: seen here), which Windows' own networking cannot carry - that takes a packet driver, and none
 * is used. Instead the players' programs carry each other's frames: each frame travels as one UDP datagram - the
 * format of xemu's "UDP Tunnel" - and the programs find each other by themselves:
 *
 *   - A program listens on the first free of four UDP ports (9369, 9370, 9371, 9368). 9368 last: it is the port xemu's
 *     tunnel listens on, so both can run on one PC.
 *   - A frame for everyone (a broadcast: "who has games?", "here is mine") is sent to everyone: to the broadcast
 *     address of each network the PC is on and to this PC itself, on each of the four ports.
 *   - From every frame that arrives, the sender's Ethernet address and where it came from are remembered. A frame for
 *     one console goes straight to where that console's program was last heard; to everyone if it is not known (as a
 *     network switch does).
 *   - What arrives is handed to the library as it is; the library takes what is for its address or for everyone.
 *     A program's own broadcast, back from the network, is left out, and so is a second copy of a broadcast that came
 *     by two ways (two networks, or the network and this PC).
 *   - Every three seconds each program says it is there (a frame of a type the library and consoles ignore, 0x88b5).
 *     A sender that never says so is a plain tunnel - xemu's "UDP Tunnel", which sends everything to the one address
 *     it was given. For such a sender this program is the way to the others: its broadcasts are passed on to every
 *     program heard, and frames between it and another program's console are passed on both ways. So an xemu whose
 *     tunnel points at any one H2Classic PC plays with all of them.
 *
 * Nobody is told an address. Windows Firewall has to let H2Classic.exe receive (it asks the first time); programs on
 * one PC reach each other without it.
 */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void h2n_log(const char *format, ...);
unsigned h2n_next_sequence(void);
int h2n_env_int(const char *name, int otherwise);

/* Winsock by name: the socket names this program links to are the game's (xnetx.c) */
typedef SOCKET (WSAAPI *socket_fn)(int, int, int);
typedef int (WSAAPI *bind_fn)(SOCKET, const struct sockaddr *, int);
typedef int (WSAAPI *sendto_fn)(SOCKET, const char *, int, int, const struct sockaddr *, int);
typedef int (WSAAPI *recvfrom_fn)(SOCKET, char *, int, int, struct sockaddr *, int *);
typedef int (WSAAPI *setopt_fn)(SOCKET, int, int, const char *, int);
typedef int (WSAAPI *close_fn)(SOCKET);
typedef int (WSAAPI *startup_fn)(WORD, WSADATA *);
typedef int (WSAAPI *lasterror_fn)(void);
typedef int (WSAAPI *ioctl_fn)(SOCKET, DWORD, void *, DWORD, void *, DWORD, DWORD *, void *, void *);
static socket_fn w_socket; static bind_fn w_bind; static sendto_fn w_sendto; static recvfrom_fn w_recvfrom; static setopt_fn w_setopt;
static close_fn w_close; static lasterror_fn w_lasterror;
static HANDLE (WINAPI *k_create_thread)(void *, SIZE_T, LPTHREAD_START_ROUTINE, void *, DWORD, DWORD *);
static DWORD (WINAPI *ip_adapters)(IP_ADAPTER_INFO *, ULONG *);

#define LOOPBACK 0x0100007fu                              /* 127.0.0.1 as it is in a packet */
#define HELLO_TYPE_0 0x88
#define HELLO_TYPE_1 0xb5                                 /* (the Ethernet type for local experiments; the library takes 0x0800, 0x0806 and PPPoE's and drops the rest: retail 0x3dcd64) */
static const char k_hello_mark[8] = { 'H', '2', 'C', 'L', 'I', 'N', 'K', '1' };
static const unsigned k_ports[4] = { 9369, 9370, 9371, 9368 };
static SOCKET g_socket = INVALID_SOCKET;
static unsigned g_port;
static BYTE g_mac[6];
static DWORD g_nonce, g_opened;
static int g_running, g_local_test;
static void (*g_frame_in)(const BYTE *frame, int length);
static volatile LONG g_out_all, g_out_one, g_out_failed, g_in, g_in_own, g_in_twice, g_passed_on, g_found_logged, g_clash_logged;
static CRITICAL_SECTION g_lock;

static WORD port_net(unsigned port) { return (WORD)((port & 0xff) << 8 | (port >> 8 & 0xff)); }
static unsigned port_host(WORD port) { return (unsigned)((port & 0xff) << 8 | port >> 8); }
static void to_address(struct sockaddr_in *to, DWORD ip, WORD port) { memset(to, 0, sizeof(*to)); to->sin_family = AF_INET; to->sin_addr.s_addr = ip; to->sin_port = port; }
static int send_to(const BYTE *frame, unsigned length, DWORD ip, WORD port)
{
	struct sockaddr_in to;
	to_address(&to, ip, port);
	return w_sendto(g_socket, (const char *)frame, (int)length, 0, (struct sockaddr *)&to, sizeof(to)) != SOCKET_ERROR;
}

/* ---- the networks this PC is on: where "everyone" is ---- */
#define NETS 8
static DWORD g_nets[NETS];                                /* each network's broadcast address */
static unsigned g_net_count;
static void networks_read(void)
{
	static BYTE space[16 * 1024];
	IP_ADAPTER_INFO *a = (IP_ADAPTER_INFO *)space;
	ULONG size = sizeof(space);
	DWORD nets[NETS];
	unsigned n = 0, i;
	if (g_local_test || !ip_adapters || ip_adapters(a, &size) != NO_ERROR) a = NULL;
	for (; a; a = a->Next)
		for (IP_ADDR_STRING *s = &a->IpAddressList; s; s = s->Next)
		{
			unsigned p[4], m[4];
			DWORD ip, mask, all;
			if (sscanf(s->IpAddress.String, "%u.%u.%u.%u", &p[0], &p[1], &p[2], &p[3]) != 4 || sscanf(s->IpMask.String, "%u.%u.%u.%u", &m[0], &m[1], &m[2], &m[3]) != 4) continue;
			ip = p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24; mask = m[0] | m[1] << 8 | m[2] << 16 | m[3] << 24;
			if (!ip || p[0] == 127 || !mask || mask == 0xffffffffu) continue;       /* (no address yet; this PC itself; a line to one machine) */
			all = ip | ~mask;
			for (i = 0; i < n && nets[i] != all; i++) { }
			if (i == n && n < NETS) nets[n++] = all;
		}
	EnterCriticalSection(&g_lock);
	if (n != g_net_count || memcmp(nets, g_nets, n * sizeof(DWORD)))
	{
		char text[NETS * 18 + 4] = "";
		for (i = 0; i < n; i++) sprintf(text + strlen(text), "%s%lu.%lu.%lu.%lu", i ? ", " : "", nets[i] & 0xff, nets[i] >> 8 & 0xff, nets[i] >> 16 & 0xff, nets[i] >> 24);
		h2n_log("[%4u] xnet LAN: %s%s\n", h2n_next_sequence(), n ? "everyone on this PC's network is reached at " : "this PC is on no network now: only programs on this PC are reached", text);
		memcpy(g_nets, nets, sizeof(nets)); g_net_count = n;
	}
	LeaveCriticalSection(&g_lock);
}
/* to everyone: each network's broadcast address and this PC, on each port (not to this program itself) */
static int send_all(const BYTE *frame, unsigned length)
{
	DWORD nets[NETS];
	unsigned n, i, p;
	int sent = 0;
	EnterCriticalSection(&g_lock);
	n = g_net_count; memcpy(nets, g_nets, sizeof(nets));
	LeaveCriticalSection(&g_lock);
	for (p = 0; p < 4; p++)
	{
		for (i = 0; i < n; i++) sent += send_to(frame, length, nets[i], port_net(k_ports[p]));
		if (k_ports[p] != g_port) sent += send_to(frame, length, LOOPBACK, port_net(k_ports[p]));
	}
	return sent;
}

/* ---- who was heard ---- */
/* the programs: where a datagram came from. told: it says it is one of these programs (a tunnel never does) */
#define ENDS 32
static struct { DWORD ip; WORD port; BYTE told; DWORD first, when; } g_ends[ENDS];
/* the consoles: Ethernet address -> the program it was heard through */
#define PEERS 48
static struct { BYTE mac[6]; DWORD ip; WORD port; DWORD when; } g_peers[PEERS];

static DWORD now_ticks(void) { DWORD t = GetTickCount(); return t ? t : 1; }
static int end_find(DWORD ip, WORD port)
{
	for (int i = 0; i < ENDS; i++) if (g_ends[i].when && g_ends[i].ip == ip && g_ends[i].port == port) return i;
	return -1;
}
/* (lock held) notes the program; returns its place */
static int end_heard(DWORD ip, WORD port, int told)
{
	DWORD now = now_ticks();
	int i = end_find(ip, port);
	if (i < 0)
	{
		int oldest = 0;
		for (i = 0; i < ENDS && g_ends[i].when; i++) if (now - g_ends[i].when > now - g_ends[oldest].when) oldest = i;
		if (i == ENDS) i = oldest;
		g_ends[i].ip = ip; g_ends[i].port = port; g_ends[i].told = 0; g_ends[i].first = now;
	}
	g_ends[i].when = now;
	if (told) g_ends[i].told = 1;
	return i;
}
/* (lock held) a plain tunnel: heard for a while (a program says what it is at once, and again every three seconds), never told */
static int end_is_tunnel(int i)
{
	DWORD now = now_ticks();
	return i >= 0 && !g_ends[i].told && now - g_ends[i].first >= 5000 && now - g_opened >= 6000;
}
static void peer_heard(const BYTE *mac, DWORD ip, WORD port)
{
	unsigned i, slot = PEERS, oldest = 0;
	DWORD now = now_ticks();
	if (mac[0] & 1) return;                                   /* (a group address is nobody's) */
	for (i = 0; i < PEERS; i++)
	{
		if (g_peers[i].when && !memcmp(g_peers[i].mac, mac, 6)) { slot = i; break; }
		if (!g_peers[i].when) { if (slot == PEERS) slot = i; }
		else if (!g_peers[oldest].when || now - g_peers[i].when > now - g_peers[oldest].when) oldest = i;
	}
	if (slot == PEERS) slot = oldest;
	if (!g_peers[slot].when || memcmp(g_peers[slot].mac, mac, 6))
	{	/* a console not heard before */
		if (InterlockedIncrement(&g_found_logged) <= 40)
			h2n_log("[%4u] xnet LAN: a player was found: console %02x:%02x:%02x:%02x:%02x:%02x, through the program at %lu.%lu.%lu.%lu port %u\n", h2n_next_sequence(),
				mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], ip & 0xff, ip >> 8 & 0xff, ip >> 16 & 0xff, ip >> 24, port_host(port));
	}
	memcpy(g_peers[slot].mac, mac, 6); g_peers[slot].ip = ip; g_peers[slot].port = port; g_peers[slot].when = now;
}
static int peer_where(const BYTE *mac, DWORD *ip, WORD *port)
{
	for (unsigned i = 0; i < PEERS; i++)
		if (g_peers[i].when && !memcmp(g_peers[i].mac, mac, 6)) { *ip = g_peers[i].ip; *port = g_peers[i].port; return 1; }
	return 0;
}

/* ---- "I am here" ---- */
static unsigned hello_make(BYTE frame[60])
{
	memset(frame, 0, 60);
	memset(frame, 0xff, 6); memcpy(frame + 6, g_mac, 6);
	frame[12] = HELLO_TYPE_0; frame[13] = HELLO_TYPE_1;
	memcpy(frame + 14, k_hello_mark, 8); memcpy(frame + 22, &g_nonce, 4);
	return 60;
}
static int hello_is(const BYTE *frame, int n) { return n >= 26 && frame[12] == HELLO_TYPE_0 && frame[13] == HELLO_TYPE_1 && !memcmp(frame + 14, k_hello_mark, 8); }
static DWORD WINAPI hello_thread(void *unused)
{
	BYTE frame[60];
	unsigned round = 0, length = hello_make(frame);
	int said_quiet = 0;
	(void)unused;
	for (;; round++)
	{
		if (round % 4 == 0) networks_read();
		send_all(frame, length);
		if (!said_quiet && now_ticks() - g_opened > 45000)
		{	/* (once) nobody yet: say what to look at, should a player be expected */
			unsigned i, known = 0;
			said_quiet = 1;
			for (i = 0; i < ENDS; i++) if (g_ends[i].when) known++;
			if (!known) h2n_log("[%4u] xnet LAN: no other player's program has been heard on this network so far. If one is running on another PC: both PCs have to be on the same network,"
				" and Windows Firewall has to allow H2Classic.exe on each (Windows Security > Firewall & network protection > Allow an app through firewall)\n", h2n_next_sequence());
		}
		Sleep(3000);
	}
}

/* ---- frames in ---- */
/* a broadcast that came twice (by two networks, or by the network and this PC) is taken once */
static int seen_just_now(const BYTE *frame, int n)
{
	static struct { DWORD hash, when; int length; } recent[16];
	static unsigned next;
	DWORD hash = 2166136261u, now = GetTickCount();
	int i;
	for (i = 0; i < n; i++) hash = (hash ^ frame[i]) * 16777619u;
	for (i = 0; i < 16; i++) if (recent[i].length == n && recent[i].hash == hash && now - recent[i].when < 40) return 1;
	recent[next].hash = hash; recent[next].when = now; recent[next].length = n; next = (next + 1) % 16;
	return 0;
}
static DWORD WINAPI read_thread(void *unused)
{
	static BYTE data[2048];
	(void)unused;
	for (;;)
	{
		struct sockaddr_in from;
		struct { DWORD ip; WORD port; } pass[ENDS];
		int size = sizeof(from), n, place, passes = 0, group, i;
		DWORD ip; WORD port;
		memset(&from, 0, sizeof(from));
		n = w_recvfrom(g_socket, (char *)data, sizeof(data), 0, (struct sockaddr *)&from, &size);
		if (n == SOCKET_ERROR)
		{
			int e = w_lasterror();
			if (e == WSAECONNRESET || e == WSAEMSGSIZE || e == WSAENETRESET) continue;     /* (a "port unreachable" from a PC that was sent to; too large a packet) */
			h2n_log("[%4u] xnet LAN: receiving stopped (Winsock error %d)\n", h2n_next_sequence(), e);
			return 0;
		}
		if (n < 14) continue;
		ip = from.sin_addr.s_addr; port = from.sin_port;
		if (hello_is(data, n))
		{
			if (!memcmp(data + 6, g_mac, 6))
			{	/* this program's own, back - or another program's with the same console address */
				if (memcmp(data + 22, &g_nonce, 4) && InterlockedIncrement(&g_clash_logged) == 1)
					h2n_log("*** xnet LAN: the program at %lu.%lu.%lu.%lu port %u has this program's console address (%02x:%02x:%02x:%02x:%02x:%02x): the two cannot play together."
						" Give one of them another with network_mac in h2classic\\settings.ini\n", ip & 0xff, ip >> 8 & 0xff, ip >> 16 & 0xff, ip >> 24, port_host(port),
						g_mac[0], g_mac[1], g_mac[2], g_mac[3], g_mac[4], g_mac[5]);
				continue;
			}
			EnterCriticalSection(&g_lock);
			place = end_find(ip, port);
			end_heard(ip, port, 1);
			LeaveCriticalSection(&g_lock);
			if (place < 0)
			{	/* new here: it hears of this program at once (it may have just started) */
				BYTE hello[60];
				send_to(hello, hello_make(hello), ip, port);
				h2n_log("[%4u] xnet LAN: another H2Classic was found at %lu.%lu.%lu.%lu port %u\n", h2n_next_sequence(), ip & 0xff, ip >> 8 & 0xff, ip >> 16 & 0xff, ip >> 24, port_host(port));
			}
			continue;
		}
		if (!memcmp(data + 6, g_mac, 6)) { InterlockedIncrement(&g_in_own); continue; }      /* this program's own broadcast, back */
		group = data[0] & 1;
		if (group && seen_just_now(data, n)) { InterlockedIncrement(&g_in_twice); continue; }
		EnterCriticalSection(&g_lock);
		place = end_heard(ip, port, 0);
		peer_heard(data + 6, ip, port);
		if (group)
		{	/* a plain tunnel's broadcast reached this program alone: on to every other program heard */
			if (end_is_tunnel(place))
				for (i = 0; i < ENDS; i++)
					if (i != place && g_ends[i].when && now_ticks() - g_ends[i].when < 60000) { pass[passes].ip = g_ends[i].ip; pass[passes].port = g_ends[i].port; passes++; }
		}
		else if (memcmp(data, g_mac, 6))
		{	/* for another console: passed on when it is to or from a plain tunnel that reaches the others through this program */
			DWORD to_ip; WORD to_port;
			if (peer_where(data, &to_ip, &to_port) && (to_ip != ip || to_port != port) && (end_is_tunnel(place) || end_is_tunnel(end_find(to_ip, to_port))))
			{ pass[0].ip = to_ip; pass[0].port = to_port; passes = 1; }
		}
		LeaveCriticalSection(&g_lock);
		for (i = 0; i < passes; i++) if (send_to(data, (unsigned)n, pass[i].ip, pass[i].port)) InterlockedIncrement(&g_passed_on);
		InterlockedIncrement(&g_in);
		g_frame_in(data, n);
	}
}

/* ---- frames out: a frame from the library ---- */
int h2n_lan_send(const BYTE *frame, unsigned length)
{
	if (!g_running || length < 14) return 0;
	if (!(frame[0] & 1))
	{
		DWORD ip; WORD port;
		int known;
		EnterCriticalSection(&g_lock);
		known = peer_where(frame, &ip, &port);
		LeaveCriticalSection(&g_lock);
		if (known)
		{	/* for one console: to the program it was heard through */
			if (send_to(frame, length, ip, port)) { InterlockedIncrement(&g_out_one); return 1; }
			InterlockedIncrement(&g_out_failed);
			return 0;
		}
	}
	if (send_all(frame, length)) { InterlockedIncrement(&g_out_all); return 1; }
	InterlockedIncrement(&g_out_failed);
	return 0;
}
void h2n_lan_counts(long counts[8])
{
	unsigned i, known = 0;
	EnterCriticalSection(&g_lock);
	for (i = 0; i < PEERS; i++) if (g_peers[i].when && now_ticks() - g_peers[i].when < 120000) known++;
	LeaveCriticalSection(&g_lock);
	counts[0] = g_out_all; counts[1] = g_out_one; counts[2] = g_out_failed; counts[3] = g_in; counts[4] = g_in_own; counts[5] = g_in_twice; counts[6] = g_passed_on; counts[7] = (long)known;
}

/* Takes this program's port: the first free of the four. Returns it, 0 when there is none. Before the console address
 * is made (xnetx.c make_mac: two programs on one PC get different addresses by their ports). */
unsigned h2n_lan_bind(void)
{
	HMODULE ws = LoadLibraryA("ws2_32.dll"), ip = LoadLibraryA("iphlpapi.dll");
	startup_fn w_startup = (startup_fn)GetProcAddress(ws, "WSAStartup");
	WSADATA data;
	unsigned i;
	int on = 1, size = 1 << 20, error = 0;
	if (g_socket != INVALID_SOCKET) return g_port;
	k_create_thread = (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "CreateThread");
	ip_adapters = ip ? (void *)GetProcAddress(ip, "GetAdaptersInfo") : NULL;
	w_socket = (socket_fn)GetProcAddress(ws, "socket"); w_bind = (bind_fn)GetProcAddress(ws, "bind"); w_sendto = (sendto_fn)GetProcAddress(ws, "sendto");
	w_recvfrom = (recvfrom_fn)GetProcAddress(ws, "recvfrom"); w_setopt = (setopt_fn)GetProcAddress(ws, "setsockopt"); w_close = (close_fn)GetProcAddress(ws, "closesocket");
	w_lasterror = (lasterror_fn)GetProcAddress(ws, "WSAGetLastError");
	if (!w_startup || !w_socket || !w_bind || !w_sendto || !w_recvfrom || !w_setopt || !w_close || !w_lasterror || !k_create_thread) { h2n_log("*** xnet LAN: Winsock is not there\n"); return 0; }
	w_startup(MAKEWORD(2, 2), &data);
	InitializeCriticalSection(&g_lock);
	for (i = 0; i < 4 && g_socket == INVALID_SOCKET; i++)
	{
		struct sockaddr_in local;
		SOCKET s = w_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (s == INVALID_SOCKET) { error = w_lasterror(); break; }
		to_address(&local, 0, port_net(k_ports[i]));
		if (w_bind(s, (struct sockaddr *)&local, sizeof(local)) == SOCKET_ERROR) { error = w_lasterror(); w_close(s); continue; }
		g_socket = s; g_port = k_ports[i];
	}
	if (g_socket == INVALID_SOCKET)
	{
		h2n_log("*** xnet LAN: none of the UDP ports 9369, 9370, 9371, 9368 could be opened (Winsock error %d): other programs have them all\n", error);
		return 0;
	}
	w_setopt(g_socket, SOL_SOCKET, SO_BROADCAST, (const char *)&on, sizeof(on));
	w_setopt(g_socket, SOL_SOCKET, SO_RCVBUF, (const char *)&size, sizeof(size));
	w_setopt(g_socket, SOL_SOCKET, SO_SNDBUF, (const char *)&size, sizeof(size));
	{	/* a "port unreachable" answer to something sent must not fail the next receive (Windows' SIO_UDP_CONNRESET) */
		ioctl_fn w_ioctl = (ioctl_fn)GetProcAddress(ws, "WSAIoctl");
		DWORD off = 0, got = 0;
		if (w_ioctl) w_ioctl(g_socket, 0x9800000Cu, &off, sizeof(off), NULL, 0, &got, NULL, NULL);
	}
	return g_port;
}
/* Opens the link on the port taken. mac: the library's Ethernet address. frame_in is called (from the receiving
 * thread) with each frame that arrives from another program. Returns 1 when open. */
int h2n_lan_open(const BYTE mac[6], void (*frame_in)(const BYTE *, int))
{
	DWORD id;
	HANDLE t;
	LARGE_INTEGER counter;
	if (g_socket == INVALID_SOCKET) return 0;
	memcpy(g_mac, mac, 6);
	g_frame_in = frame_in;
	QueryPerformanceCounter(&counter);
	g_nonce = (DWORD)counter.LowPart * 2654435761u ^ GetCurrentProcessId() << 16 ^ GetTickCount();
	g_opened = now_ticks();
	g_local_test = h2n_env_int("H2N_XNET_LOCAL", 0);          /* (tests) this PC only: nothing is sent to the network */
	networks_read();
	g_running = 1;
	t = k_create_thread(NULL, 0x10000, read_thread, NULL, 0, &id);
	SetThreadPriority(t, THREAD_PRIORITY_ABOVE_NORMAL);
	k_create_thread(NULL, 0x10000, hello_thread, NULL, 0, &id);
	h2n_log("xnet LAN: system link finds the other players on this network by itself (UDP port %u here; console address %02x:%02x:%02x:%02x:%02x:%02x)."
		" Windows Firewall has to let H2Classic.exe receive on this network (it asks the first time).\n", g_port, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return 1;
}
