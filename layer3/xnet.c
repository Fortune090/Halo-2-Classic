/* xnet.c - the console's network library (XNet) on the PC's own network (s9t).
 *
 * The game's network code (transport.cpp, unknown_092870.cpp, unknown_07a8a0.cpp, unknown_07a9a0.cpp) already talks to
 * Windows' own sockets; what it also needs from the console's XNet library is answered here, from the adapter Windows
 * itself uses for its default route:
 *   - XNetGetEthernetLinkStatus: whether that adapter is up (the game starts its network only when it is);
 *   - XNetGetTitleXnAddr: the console's address - that adapter's IPv4 address and MAC;
 *   - the session keys a system link game is made with (XNetCreateKey / XNetRegisterKey / XNetUnregisterKey);
 *   - the console's "secure" addresses: on the console a peer's XNADDR is turned into an IN_ADDR that stands for it
 *     (XNetXnAddrToInAddr) and back (XNetInAddrToXnAddr). Here that address is the peer's own IPv4 address, and the
 *     table below remembers which XNADDR and key go with it;
 *   - VDP sockets (IPPROTO_VDP, the console's game-and-voice datagrams: ports 1000, 1005 and 1006 of a system link
 *     game) are UDP sockets here; the packets keep the console's layout (a 2-byte game data length first), so two PCs
 *     running this exe read each other's packets as two consoles do. (Not the console's wire format: a PC does not
 *     play with a console.)
 * Xbox Live stays off (XOnline and QoS are still the stand-ins).
 */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void h2n_log(const char *format, ...);
unsigned h2n_next_sequence(void);
int __stdcall h2n_XNetRandom(BYTE *buffer, UINT count) __asm__("_h2n_pc_XNetRandom@8");     /* xapi.c */

typedef struct { BYTE ab[8]; } xn_kid;
typedef struct { BYTE ab[16]; } xn_key;
typedef struct { DWORD ina, ina_online; WORD port_online; BYTE enet[6]; BYTE online[20]; } xn_addr;     /* XNADDR, 36 bytes */

#define XNADDR_NONE 0x0001
#define XNADDR_STATIC 0x0004
#define XNADDR_DHCP 0x0008
#define XNADDR_GATEWAY 0x0020
#define XNADDR_DNS 0x0040
#define LINK_ACTIVE 0x01
#define LINK_100MBPS 0x02
#define LINK_10MBPS 0x04
#define LINK_FULL_DUPLEX 0x08
#define VDP 254

static CRITICAL_SECTION g_lock;
static LONG g_lock_made;
static void lock(void)
{
	if (InterlockedCompareExchange(&g_lock_made, 1, 0) == 0) { InitializeCriticalSection(&g_lock); InterlockedExchange(&g_lock_made, 2); }
	while (g_lock_made != 2) Sleep(0);
	EnterCriticalSection(&g_lock);
}
static void unlock(void) { LeaveCriticalSection(&g_lock); }

/* ---- the PC's default adapter ---- */
static struct
{
	int valid, up, dhcp, gateway, dns;
	DWORD ip, index, checked;
	BYTE mac[6];
	unsigned long long speed;
	char name[96];
} g_net, g_told;
static int g_told_once;

typedef ULONG (WINAPI *get_adapters_fn)(ULONG, ULONG, void *, IP_ADAPTER_ADDRESSES *, ULONG *);
typedef DWORD (WINAPI *best_interface_fn)(IPAddr, DWORD *);

