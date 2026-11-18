/* 64-bit arithmetic helpers, by the names and convention (arguments on the stack, callee pops) the SDK compiler uses */
long long __stdcall h2n_allmul(long long a, long long b) __asm__("__allmul");
long long __stdcall h2n_allmul(long long a, long long b) { return a * b; }
long long __stdcall h2n_alldiv(long long a, long long b) __asm__("__alldiv");
long long __stdcall h2n_alldiv(long long a, long long b) { return a / b; }
long long __stdcall h2n_allrem(long long a, long long b) __asm__("__allrem");
long long __stdcall h2n_allrem(long long a, long long b) { return a % b; }
unsigned long long __stdcall h2n_aulldiv(unsigned long long a, unsigned long long b) __asm__("__aulldiv");
unsigned long long __stdcall h2n_aulldiv(unsigned long long a, unsigned long long b) { return a / b; }
