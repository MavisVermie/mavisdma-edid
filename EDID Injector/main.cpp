#include "edid_sender.h"

#include <dwmapi.h>
#include <Uxtheme.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

#include "resource.h"

// Forward declarations
LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param);
void init_com_ports(HWND combo);
void browse_file(HWND hwnd);
void inject_edid(HWND hwnd);
std::vector<std::string> get_available_com_ports();
void handle_drop_files(HWND hwnd, HDROP hdrop);

// Controls IDs
#define ID_COMBO_COM      1001
#define ID_EDIT_FILE      1002
#define ID_BUTTON_BROWSE  1003
#define ID_BUTTON_INJECT  1004
#define ID_COMBO_BAUD     1005


#define TIMER_REFRESH      1

#define IDR_MANIFEST                    1
#define IDD_MAIN_DIALOG                101

const wchar_t CLASS_NAME[] = L"EDID_Injector_Window";

enum class IMMERSIVE_HC_CACHE_MODE {
    IHCM_USE_CACHED_VALUE,
    IHCM_REFRESH
};

enum class PreferredAppMode {
    Default,
    AllowDark,
    ForceDark,
    ForceLight,
    Max
};

// Dark mode helper functions declarations
typedef bool (WINAPI* RtlGetNtVersionNumbers)(LPDWORD major, LPDWORD minor, LPDWORD build);
typedef HRESULT(WINAPI* SetWindowCompositionAttribute)(HWND, void*);
typedef bool (WINAPI* ShouldAppsUseDarkMode)();
typedef PreferredAppMode(WINAPI* SetPreferredAppMode)(PreferredAppMode appMode);
typedef void (WINAPI* AllowDarkModeForWindow)(HWND hwnd, bool allow);
typedef void (WINAPI* RefreshImmersiveColorPolicyState)();

void init_dark_mode(HWND hwnd) {
    HMODULE uxtheme = LoadLibraryEx(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!uxtheme) return;

    auto set_preferred_app_mode = reinterpret_cast<SetPreferredAppMode>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)));
    auto allow_dark_mode_for_window = reinterpret_cast<AllowDarkModeForWindow>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(133)));
    auto refresh_immersive_color_policy = reinterpret_cast<RefreshImmersiveColorPolicyState>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(104)));

    if (set_preferred_app_mode) {
        set_preferred_app_mode(PreferredAppMode::ForceDark);
    }
    if (allow_dark_mode_for_window) {
        allow_dark_mode_for_window(hwnd, true);
    }
    if (refresh_immersive_color_policy) {
        refresh_immersive_color_policy();
    }

    // Set dark title bar
    BOOL value = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));

    SetWindowTheme(GetDlgItem(hwnd, ID_COMBO_COM), L"DarkMode_Explorer", nullptr);
    SetWindowTheme(GetDlgItem(hwnd, ID_EDIT_FILE), L"DarkMode_Explorer", nullptr);
    SetWindowTheme(GetDlgItem(hwnd, ID_BUTTON_BROWSE), L"DarkMode_Explorer", nullptr);
    SetWindowTheme(GetDlgItem(hwnd, ID_BUTTON_INJECT), L"DarkMode_Explorer", nullptr);

    FreeLibrary(uxtheme);
}

// Add these colors
const COLORREF COLOR_DARK_BG = RGB(28, 28, 28);         // Slightly darker background
const COLORREF COLOR_DARK_CONTROL_BG = RGB(37, 37, 37); // Softer control background
const COLORREF COLOR_DARK_TEXT = RGB(240, 240, 240);    // Slightly off-white text
const COLORREF COLOR_DARK_BORDER = RGB(60, 60, 60);     // Subtle border color