static int network_off(void)          /* H2N_NETWORK=off: no network, as before s9t */
{
	static int off = -1;
	if (off < 0)
	{
		const char *v = getenv("H2N_NETWORK");
		off = v && !strcmp(v, "off");
		if (off) h2n_log("H2N_NETWORK=off: the game is told there is no network (as before s9t)\n");
	}
	return off;
}
static void net_refresh(void)
{
	static get_adapters_fn get_adapters;
	static best_interface_fn best_interface;
	static int looked;
	static unsigned char buffer[65536];
	DWORD now = GetTickCount(), last = GetLastError();
	if (g_net.checked && now - g_net.checked < 2000) return;
	g_net.checked = now ? now : 1;
	if (!looked)
	{
		HMODULE ip = LoadLibraryA("iphlpapi.dll");
		get_adapters = ip ? (get_adapters_fn)GetProcAddress(ip, "GetAdaptersAddresses") : NULL;
		best_interface = ip ? (best_interface_fn)GetProcAddress(ip, "GetBestInterface") : NULL;
		looked = 1;
	}
	g_net.valid = 0;
	if (get_adapters && !network_off())
	{
		DWORD best = 0;
		ULONG size = sizeof(buffer);
		/* the adapter of the default route: the one Windows would send to an address outside the LAN through
		   (asking does not send anything) */
		if (!best_interface || best_interface(htonl(0x08080808), &best) != NO_ERROR) best = 0;
		if (get_adapters(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, NULL,
			(IP_ADAPTER_ADDRESSES *)buffer, &size) == NO_ERROR)
		{
			IP_ADAPTER_ADDRESSES *chosen = NULL;
			for (int pass = 0; pass < 2 && !chosen; pass++)       /* pass 1: no default route - the first adapter that is up */
				for (IP_ADAPTER_ADDRESSES *a = (IP_ADAPTER_ADDRESSES *)buffer; a && !chosen; a = a->Next)
				{
					IP_ADAPTER_UNICAST_ADDRESS *u;
					if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK || a->OperStatus != IfOperStatusUp) continue;
					if (pass == 0 && a->IfIndex != best) continue;
					for (u = a->FirstUnicastAddress; u; u = u->Next)
						if (u->Address.lpSockaddr && u->Address.lpSockaddr->sa_family == AF_INET)
						{
							DWORD v = ((struct sockaddr_in *)u->Address.lpSockaddr)->sin_addr.s_addr;
							if ((ntohl(v) >> 16) != 0xa9fe) { chosen = a; g_net.ip = v; break; }     /* not 169.254.x.x */
						}
				}
			if (chosen)
			{
				g_net.valid = 1;
				g_net.up = 1;
				g_net.index = chosen->IfIndex;
				memset(g_net.mac, 0, sizeof(g_net.mac));
				memcpy(g_net.mac, chosen->PhysicalAddress, chosen->PhysicalAddressLength < 6 ? chosen->PhysicalAddressLength : 6);
				g_net.speed = chosen->TransmitLinkSpeed;
				g_net.dhcp = (chosen->Flags & IP_ADAPTER_DHCP_ENABLED) != 0;
				g_net.gateway = chosen->FirstGatewayAddress != NULL;
				g_net.dns = chosen->FirstDnsServerAddress != NULL;
				WideCharToMultiByte(CP_UTF8, 0, chosen->FriendlyName ? chosen->FriendlyName : L"?", -1, g_net.name, sizeof(g_net.name), NULL, NULL);
			}
		}
	}
	if (!g_told_once || g_told.valid != g_net.valid || g_told.ip != g_net.ip || g_told.index != g_net.index)
	{
		if (g_net.valid)
		{
			BYTE *b = (BYTE *)&g_net.ip;
			h2n_log("[%4u] network: the PC's default adapter \"%s\": %u.%u.%u.%u, MAC %02x-%02x-%02x-%02x-%02x-%02x, %llu Mbps, %s%s%s\n",
				h2n_next_sequence(), g_net.name, b[0], b[1], b[2], b[3], g_net.mac[0], g_net.mac[1], g_net.mac[2], g_net.mac[3],
				g_net.mac[4], g_net.mac[5], g_net.speed / 1000000, g_net.dhcp ? "DHCP" : "static address",
				g_net.gateway ? ", a gateway" : "", g_net.dns ? ", DNS" : "");
		}
		else h2n_log("[%4u] network: no network adapter of the PC is up with an IPv4 address: the game sees no network\n", h2n_next_sequence());
		g_told = g_net;
		g_told_once = 1;
	}
	SetLastError(last);
}

DWORD __stdcall h2n_XNetGetEthernetLinkStatus(void) __asm__("_h2n_pc_XNetGetEthernetLinkStatus@0");
DWORD __stdcall h2n_XNetGetEthernetLinkStatus(void)
{
	DWORD result = 0;
	lock();
	net_refresh();
	if (g_net.valid && g_net.up)       /* the console knows 10 and 100 Mbps; a faster link is reported as 100 */
		result = LINK_ACTIVE | (g_net.speed >= 100000000ull || !g_net.speed ? LINK_100MBPS : LINK_10MBPS) | LINK_FULL_DUPLEX;
	unlock();
	return result;
}

