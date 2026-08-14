#include <windows.h>
#include <stdio.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;
    const char msg[] = "Hello from real Windows x64 executable!\r\n";
    WriteConsoleA(hStdOut, msg, sizeof(msg) - 1, &written, NULL);
    ExitProcess(0);
    return 0;
}
