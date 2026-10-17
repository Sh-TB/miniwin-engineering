/* Step-wise IOCP probe: WriteFile markers around each call. */
#include <windows.h>

static void say(const char* s) {
    DWORD w = 0;
    unsigned n = 0; while (s[n]) n++;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, n, &w, NULL);
}

static DWORD WINAPI worker(LPVOID arg) {
    (void)arg;
    say("T1\n");
    HANDLE port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    say("T2-created\n");
    BOOL p = PostQueuedCompletionStatus(port, 42, 1234, (LPOVERLAPPED)0x5678);
    say("T3-posted\n");
    DWORD bytes = 0; ULONG_PTR key = 0; LPOVERLAPPED ovl = NULL;
    BOOL ok = GetQueuedCompletionStatus(port, &bytes, &key, &ovl, 1000);
    say("T4-got\n");
    (void)p; (void)ok;
    return 7;
}

int main(void) {
    say("M1\n");
    HANDLE t = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    say("M2-created\n");
    DWORD w = WaitForSingleObject(t, 5000);
    say("M3-waited\n");
    DWORD ec = 0;
    GetExitCodeThread(t, &ec);
    say("M4-exitcode\n");
    char buf[64];
    /* manual small formatter, no CRT printf here */
    const char* hex = "0123456789abcdef";
    buf[0] = 'r'; buf[1] = 'e'; buf[2] = 's'; buf[3] = '=';
    for (int i = 0; i < 8; i++) {
        buf[4 + i] = hex[((w >> (28 - 4 * i)) & 0xF)];
    }
    buf[12] = ' ';
    for (int i = 0; i < 8; i++) {
        buf[13 + i] = hex[((ec >> (28 - 4 * i)) & 0xF)];
    }
    buf[21] = '\n'; buf[22] = 0;
    say(buf);
    return (w == 0 && ec == 7) ? 0 : 3;
}