int WINAPI wWinMain(HINSTANCE h_instance, HINSTANCE h_prev_instance, PWSTR cmd_line, int n_cmd_show) {
    INITCOMMONCONTROLSEX icc = { sizeof(INITCOMMONCONTROLSEX) };
    icc.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSEX wc = { sizeof(WNDCLASSEX) };
    wc.lpfnWndProc = window_proc;
    wc.hInstance = h_instance;
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon = LoadIcon(h_instance, MAKEINTRESOURCE(IDI_ICON1));
    wc.hIconSm = LoadIcon(h_instance, MAKEINTRESOURCE(IDI_ICON1));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClassEx(&wc);

    int window_width = 450;
    int window_height = 180;
    int screen_width = GetSystemMetrics(SM_CXSCREEN);
    int screen_height = GetSystemMetrics(SM_CYSCREEN);
    int pos_x = (screen_width - window_width) / 2;
    int pos_y = (screen_height - window_height) / 2;

    HWND hwnd = CreateWindowEx(
        WS_EX_ACCEPTFILES,
        CLASS_NAME,
        L"MavisDMA Dichen Injector",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        pos_x, pos_y,
        window_width, window_height,
        NULL,
        NULL,
        h_instance,
        NULL
    );

    if (hwnd == NULL) {
        return 0;
    }

    ShowWindow(hwnd, n_cmd_show);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param) {
    switch (msg) {
    case WM_CREATE: {
        HFONT hFont = CreateFont(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        // COM Port label with softer text
        HWND label_com = CreateWindowEx(0, WC_STATIC, L"COM Port:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 22, 70, 20,
            hwnd, NULL, NULL, NULL);
        SendMessage(label_com, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Combo box with border
        HWND combo = CreateWindowEx(WS_EX_CLIENTEDGE, WC_COMBOBOX, L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            90, 20, 200, 200,
            hwnd, (HMENU)ID_COMBO_COM, NULL, NULL);
        SendMessage(combo, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND label_baud = CreateWindowEx(0, WC_STATIC, L"Baud:",
            WS_CHILD | WS_VISIBLE,
            300, 22, 40, 20,
            hwnd, NULL, NULL, NULL);
        SendMessage(label_baud, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND baud_combo = CreateWindowEx(0, WC_COMBOBOX, L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
            340, 20, 90, 200,
            hwnd, (HMENU)ID_COMBO_BAUD, NULL, NULL);
        SendMessage(baud_combo, WM_SETFONT, (WPARAM)hFont, TRUE);

        SendMessageA(baud_combo, CB_ADDSTRING, 0, (LPARAM)"921600");
        SendMessageA(baud_combo, CB_ADDSTRING, 0, (LPARAM)"115200");
        SendMessage(baud_combo, CB_SETCURSEL, 0, 0);  // Select 921600 by default

        // EDID File label
        HWND label_file = CreateWindowEx(0, WC_STATIC, L"EDID File:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 62, 70, 20,
            hwnd, NULL, NULL, NULL);
        SendMessage(label_file, WM_SETFONT, (WPARAM)hFont, TRUE);

        // File path edit box with softer edges
        HWND edit = CreateWindowEx(WS_EX_CLIENTEDGE, WC_EDIT, L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
            90, 60, 250, 23,
            hwnd, (HMENU)ID_EDIT_FILE, NULL, NULL);
        SendMessage(edit, WM_SETFONT, (WPARAM)hFont, TRUE);
        SetWindowText(edit, L"Drop EDID file here or click Browse...");

        // Browse button with modern style
        HWND browse = CreateWindowEx(0, WC_BUTTON, L"Browse...",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
            350, 59, 80, 25,
            hwnd, (HMENU)ID_BUTTON_BROWSE, NULL, NULL);
        SendMessage(browse, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Inject button with increased padding and modern style
        HWND inject = CreateWindowEx(0, WC_BUTTON, L"Inject EDID",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
            20, 100, 410, 35,  // Made slightly taller
            hwnd, (HMENU)ID_BUTTON_INJECT, NULL, NULL);
        SendMessage(inject, WM_SETFONT, (WPARAM)hFont, TRUE);

        SetTimer(hwnd, TIMER_REFRESH, 1000, NULL);

        init_dark_mode(hwnd);
        break;
    }

    case WM_COMMAND: {
        switch (LOWORD(w_param)) {
        case ID_BUTTON_BROWSE:
            browse_file(hwnd);
            break;

        case ID_BUTTON_INJECT:
            inject_edid(hwnd);
            break;
        }
        break;
    }

    case WM_DROPFILES: {
        handle_drop_files(hwnd, (HDROP)w_param);
        break;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)w_param;
        SetTextColor(hdc, COLOR_DARK_TEXT);
        SetBkColor(hdc, COLOR_DARK_CONTROL_BG);
        static HBRUSH hbrControl = CreateSolidBrush(COLOR_DARK_CONTROL_BG);
        return (INT_PTR)hbrControl;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)w_param;
        SetTextColor(hdc, COLOR_DARK_TEXT);
        SetBkColor(hdc, COLOR_DARK_BG);
        static HBRUSH hbrStatic = CreateSolidBrush(COLOR_DARK_BG);
        return (INT_PTR)hbrStatic;
    }

    case WM_CTLCOLORBTN: {
        HDC hdc = (HDC)w_param;
        SetTextColor(hdc, COLOR_DARK_TEXT);
        SetBkColor(hdc, COLOR_DARK_BG);  // Match background
        static HBRUSH hbrBtn = CreateSolidBrush(COLOR_DARK_BG);
        return (INT_PTR)hbrBtn;
    }

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)w_param;
        RECT rc;
        GetClientRect(hwnd, &rc);
        static HBRUSH hbrBkgnd = CreateSolidBrush(COLOR_DARK_BG);
        FillRect(hdc, &rc, hbrBkgnd);
        return TRUE;
    }

    case WM_TIMER: {
        if (w_param == TIMER_REFRESH) {
            HWND combo = GetDlgItem(hwnd, ID_COMBO_COM);
            char current_port[32] = "";
            int current_sel = SendMessage(combo, CB_GETCURSEL, 0, 0);
            if (current_sel != CB_ERR) {
                SendMessageA(combo, CB_GETLBTEXT, current_sel, (LPARAM)current_port);
            }

            SendMessage(combo, CB_RESETCONTENT, 0, 0);
            auto ports = get_available_com_ports();
            for (const auto& port : ports) {
                SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM)port.c_str());
                if (port == current_port) {
                    SendMessage(combo, CB_SETCURSEL, SendMessage(combo, CB_GETCOUNT, 0, 0) - 1, 0);
                }
            }
            if (SendMessage(combo, CB_GETCURSEL, 0, 0) == CB_ERR && !ports.empty()) {
                SendMessage(combo, CB_SETCURSEL, 0, 0);
            }
        }
        break;
    }

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_REFRESH);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, w_param, l_param);
}