DWORD __stdcall h2n_XNetGetTitleXnAddr(xn_addr *address) __asm__("_h2n_pc_XNetGetTitleXnAddr@4");
DWORD __stdcall h2n_XNetGetTitleXnAddr(xn_addr *address)
{
	DWORD result = XNADDR_NONE;
	lock();
	net_refresh();
	if (address)
	{
		memset(address, 0, sizeof(*address));
		if (g_net.valid)
		{
			address->ina = g_net.ip;
			memcpy(address->enet, g_net.mac, 6);
		}
	}
	if (g_net.valid) result = (g_net.dhcp ? XNADDR_DHCP : XNADDR_STATIC) | (g_net.gateway ? XNADDR_GATEWAY : 0) | (g_net.dns ? XNADDR_DNS : 0);
	unlock();
	return result;
}

INT __stdcall h2n_XNetStartup(const void *parameters) __asm__("_h2n_pc_XNetStartup@4");
INT __stdcall h2n_XNetStartup(const void *parameters)
{
	(void)parameters;
	h2n_log("[%4u] network: XNetStartup - the console's network library on the PC's own network\n", h2n_next_sequence());
	return 0;
}
INT __stdcall h2n_XNetCleanup(void) __asm__("_h2n_pc_XNetCleanup@0");
INT __stdcall h2n_XNetCleanup(void) { return 0; }

/* ---- session keys (cfgKeyRegMax: the game asks for 8) ---- */
static struct { int used; xn_kid kid; xn_key key; DWORD order; } g_keys[8];
static DWORD g_key_order;

INT __stdcall h2n_XNetCreateKey(xn_kid *kid, xn_key *key) __asm__("_h2n_pc_XNetCreateKey@8");
INT __stdcall h2n_XNetCreateKey(xn_kid *kid, xn_key *key)
{
	if (!kid || !key) return WSAEINVAL;
	h2n_XNetRandom(kid->ab, sizeof(kid->ab));
	h2n_XNetRandom(key->ab, sizeof(key->ab));
	kid->ab[0] &= 0x0f;                 /* XNET_XNKID_SYSTEM_LINK: the flag bits of the first byte clear */
	return 0;
}
INT __stdcall h2n_XNetRegisterKey(const xn_kid *kid, const xn_key *key) __asm__("_h2n_pc_XNetRegisterKey@8");
INT __stdcall h2n_XNetRegisterKey(const xn_kid *kid, const xn_key *key)
{
	int i, free_slot = -1, result = 0;
	if (!kid || !key) return WSAEINVAL;
	lock();
	for (i = 0; i < 8; i++)
	{
		if (g_keys[i].used && !memcmp(&g_keys[i].kid, kid, sizeof(*kid))) break;
		if (!g_keys[i].used && free_slot < 0) free_slot = i;
	}
	if (i == 8) i = free_slot;
	if (i < 0) result = WSAENOBUFS;
	else { g_keys[i].used = 1; g_keys[i].kid = *kid; g_keys[i].key = *key; g_keys[i].order = ++g_key_order; }
	unlock();
	return result;
}
INT __stdcall h2n_XNetUnregisterKey(const xn_kid *kid) __asm__("_h2n_pc_XNetUnregisterKey@4");
INT __stdcall h2n_XNetUnregisterKey(const xn_kid *kid)
{
	int result = WSAEINVAL;
	if (!kid) return WSAEINVAL;
	lock();
	for (int i = 0; i < 8; i++)
		if (g_keys[i].used && !memcmp(&g_keys[i].kid, kid, sizeof(*kid))) { g_keys[i].used = 0; result = 0; }
	unlock();
	return result;
}

/* ---- the peers' addresses: the IN_ADDR that stands for a peer is its own IPv4 address ---- */
static struct { int used; xn_addr address; xn_kid kid; DWORD order; } g_peers[64];
static DWORD g_peer_order;

static void peer_remember(const xn_addr *address, const xn_kid *kid)
{
	int i, oldest = 0;
	for (i = 0; i < 64; i++) if (g_peers[i].used && g_peers[i].address.ina == address->ina) break;
	if (i == 64)
	{
		for (i = 0; i < 64 && g_peers[i].used; i++)
			if (g_peers[i].order < g_peers[oldest].order) oldest = i;
		if (i == 64) i = oldest;
		{
			BYTE *b = (BYTE *)&address->ina;
			h2n_log("[%4u] network: peer %u.%u.%u.%u (MAC %02x-%02x-%02x-%02x-%02x-%02x)\n", h2n_next_sequence(), b[0], b[1], b[2], b[3],
				address->enet[0], address->enet[1], address->enet[2], address->enet[3], address->enet[4], address->enet[5]);
		}
	}
	g_peers[i].used = 1;
	g_peers[i].address = *address;
	if (kid) g_peers[i].kid = *kid;
	g_peers[i].order = ++g_peer_order;
}

