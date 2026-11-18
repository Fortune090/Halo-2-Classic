import json,sys,re,collections
res=json.load(open(sys.argv[1]))
sdk_names=re.compile(r"'(HANDLE|DWORD|BOOL|BYTE|WORD|LONG|ULONG|HRESULT|LPVOID|OVERLAPPED|FILETIME|SYSTEMTIME|LARGE_INTEGER|ULARGE_INTEGER|CRITICAL_SECTION|WIN32_FIND_DATA\w*|XINPUT\w*|XONLINE\w*|XNADDR|XNKID|XNKEY|XNQOS\w*|XUID|D3D\w*|LPDIRECT\w*|IDirect\w*|DS\w+|X[A-Z]\w+|INFINITE|INVALID_HANDLE_VALUE|GENERIC_\w+|FILE_\w+|OPEN_\w+|CREATE_\w+|ERROR_\w+|S_OK|E_\w+|FAILED|SUCCEEDED|MEM_\w+|PAGE_\w+|WAIT_\w+|TRUE|FALSE|SOCKET|sockaddr\w*|in_addr|WSA\w+|fd_set|timeval|[A-Z][A-Za-z]+(A|W|Ex)?|LPC?W?STR|WCHAR|CHAR|UINT|INT|FLOAT|SHORT|USHORT|UCHAR|PVOID|LPDWORD|LPBYTE|LPCVOID|SIZE_T|ULONGLONG|LONGLONG|__int64|PBYTE|RECT|POINT|GUID|va_list|NTSTATUS|PHYSICAL_ADDRESS|\w*_t)'")
cat=collections.Counter(); groups=collections.defaultdict(list)
for f,(rc,errs) in res.items():
    real=[e for e in errs if 'failed to rename' not in e]
    if not real: cat['compiles']+=1; continue
    if real==['timeout']: cat['timed out (not judged)']+=1; continue
    kinds=set()
    for e in real:
        m=re.search(r'error: (.*)',e).group(1)
        if re.search(r"unknown type name|use of undeclared identifier|incomplete type|no member named .* in namespace 'D3D'|expected class name|no type named",m): kinds.add('sdk')
        elif '__m128' in m or 'vector of' in m: kinds.add('m128 member access (MSVC-only)')
        elif 'wcs' in m or 'wchar_t' in m: kinds.add('wchar_t is a real type now')
        elif 'negative size' in m or 'variable length array' in m: kinds.add('follow-on')
        elif 'goto' in m: kinds.add('goto past an initialisation')
        else: kinds.add('other')
    k=kinds-{'follow-on'}
    if not k: k={'follow-on only'}
    if 'sdk' in k: cat['needs Xbox SDK declarations (can\'t judge further without the headers)']+=1
    else:
        key=' + '.join(sorted(k)); cat['language: '+key]+=1; groups[key].append(f)
for k,v in cat.most_common(): print(f'{v:5d}  {k}')
print(groups.get('other',[])[:12]); print(groups.get('follow-on only',[])[:8])
