// Minimal Bink test — open, summary, decode, copy, check multiple frames
#include <windows.h>
#include <stdio.h>

typedef void* HBINK;

int main(int argc, char* argv[]) {
    if (argc < 2) { printf("Usage: %s <bik_file> [out_w out_h]\n", argv[0]); return 1; }

    printf("=== Proxy_Bink32w Integration Test ===\n");
    HMODULE hMod = LoadLibraryA("binkw32.dll");
    if (!hMod) { printf("ERROR: LoadLibrary failed\n"); return 1; }

    auto BinkOpen = (HBINK(__stdcall*)(const char*, unsigned int))GetProcAddress(hMod, "_BinkOpen@8");
    auto BinkClose = (void(__stdcall*)(HBINK))GetProcAddress(hMod, "_BinkClose@4");
    auto BinkGetSummary = (void(__stdcall*)(HBINK, void*))GetProcAddress(hMod, "_BinkGetSummary@8");
    auto BinkDoFrame = (int(__stdcall*)(HBINK))GetProcAddress(hMod, "_BinkDoFrame@4");
    auto BinkWait = (int(__stdcall*)(HBINK))GetProcAddress(hMod, "_BinkWait@4");
    auto BinkNextFrame = (void(__stdcall*)(HBINK))GetProcAddress(hMod, "_BinkNextFrame@4");
    auto BinkCopyToBuffer = (int(__stdcall*)(HBINK, void*, int, int, int, int, int))GetProcAddress(hMod, "_BinkCopyToBuffer@28");

    printf("Opening %s...\n", argv[1]);
    HBINK bink = BinkOpen(argv[1], 0);
    if (!bink) { printf("ERROR: BinkOpen NULL\n"); return 1; }

    unsigned char summary[512] = {0};
    BinkGetSummary(bink, summary);
    unsigned int srcW = *(unsigned int*)(summary + 0);
    unsigned int srcH = *(unsigned int*)(summary + 4);
    printf("Source: %ux%u\n", srcW, srcH);

    int dstW = (argc >= 3) ? atoi(argv[2]) : srcW;
    int dstH = (argc >= 4) ? atoi(argv[3]) : srcH;
    int needScale = (dstW != (int)srcW || dstH != (int)srcH);
    printf("Output: %dx%d%s\n", dstW, dstH, needScale ? " (SCALING)" : "");

    int pitch = (dstW * 2 + 15) & ~15;
    void* buf = VirtualAlloc(NULL, pitch * dstH, MEM_COMMIT, PAGE_READWRITE);

    // Try first 5 frames
    for (int frame = 0; frame < 5; frame++) {
        BinkDoFrame(bink);
        BinkWait(bink);
        int result = BinkCopyToBuffer(bink, buf, pitch, dstH, 0, 0, 10);

        unsigned short* px = (unsigned short*)buf;
        int nz = 0;
        for (int i = 0; i < dstW * dstH && i < 10000; i++) if (px[i]) nz++;
        printf("Frame %d: result=%d nonzero=%d first=0x%04X\n", frame, result, nz, px[0]);

        BinkNextFrame(bink);
    }

    VirtualFree(buf, 0, MEM_RELEASE);
    BinkClose(bink);
    FreeLibrary(hMod);
    printf("Done!\n");
    return 0;
}
