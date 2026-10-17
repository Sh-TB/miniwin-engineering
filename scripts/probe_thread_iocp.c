/* BUG-062/064 probe: isolated guest-thread + IOCP chain test.
 * Thread A: computes a value, returns it via _endthreadex.
 * Main: CreateThread -> WaitForSingleObject -> GetExitCodeThread.
 * Also exercises Post/GetQueuedCompletionStatus handoff. */
#include <windows.h>
#include <stdio.h>

static DWORD WINAPI worker(LPVOID arg) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    const char* msg = "THREAD-RAN\n";
    DWORD written = 0;
    WriteFile(out, msg, 11, &written, NULL);
    /* IOCP round trip: post a completion, get it back */
    HANDLE port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    PostQueuedCompletionStatus(port, 42, 1234, (LPOVERLAPPED)0x5678);
    DWORD bytes = 0; ULONG_PTR key = 0; LPOVERLAPPED ovl = NULL;
    BOOL ok = GetQueuedCompletionStatus(port, &bytes, &key, &ovl, 1000);
    char buf[80];
    int n = wsprintfA(buf, "IOCP ok=%d bytes=%lu key=%lu\n", (int)ok, bytes, (unsigned long)key);
    WriteFile(out, buf, n, &written, NULL);
    return 7; /* expected exit code */
}

int main(void) {
    HANDLE t = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    if (!t) { printf("PROBE-FAIL: CreateThread\n"); return 2; }
    DWORD ec = 0;
    DWORD w = WaitForSingleObject(t, 5000);
    printf("wait=%lu (0=OK,0x102=timeout)\n", w);
    GetExitCodeThread(t, &ec);
    printf("exit_code=%lu (expect 7)\n", ec);
    CloseHandle(t);
    return (w == 0 && ec == 7) ? 0 : 3;
}
