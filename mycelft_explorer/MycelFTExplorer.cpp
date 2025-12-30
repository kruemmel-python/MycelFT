#include <windows.h>
#include <commctrl.h>
#include <stdint.h>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

namespace {

constexpr int kGridW = 16;
constexpr int kGridH = 16;
constexpr int kGridSize = kGridW * kGridH;
constexpr size_t kKeyBlockSize = 256;
constexpr size_t kIoChunkSize = 64 * 1024;

uint64_t MycelNextRand(uint64_t& state) {
    uint64_t x = state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    state = x;
    return x;
}

float MycelFloatRand(uint64_t& state) {
    return static_cast<float>(MycelNextRand(state) & 0xFFFF) / 65536.0f;
}

float MycelSample(const std::vector<float>& grid, int x, int y) {
    x %= kGridW;
    if (x < 0) {
        x += kGridW;
    }
    y %= kGridH;
    if (y < 0) {
        y += kGridH;
    }
    return grid[(y * kGridW) + x];
}

float MycelLaplace(const std::vector<float>& grid, int x, int y) {
    float c = MycelSample(grid, x, y);
    float u = MycelSample(grid, x, y - 1);
    float d = MycelSample(grid, x, y + 1);
    float l = MycelSample(grid, x - 1, y);
    float r = MycelSample(grid, x + 1, y);
    return (u + d + l + r - (4.0f * c));
}

void MycelGenerateKeyBlock(uint64_t bioSeed, uint64_t blockIndex, uint8_t* keyBuffer, size_t bufferSize) {
    std::vector<float> energy(kGridSize);
    std::vector<float> phase(kGridSize);
    std::vector<float> nextEnergy(kGridSize);
    uint64_t rngState = bioSeed ^ (blockIndex * 0x9E3779B97F4A7C15ULL);

    for (int i = 0; i < kGridSize; ++i) {
        energy[i] = MycelFloatRand(rngState);
        phase[i] = MycelFloatRand(rngState) * 6.2831853f;
    }

    constexpr int kSimSteps = 4;
    for (int step = 0; step < kSimSteps; ++step) {
        for (int y = 0; y < kGridH; ++y) {
            for (int x = 0; x < kGridW; ++x) {
                int idx = (y * kGridW) + x;
                float lap = MycelLaplace(energy, x, y);
                float noise = (MycelFloatRand(rngState) - 0.5f) * 0.1f;
                float dE = (0.2f * lap) + noise + (0.05f * sinf(phase[idx]));
                float val = energy[idx] + dE;
                if (val < 0.0f) {
                    val = 0.0f;
                } else if (val > 1.0f) {
                    val = 1.0f;
                }
                nextEnergy[idx] = val;
            }
        }
        std::swap(energy, nextEnergy);
    }

    for (size_t i = 0; i < bufferSize; ++i) {
        union {
            float f;
            uint32_t u;
        } conv;
        conv.f = energy[i % kGridSize];
        uint32_t hash = (conv.u ^ (conv.u >> 16)) * 0x45d9f3b;
        keyBuffer[i] = static_cast<uint8_t>(hash & 0xFF);
    }
}

void MycelProcessBuffer(uint8_t* buffer, size_t length, uint64_t bioSeed, uint64_t streamOffset) {
    size_t offset = 0;
    uint8_t keyBlock[kKeyBlockSize];

    while (offset < length) {
        uint64_t absPos = streamOffset + offset;
        uint64_t blockIndex = absPos / kKeyBlockSize;
        size_t blockOffset = static_cast<size_t>(absPos % kKeyBlockSize);
        size_t bytesToProcess = kKeyBlockSize - blockOffset;
        if (bytesToProcess > (length - offset)) {
            bytesToProcess = length - offset;
        }

        MycelGenerateKeyBlock(bioSeed, blockIndex, keyBlock, kKeyBlockSize);
        for (size_t i = 0; i < bytesToProcess; ++i) {
            buffer[offset + i] ^= keyBlock[blockOffset + i];
        }

        offset += bytesToProcess;
    }
}

uint64_t HashNameWithSerial(const std::wstring& name, uint32_t volumeSerial) {
    uint32_t hash = volumeSerial;
    for (wchar_t ch : name) {
        hash = (hash * 31) + static_cast<uint16_t>(ch);
    }
    return static_cast<uint64_t>(hash) * 0x9E3779B97F4A7C15ULL;
}

std::wstring BaseName(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return path;
    }
    return path.substr(pos + 1);
}

uint64_t MycelSeedForPath(const std::wstring& path) {
    wchar_t rootPath[MAX_PATH] = {};
    std::wstring drive = path.substr(0, std::min<size_t>(3, path.size()));
    if (drive.size() >= 2 && drive[1] == L':') {
        wcscpy_s(rootPath, drive.c_str());
        wcscat_s(rootPath, L"\\");
    } else {
        GetCurrentDirectoryW(MAX_PATH, rootPath);
        if (wcslen(rootPath) >= 2) {
            rootPath[3] = L'\0';
        }
    }

    DWORD serial = 0;
    GetVolumeInformationW(rootPath, nullptr, 0, &serial, nullptr, nullptr, nullptr, 0);
    return HashNameWithSerial(BaseName(path), serial);
}