bool validate_edid_file(const std::string& file_path) {
    // Read file
    std::vector<uint8_t> edid_data(256, 0);
    std::ifstream file(file_path, std::ios::binary);
    if (!file || !file.read(reinterpret_cast<char*>(edid_data.data()), 256)) {
        MessageBoxA(NULL, "File is not the correct size (should be 256 bytes)",
            "Invalid EDID", MB_ICONWARNING);
        return false;
    }
    file.close();

    // Check EDID header magic
    const uint8_t header[] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
    if (memcmp(edid_data.data(), header, sizeof(header)) != 0) {
        MessageBoxA(NULL, "Not a valid EDID file (invalid header)",
            "Invalid EDID", MB_ICONWARNING);
        return false;
    }

    // Validate checksum
    uint8_t sum = 0;
    for (int i = 0; i < 128; i++) {
        sum += edid_data[i];
    }
    if (sum != 0) {
        MessageBoxA(NULL, "EDID checksum validation failed",
            "Invalid EDID", MB_ICONWARNING);
        return false;
    }

    return true;
}

void handle_drop_files(HWND hwnd, HDROP hdrop) {
    char file_path[MAX_PATH];
    if (DragQueryFileA(hdrop, 0, file_path, MAX_PATH) > 0) {
        std::string path(file_path);
        if (path.length() > 4 && path.substr(path.length() - 4) == ".bin") {
            if(validate_edid_file(path))
                SetDlgItemTextA(hwnd, ID_EDIT_FILE, file_path);
        }
        else {
            MessageBox(hwnd, L"Please select a .bin file", L"Invalid File", MB_ICONWARNING);
        }
    }
    DragFinish(hdrop);
}

