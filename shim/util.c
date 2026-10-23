#include "shim.h"
#include <string.h>

void shim_log(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    wvsprintfA(buf, fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
}

void game_dir(char *buf, DWORD size)
{
    char *slash;
    GetModuleFileNameA(NULL, buf, size);
    slash = strrchr(buf, '\\');
    if (slash)
        slash[1] = '\0';
    else
        buf[0] = '\0';
}

void *iat_hook(const char *dll, const char *func, void *replacement)
{
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR *imp;

    if (!dir->VirtualAddress)
        return NULL;
    for (imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress); imp->Name; imp++) {
        IMAGE_THUNK_DATA *names, *addrs;
        if (lstrcmpiA((char *)(base + imp->Name), dll))
            continue;
        /* SAN4W95.EXE has no OriginalFirstThunk; names then live in FirstThunk on disk only. */
        names = (IMAGE_THUNK_DATA *)(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        addrs = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (; addrs->u1.Function; names++, addrs++) {
            void *old;
            DWORD prot;
            if (imp->OriginalFirstThunk) {
                if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)
                    || strcmp((char *)((IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData))->Name, func))
                    continue;
            } else if ((FARPROC)addrs->u1.Function != GetProcAddress(GetModuleHandleA(dll), func)) {
                continue;
            }
            old = (void *)addrs->u1.Function;
            VirtualProtect(&addrs->u1.Function, sizeof(void *), PAGE_READWRITE, &prot);
            addrs->u1.Function = (DWORD_PTR)replacement;
            VirtualProtect(&addrs->u1.Function, sizeof(void *), prot, &prot);
            return old;
        }
    }
    return NULL;
}