bool TransformFile(const std::wstring& path, std::wstring& error) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"Datei konnte nicht geöffnet werden.";
        return false;
    }

    LARGE_INTEGER fileSize = {};
    if (!GetFileSizeEx(file, &fileSize)) {
        CloseHandle(file);
        error = L"Dateigröße konnte nicht gelesen werden.";
        return false;
    }

    uint64_t seed = MycelSeedForPath(path);
    uint64_t offset = 0;
    std::vector<uint8_t> buffer(kIoChunkSize);

    while (offset < static_cast<uint64_t>(fileSize.QuadPart)) {
        DWORD toRead = static_cast<DWORD>(
            std::min<uint64_t>(buffer.size(), fileSize.QuadPart - offset));
        DWORD bytesRead = 0;
        DWORD bytesWritten = 0;

        if (!SetFilePointerEx(file, {static_cast<LONGLONG>(offset)}, nullptr, FILE_BEGIN) ||
            !ReadFile(file, buffer.data(), toRead, &bytesRead, nullptr)) {
            CloseHandle(file);
            error = L"Datei konnte nicht gelesen werden.";
            return false;
        }

        MycelProcessBuffer(buffer.data(), bytesRead, seed, offset);

        if (!SetFilePointerEx(file, {static_cast<LONGLONG>(offset)}, nullptr, FILE_BEGIN) ||
            !WriteFile(file, buffer.data(), bytesRead, &bytesWritten, nullptr) ||
            bytesWritten != bytesRead) {
            CloseHandle(file);
            error = L"Datei konnte nicht geschrieben werden.";
            return false;
        }

        offset += bytesRead;
    }

    CloseHandle(file);
    return true;
}

struct UiState {
    HWND window = nullptr;
    HWND pathEdit = nullptr;
    HWND listView = nullptr;
};

void PopulateListView(const UiState& ui, const std::wstring& folder) {
    ListView_DeleteAllItems(ui.listView);

    std::wstring searchPath = folder;
    if (!searchPath.empty() && searchPath.back() != L'\\') {
        searchPath.push_back(L'\\');
    }
    searchPath += L"*";

    WIN32_FIND_DATAW findData = {};
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        return;
    }

    int index = 0;
    do {
        if (findData.cFileName[0] == L'.') {
            continue;
        }
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = index++;
        item.pszText = findData.cFileName;
        ListView_InsertItem(ui.listView, &item);
    } while (FindNextFileW(hFind, &findData));
    FindClose(hFind);
}

std::wstring GetEditText(HWND edit) {
    int length = GetWindowTextLengthW(edit);
    std::wstring text(length, L'\0');
    GetWindowTextW(edit, &text[0], length + 1);
    return text;
}

std::wstring SelectedFilePath(const UiState& ui) {
    int index = ListView_GetNextItem(ui.listView, -1, LVNI_SELECTED);
    if (index < 0) {
        return {};
    }
    wchar_t name[MAX_PATH] = {};
    ListView_GetItemText(ui.listView, index, 0, name, MAX_PATH);
    std::wstring folder = GetEditText(ui.pathEdit);
    if (!folder.empty() && folder.back() != L'\\') {
        folder.push_back(L'\\');
    }
    return folder + name;
}

void ShowMessage(HWND hwnd, const std::wstring& message) {
    MessageBoxW(hwnd, message.c_str(), L"MycelFT Explorer", MB_OK | MB_ICONINFORMATION);
}

}  // namespace

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    static UiState ui;

    switch (msg) {
        case WM_CREATE: {
            ui.window = hwnd;
            ui.pathEdit = CreateWindowExW(0, WC_EDITW, L"C:\\",
                                          WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT,
                                          10, 10, 400, 24, hwnd, nullptr, nullptr, nullptr);
            HWND refreshButton = CreateWindowExW(0, WC_BUTTONW, L"Refresh",
                                                 WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                 420, 10, 100, 24, hwnd,
                                                 reinterpret_cast<HMENU>(1), nullptr, nullptr);
            HWND applyButton = CreateWindowExW(0, WC_BUTTONW, L"Encrypt/Decrypt",
                                               WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                               530, 10, 140, 24, hwnd,
                                               reinterpret_cast<HMENU>(2), nullptr, nullptr);

            RECT rect;
            GetClientRect(hwnd, &rect);
            ui.listView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr,
                                          WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                                          10, 44, rect.right - 20, rect.bottom - 54,
                                          hwnd, nullptr, nullptr, nullptr);

            LVCOLUMNW column = {};
            column.mask = LVCF_TEXT | LVCF_WIDTH;
            column.pszText = const_cast<LPWSTR>(L"Name");
            column.cx = rect.right - 40;
            ListView_InsertColumn(ui.listView, 0, &column);

            PopulateListView(ui, GetEditText(ui.pathEdit));
            return 0;
        }
        case WM_SIZE: {
            RECT rect;
            GetClientRect(hwnd, &rect);
            SetWindowPos(ui.listView, nullptr, 10, 44, rect.right - 20, rect.bottom - 54, SWP_NOZORDER);
            return 0;
        }
        case WM_COMMAND: {
            switch (LOWORD(wparam)) {
                case 1: {
                    PopulateListView(ui, GetEditText(ui.pathEdit));
                    return 0;
                }
                case 2: {
                    std::wstring path = SelectedFilePath(ui);
                    if (path.empty()) {
                        ShowMessage(hwnd, L"Bitte eine Datei auswählen.");
                        return 0;
                    }
                    std::wstring error;
                    if (TransformFile(path, error)) {
                        ShowMessage(hwnd, L"Datei wurde verarbeitet.");
                    } else {
                        ShowMessage(hwnd, error);
                    }
                    return 0;
                }
                default:
                    break;
            }
            break;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCmd) {
    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icc);

    const wchar_t kClassName[] = L"MycelFTExplorerWindow";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, kClassName, L"MycelFT Explorer",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                720, 480, nullptr, nullptr, instance, nullptr);
    if (!hwnd) {
        return 0;
    }

    ShowWindow(hwnd, showCmd);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