INT __stdcall h2n_XNetXnAddrToInAddr(const xn_addr *address, const xn_kid *kid, DWORD *ina) __asm__("_h2n_pc_XNetXnAddrToInAddr@12");
INT __stdcall h2n_XNetXnAddrToInAddr(const xn_addr *address, const xn_kid *kid, DWORD *ina)
{
	if (ina) *ina = 0;
	if (!address || !ina || !address->ina) return WSAEINVAL;
	lock();
	peer_remember(address, kid);
	*ina = address->ina;
	unlock();
	return 0;
}

INT __stdcall h2n_XNetInAddrToXnAddr(DWORD ina, xn_addr *address, xn_kid *kid) __asm__("_h2n_pc_XNetInAddrToXnAddr@12");
INT __stdcall h2n_XNetInAddrToXnAddr(DWORD ina, xn_addr *address, xn_kid *kid)
{
	int i, result = WSAEINVAL;
	if (address) memset(address, 0, sizeof(*address));
	if (kid) memset(kid, 0, sizeof(*kid));
	lock();
	for (i = 0; i < 64; i++)
		if (g_peers[i].used && g_peers[i].address.ina == ina)
		{
			if (address) *address = g_peers[i].address;
			if (kid) *kid = g_peers[i].kid;
			result = 0;
			break;
		}
	if (result)
	{
		/* a peer this PC has not asked about yet (one joining the game it hosts): the console knows it through the
		   game's key it connected with; here that is the key registered last */
		int newest = -1;
		for (i = 0; i < 8; i++)
			if (g_keys[i].used && (newest < 0 || g_keys[i].order > g_keys[newest].order)) newest = i;
		if (newest >= 0 && ina)
		{
			xn_addr made;
			memset(&made, 0, sizeof(made));
			made.ina = ina;
			peer_remember(&made, &g_keys[newest].kid);
			if (address) *address = made;
			if (kid) *kid = g_keys[newest].kid;
			result = 0;
		}
	}
	unlock();
	return result;
}

/* a PC needs no secure connection set up first: every peer counts as connected */
INT __stdcall h2n_XNetConnect(DWORD ina) __asm__("_h2n_pc_XNetConnect@4");
INT __stdcall h2n_XNetConnect(DWORD ina) { (void)ina; return 0; }
DWORD __stdcall h2n_XNetGetConnectStatus(DWORD ina) __asm__("_h2n_pc_XNetGetConnectStatus@4");
DWORD __stdcall h2n_XNetGetConnectStatus(DWORD ina) { (void)ina; return 2; }      /* XNET_CONNECT_STATUS_CONNECTED */

/* ---- sockets: VDP is UDP here ---- */
typedef SOCKET (WSAAPI *socket_fn)(int, int, int);
SOCKET __stdcall h2n_socket(int family, int type, int protocol) __asm__("_h2n_pc_socket@12");
SOCKET __stdcall h2n_socket(int family, int type, int protocol)
{
	static socket_fn real;
	static LONG told;
	SOCKET s;
	DWORD error;
	if (!real)
	{
		HMODULE ws = LoadLibraryA("ws2_32.dll");
		real = ws ? (socket_fn)GetProcAddress(ws, "socket") : NULL;
	}
	if (!real) { SetLastError(WSAENETDOWN); return INVALID_SOCKET; }   /* (WSASetLastError is SetLastError) */
	s = real(family, type, protocol == VDP ? IPPROTO_UDP : protocol);
	error = GetLastError();               /* (WSAGetLastError is GetLastError; no import of ws2_32's names: nettramp.S has them) */
	if (InterlockedIncrement(&told) <= 16)
		h2n_log("[%4u] network: socket %s%s\n", h2n_next_sequence(), protocol == VDP ? "VDP (as UDP)" : protocol == IPPROTO_UDP ? "UDP" :
			protocol == IPPROTO_TCP ? "TCP" : "?", s == INVALID_SOCKET ? " - Windows refused it" : "");
	SetLastError(error);
	return s;
}