void inject_edid(HWND hwnd) {
    // Get selected COM port
    HWND combo = GetDlgItem(hwnd, ID_COMBO_COM);
    int sel = SendMessage(combo, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR) {
        MessageBox(hwnd, L"Please select a COM port", L"Error", MB_ICONERROR);
        return;
    }

    char port[32];
    SendMessageA(combo, CB_GETLBTEXT, sel, (LPARAM)port);

    // Get file path
    char file_path[MAX_PATH];
    GetDlgItemTextA(hwnd, ID_EDIT_FILE, file_path, MAX_PATH);
    if (file_path[0] == '\0' || strstr(file_path, "Drop EDID file here") != NULL) {
        MessageBox(hwnd, L"Please select an EDID file", L"Error", MB_ICONERROR);
        return;
    }

    // Disable inject button during operation
    EnableWindow(GetDlgItem(hwnd, ID_BUTTON_INJECT), FALSE);

    HWND baud_combo = GetDlgItem(hwnd, ID_COMBO_BAUD);
    int baud_sel = SendMessage(baud_combo, CB_GETCURSEL, 0, 0);
    DWORD baud_rate = baud_sel == 0 ? 921600 : 115200;

    // Create EDID sender and inject
    edid_sender sender(port);
    if (!sender.open_port()) {
        MessageBox(hwnd, L"Failed to open COM port", L"Error", MB_ICONERROR);
        EnableWindow(GetDlgItem(hwnd, ID_BUTTON_INJECT), TRUE);
        return;
    }

    if (!sender.try_baud_rate(baud_rate)) {
        MessageBox(hwnd, L"Failed to set baud rate", L"Error", MB_ICONERROR);
        EnableWindow(GetDlgItem(hwnd, ID_BUTTON_INJECT), TRUE);
        return;
    }

    if (!sender.send_edid(file_path)) {
        MessageBox(hwnd, L"EDID injection failed", L"Error", MB_ICONERROR);
        EnableWindow(GetDlgItem(hwnd, ID_BUTTON_INJECT), TRUE);
        return;
    }

    MessageBox(hwnd, L"EDID injection successful!", L"Success", MB_ICONINFORMATION);
    EnableWindow(GetDlgItem(hwnd, ID_BUTTON_INJECT), TRUE);
}

void browse_file(HWND hwnd) {
    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn = { sizeof(OPENFILENAMEA) };
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "EDID Files (*.bin)\0*.bin\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = "Select EDID File";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameA(&ofn))
        if(validate_edid_file(filename))
            SetDlgItemTextA(hwnd, ID_EDIT_FILE, filename);
}

std::vector<std::string> get_available_com_ports() {
    std::vector<std::string> ports;
    char port_name[32];

    for (int i = 1; i <= 256; i++) {
        sprintf_s(port_name, "COM%d", i);

        std::string full_path = "\\\\.\\" + std::string(port_name);
        HANDLE h_port = CreateFileA(
            full_path.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            0,
            NULL
        );

        if (h_port != INVALID_HANDLE_VALUE) {
            CloseHandle(h_port);
            ports.push_back(port_name);
        }
    }

    return ports;
}

void init_com_ports(HWND combo) {
    auto ports = get_available_com_ports();
    for (const auto& port : ports) {
        SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM)port.c_str());
    }
    if (!ports.empty()) {
        SendMessage(combo, CB_SETCURSEL, 0, 0);
    }
}