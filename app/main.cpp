#include <QApplication>
#include <QMainWindow>

#include "mainwindow.h"

#ifdef _WIN32
#include <winsock2.h>

#define WIN32_LEAN_AND_MEAN
#define NOGDI
#include <Windows.h>


char* utf8_encode(const wchar_t* wstr) {
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, 0, nullptr);
    char* utf8 = (char*)malloc((size + 1) * sizeof(char));
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, utf8, size, 0, nullptr);
    utf8[size] = 0;
    return utf8;
}

int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInst, LPSTR _lpCmdLine, int nShowCmd) {
    WSADATA wsaData;
    WSAStartup(0x0202, &wsaData);
    
    wchar_t* lpCmdLine = GetCommandLineW();

    int argc;
    wchar_t** argv_w = CommandLineToArgvW(lpCmdLine, &argc);

    char** argv = (char**)malloc(argc * sizeof(char*));

    for(unsigned i = 0; i < argc; i++) {
        argv[i] = utf8_encode(argv_w[i]);
    }

    LocalFree(argv_w);

    QApplication app(argc, argv);

    MainWindow mw;
    mw.show();

    int exitCode = app.exec();

    WSACleanup();

    for(unsigned i = 0; i < argc; i++) {
        free(argv[i]);
    }
    free(argv);
    return exitCode;
}

#else
int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    MainWindow mw;
    mw.show();

    return app.exec();
}
#endif