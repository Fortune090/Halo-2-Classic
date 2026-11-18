import re,os,sys,collections
root=sys.argv[1]
pats=[('Direct3D',r'\b(D3D(?:Device|Texture|Surface|VertexBuffer|IndexBuffer|Resource|Palette|CubeTexture|VolumeTexture|BaseTexture|PushBuffer|X)?_\w+|Direct3D_\w+|XGSwizzle\w*|XGUnswizzle\w*|XG[A-Z]\w+)\s*\('),
      ('DirectSound',r'\b((?:I?DirectSound\w*|CDirectSound\w*|IDirectSoundBuffer|IDirectSoundStream|XMediaObject|XFileMediaObject|XWmaDecoder|CMcpxStream)\w*_\w+|DirectSound\w+|XAudio\w+|WmaCreate\w+|XWaveFileCreate\w+)\s*\('),
      ('Xbox Live',r'\b(XOnline\w+|_XOnline\w+)\s*\('),
      ('Network',r'\b(XNet\w+|WSA\w+|(?:socket|bind|sendto|recvfrom|select|closesocket|ioctlsocket|setsockopt|getsockopt|connect|listen|accept|send|recv|shutdown|inet_addr|htons|ntohs|htonl|ntohl)(?=\s*\())\s*\('),
      ('Voice',r'\b(XHV\w+|XVoice\w+)\s*\('),
      ('Bink',r'\b(Bink\w+)\s*\('),
      ('Xbox API',r'\b(X(?:Input|Get|Set|Mount|Unmount|Launch|Create|Find|Load|Free|Write|Read|Calculate|Close|Query|Mem|Physical|Signature|Format|Auto|Init|Mu|Cleanup)\w+|XapiBoot\w*)\s*\('),
      ('Kernel',r'\b((?:Nt|Ke|Rtl|Ex|Hal|Io|Mm|Ob|Ps|Av|Dbg|Xc|Xe|Fsc|Kd|Kf|Kfx|Ph)[A-Z][A-Za-z0-9]+)\s*\('),
      ('Win32-style',r'\b(CreateFileA|CreateFileW|ReadFile|WriteFile|CloseHandle|SetFilePointer|GetFileSize\w*|FindFirstFile\w*|FindNextFile\w*|FindClose|DeleteFile\w*|CreateDirectory\w*|GetLastError|SetLastError|CreateThread|CreateEvent\w*|SetEvent|ResetEvent|WaitFor\w+|Sleep\w*|QueryPerformance\w+|GetTickCount|VirtualAlloc\w*|VirtualFree\w*|GetOverlappedResult|InitializeCriticalSection|EnterCriticalSection|LeaveCriticalSection|DeleteCriticalSection|GetSystemTime\w*|SystemTimeToFileTime|FileTimeTo\w+|OutputDebugString\w*|TlsAlloc|TlsGetValue|TlsSetValue|GetCurrentThread\w*|SetThreadPriority\w*|ExitThread|Interlocked\w+|MoveFile\w*|CopyFile\w*|GetFileAttributes\w*|SetFileAttributes\w*|FlushFileBuffers|SetEndOfFile|GetDiskFreeSpace\w*|CreateSemaphore\w*|ReleaseSemaphore|CreateMutex\w*|ReleaseMutex|ResumeThread|SuspendThread|GetExitCodeThread|lstrc\w+|MultiByteToWideChar|WideCharToMultiByte|GlobalMemoryStatus)\s*\(')]
found={k:collections.Counter() for k,_ in pats}
comp=[(k,re.compile(p)) for k,p in pats]
for base in('src','include'):
    for d,_,ns in os.walk(os.path.join(root,base)):
        for n in ns:
            if not n.endswith(('.cpp','.h')): continue
            t=open(os.path.join(d,n),encoding='utf-8',errors='replace').read()
            t=re.sub(r'/\*.*?\*/','',t,flags=re.S); t=re.sub(r'//.*','',t)
            for k,c in comp:
                for m in c.finditer(t): found[k][m.group(1)]+=1
tot=0
for k,c in found.items():
    tot+=len(c)
    print(f'{k:12s} {len(c):4d} distinct, {sum(c.values()):6d} uses; most used: '+', '.join(f'{n}({v})' for n,v in c.most_common(7)))
print('total',tot)
import json; json.dump({k:dict(c) for k,c in found.items()},open(sys.argv[2],'w'),indent=1)
