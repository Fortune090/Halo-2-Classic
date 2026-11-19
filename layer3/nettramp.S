/* nettramp.S - the game's 34 socket and XNet calls (s9u): each jumps through h2n_net_ptr[i], which xnetx.c points at
 * the PC's functions or (H2N_XNET=retail) the XBE's own network library. Same order as xnetx.c's g_net. */
.text
.globl "_socket@12"
"_socket@12":
	jmp *_h2n_net_ptr+0
.globl "_bind@12"
"_bind@12":
	jmp *_h2n_net_ptr+4
.globl "_sendto@24"
"_sendto@24":
	jmp *_h2n_net_ptr+8
.globl "_recvfrom@24"
"_recvfrom@24":
	jmp *_h2n_net_ptr+12
.globl "_recv@16"
"_recv@16":
	jmp *_h2n_net_ptr+16
.globl "_send@16"
"_send@16":
	jmp *_h2n_net_ptr+20
.globl "_select@20"
"_select@20":
	jmp *_h2n_net_ptr+24
.globl "_ioctlsocket@12"
"_ioctlsocket@12":
	jmp *_h2n_net_ptr+28
.globl "_setsockopt@20"
"_setsockopt@20":
	jmp *_h2n_net_ptr+32
.globl "_getsockopt@20"
"_getsockopt@20":
	jmp *_h2n_net_ptr+36
.globl "_closesocket@4"
"_closesocket@4":
	jmp *_h2n_net_ptr+40
.globl "_connect@12"
"_connect@12":
	jmp *_h2n_net_ptr+44
.globl "_shutdown@8"
"_shutdown@8":
	jmp *_h2n_net_ptr+48
.globl "___WSAFDIsSet@8"
"___WSAFDIsSet@8":
	jmp *_h2n_net_ptr+52
.globl "_WSAStartup@8"
"_WSAStartup@8":
	jmp *_h2n_net_ptr+56
.globl "_WSACleanup@0"
"_WSACleanup@0":
	jmp *_h2n_net_ptr+60
.globl "_WSAGetLastError@0"
"_WSAGetLastError@0":
	jmp *_h2n_net_ptr+64
.globl "_XNetStartup@4"
"_XNetStartup@4":
	jmp *_h2n_net_ptr+68
.globl "_XNetCleanup@0"
"_XNetCleanup@0":
	jmp *_h2n_net_ptr+72
.globl "_XNetGetTitleXnAddr@4"
"_XNetGetTitleXnAddr@4":
	jmp *_h2n_net_ptr+76
.globl "_XNetGetEthernetLinkStatus@0"
"_XNetGetEthernetLinkStatus@0":
	jmp *_h2n_net_ptr+80
.globl "_XNetCreateKey@8"
"_XNetCreateKey@8":
	jmp *_h2n_net_ptr+84
.globl "_XNetRegisterKey@8"
"_XNetRegisterKey@8":
	jmp *_h2n_net_ptr+88
.globl "_XNetUnregisterKey@4"
"_XNetUnregisterKey@4":
	jmp *_h2n_net_ptr+92
.globl "_XNetXnAddrToInAddr@12"
"_XNetXnAddrToInAddr@12":
	jmp *_h2n_net_ptr+96
.globl "_XNetInAddrToXnAddr@12"
"_XNetInAddrToXnAddr@12":
	jmp *_h2n_net_ptr+100
.globl "_XNetConnect@4"
"_XNetConnect@4":
	jmp *_h2n_net_ptr+104
.globl "_XNetGetConnectStatus@4"
"_XNetGetConnectStatus@4":
	jmp *_h2n_net_ptr+108
.globl "_XNetQosListen@20"
"_XNetQosListen@20":
	jmp *_h2n_net_ptr+112
.globl "_XNetQosLookup@48"
"_XNetQosLookup@48":
	jmp *_h2n_net_ptr+116
.globl "_XNetQosRelease@4"
"_XNetQosRelease@4":
	jmp *_h2n_net_ptr+120
.globl "_XNetQosServiceLookup@12"
"_XNetQosServiceLookup@12":
	jmp *_h2n_net_ptr+124
.globl "_XNetTsAddrToInAddr@16"
"_XNetTsAddrToInAddr@16":
	jmp *_h2n_net_ptr+128
.globl "_XNetRandom@8"
"_XNetRandom@8":
	jmp *_h2n_net_ptr+132
