// Input Recorder — application entry point.
//
// A Win32 GUI application (no console): it assembles storage, capture,
// reconstruction, the main window and the tray icon via ir::Application and runs
// the message loop until the user exits (spec §566). All of the behaviour lives
// in the unit-tested library; this file only launches it.

#include <Windows.h>

#include "app/application.hpp"

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    // A single running instance owns the global hooks (spec §412). A named mutex
    // detects a prior instance; if present, we exit quietly.
    HANDLE instance_guard =
        CreateMutexW(nullptr, TRUE, L"Local\\InputRecorderSingleInstance");
    if (instance_guard != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
        return 0;
    }

    int exit_code = 0;
    {
        ir::Application app;
        if (!app.initialize(hInstance, nCmdShow)) {
            MessageBoxW(nullptr,
                        L"Input Recorder could not start (its local database "
                        L"could not be opened).",
                        L"Input Recorder", MB_ICONERROR | MB_OK);
            exit_code = 1;
        } else {
            exit_code = app.run();
        }
    }

    if (instance_guard != nullptr) {
        ReleaseMutex(instance_guard);
        CloseHandle(instance_guard);
    }
    return exit_code;
}
