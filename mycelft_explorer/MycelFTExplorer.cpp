#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <bcrypt.h>
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

constexpr size_t kHmacSize = 32;
const wchar_t* kHmacStreamName = L"mycelft.hmac";

bool SeekFile(HANDLE file, uint64_t offset) {
    LARGE_INTEGER seekPos = {};
    seekPos.QuadPart = static_cast<LONGLONG>(offset);
    return SetFilePointerEx(file, seekPos, nullptr, FILE_BEGIN) != 0;
}

std::wstring AdsPath(const std::wstring& path, const std::wstring& streamName) {
    return path + L":" + streamName;
}

bool ReadAdsData(const std::wstring& path,
                 const std::wstring& streamName,
                 std::vector<uint8_t>& data,
                 bool& exists,
                 std::wstring& error) {
    exists = false;
    std::wstring streamPath = AdsPath(path, streamName);
    HANDLE stream = CreateFileW(streamPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (stream == INVALID_HANDLE_VALUE) {
        DWORD lastError = GetLastError();
        if (lastError == ERROR_FILE_NOT_FOUND || lastError == ERROR_PATH_NOT_FOUND) {
            return true;
        }
        error = L"Konnte HMAC-Stream nicht öffnen.";
        return false;
    }

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(stream, &size)) {
        CloseHandle(stream);
        error = L"Konnte HMAC-Stream nicht lesen.";
        return false;
    }

    data.resize(static_cast<size_t>(size.QuadPart));
    DWORD bytesRead = 0;
    if (size.QuadPart > 0 &&
        !ReadFile(stream, data.data(), static_cast<DWORD>(data.size()), &bytesRead, nullptr)) {
        CloseHandle(stream);
        error = L"Konnte HMAC-Stream nicht lesen.";
        return false;
    }

    CloseHandle(stream);
    exists = true;
    return true;
}

bool WriteAdsData(const std::wstring& path,
                  const std::wstring& streamName,
                  const std::vector<uint8_t>& data,
                  std::wstring& error) {
    std::wstring streamPath = AdsPath(path, streamName);
    HANDLE stream = CreateFileW(streamPath.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (stream == INVALID_HANDLE_VALUE) {
        error = L"Konnte HMAC-Stream nicht schreiben.";
        return false;
    }

    DWORD bytesWritten = 0;
    if (!data.empty() &&
        (!WriteFile(stream, data.data(), static_cast<DWORD>(data.size()), &bytesWritten, nullptr) ||
         bytesWritten != data.size())) {
        CloseHandle(stream);
        error = L"Konnte HMAC-Stream nicht schreiben.";
        return false;
    }

    CloseHandle(stream);
    return true;
}

void DeleteAdsData(const std::wstring& path, const std::wstring& streamName) {
    std::wstring streamPath = AdsPath(path, streamName);
    DeleteFileW(streamPath.c_str());
}

std::vector<uint8_t> DeriveHmacKey(uint64_t seed) {
    std::vector<uint8_t> key(kHmacSize);
    uint64_t state = seed ^ 0xA5A5A5A5A5A5A5A5ULL;
    for (size_t i = 0; i < key.size(); ++i) {
        state = MycelNextRand(state);
        key[i] = static_cast<uint8_t>(state & 0xFF);
    }
    return key;
}

bool InitHmac(uint64_t seed,
              BCRYPT_ALG_HANDLE& algHandle,
              BCRYPT_HASH_HANDLE& hashHandle,
              std::vector<BYTE>& hashObject,
              std::wstring& error) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg,
                                                  BCRYPT_SHA256_ALGORITHM,
                                                  nullptr,
                                                  BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (status != 0) {
        error = L"Konnte HMAC-Provider nicht öffnen.";
        return false;
    }

    DWORD objectLength = 0;
    DWORD cbResult = 0;
    status = BCryptGetProperty(hAlg,
                               BCRYPT_OBJECT_LENGTH,
                               reinterpret_cast<PUCHAR>(&objectLength),
                               sizeof(objectLength),
                               &cbResult,
                               0);
    if (status != 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        error = L"Konnte HMAC-Objektlänge nicht lesen.";
        return false;
    }

    hashObject.resize(objectLength);
    std::vector<uint8_t> key = DeriveHmacKey(seed);

    BCRYPT_HASH_HANDLE hHash = nullptr;
    status = BCryptCreateHash(hAlg,
                              &hHash,
                              hashObject.data(),
                              static_cast<ULONG>(hashObject.size()),
                              key.data(),
                              static_cast<ULONG>(key.size()),
                              0);
    if (status != 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        error = L"Konnte HMAC-Hash nicht erstellen.";
        return false;
    }

    algHandle = hAlg;
    hashHandle = hHash;
    return true;
}

bool FinishHmac(BCRYPT_ALG_HANDLE algHandle,
                BCRYPT_HASH_HANDLE hashHandle,
                std::vector<uint8_t>& outHash,
                std::wstring& error) {
    outHash.assign(kHmacSize, 0);
    NTSTATUS status = BCryptFinishHash(hashHandle,
                                       outHash.data(),
                                       static_cast<ULONG>(outHash.size()),
                                       0);
    BCryptDestroyHash(hashHandle);
    BCryptCloseAlgorithmProvider(algHandle, 0);
    if (status != 0) {
        error = L"Konnte HMAC nicht abschließen.";
        return false;
    }
    return true;
}

bool ComputeHmacForFile(HANDLE file, uint64_t seed, std::vector<uint8_t>& outHash, std::wstring& error) {
    if (!SeekFile(file, 0)) {
        error = L"Konnte Datei nicht lesen (Seek).";
        return false;
    }

    BCRYPT_ALG_HANDLE algHandle = nullptr;
    BCRYPT_HASH_HANDLE hashHandle = nullptr;
    std::vector<BYTE> hashObject;

    if (!InitHmac(seed, algHandle, hashHandle, hashObject, error)) {
        return false;
    }

    std::vector<uint8_t> buffer(kIoChunkSize);
    DWORD bytesRead = 0;
    while (ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) &&
           bytesRead > 0) {
        NTSTATUS status = BCryptHashData(hashHandle, buffer.data(), bytesRead, 0);
        if (status != 0) {
            BCryptDestroyHash(hashHandle);
            BCryptCloseAlgorithmProvider(algHandle, 0);
            error = L"Konnte HMAC nicht berechnen.";
            return false;
        }
    }

    return FinishHmac(algHandle, hashHandle, outHash, error);
}

bool TransformFilePass(HANDLE file,
                       uint64_t seed,
                       uint64_t fileSize,
                       bool computeHmac,
                       std::vector<uint8_t>* outHmac,
                       std::wstring& error) {
    if (!SeekFile(file, 0)) {
        error = L"Konnte Datei nicht lesen (Seek).";
        return false;
    }

    BCRYPT_ALG_HANDLE algHandle = nullptr;
    BCRYPT_HASH_HANDLE hashHandle = nullptr;
    std::vector<BYTE> hashObject;

    if (computeHmac) {
        if (!InitHmac(seed, algHandle, hashHandle, hashObject, error)) {
            return false;
        }
    }

    uint64_t offset = 0;
    std::vector<uint8_t> buffer(kIoChunkSize);
    while (offset < fileSize) {
        DWORD toRead = static_cast<DWORD>(std::min<uint64_t>(buffer.size(), fileSize - offset));
        DWORD bytesRead = 0;
        DWORD bytesWritten = 0;

        if (!ReadFile(file, buffer.data(), toRead, &bytesRead, nullptr)) {
            if (computeHmac) {
                BCryptDestroyHash(hashHandle);
                BCryptCloseAlgorithmProvider(algHandle, 0);
            }
            error = L"Datei konnte nicht gelesen werden.";
            return false;
        }

        MycelProcessBuffer(buffer.data(), bytesRead, seed, offset);

        if (computeHmac) {
            NTSTATUS status = BCryptHashData(hashHandle, buffer.data(), bytesRead, 0);
            if (status != 0) {
                BCryptDestroyHash(hashHandle);
                BCryptCloseAlgorithmProvider(algHandle, 0);
                error = L"Konnte HMAC nicht berechnen.";
                return false;
            }
        }

        if (!SeekFile(file, offset) ||
            !WriteFile(file, buffer.data(), bytesRead, &bytesWritten, nullptr) ||
            bytesWritten != bytesRead) {
            if (computeHmac) {
                BCryptDestroyHash(hashHandle);
                BCryptCloseAlgorithmProvider(algHandle, 0);
            }
            error = L"Datei konnte nicht geschrieben werden.";
            return false;
        }

        offset += bytesRead;
    }

    if (computeHmac) {
        if (!FinishHmac(algHandle, hashHandle, *outHmac, error)) {
            return false;
        }
    }
    return true;
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
    std::vector<uint8_t> storedHmac;
    bool hasHmac = false;
    if (!ReadAdsData(path, kHmacStreamName, storedHmac, hasHmac, error)) {
        CloseHandle(file);
        return false;
    }

    if (hasHmac) {
        if (storedHmac.size() != kHmacSize) {
            CloseHandle(file);
            error = L"HMAC-Stream hat ungültige Länge.";
            return false;
        }

        std::vector<uint8_t> computedHmac;
        if (!ComputeHmacForFile(file, seed, computedHmac, error)) {
            CloseHandle(file);
            return false;
        }

        if (computedHmac != storedHmac) {
            CloseHandle(file);
            error = L"Integritätsprüfung fehlgeschlagen.";
            return false;
        }

        if (!TransformFilePass(file,
                               seed,
                               static_cast<uint64_t>(fileSize.QuadPart),
                               false,
                               nullptr,
                               error)) {
            CloseHandle(file);
            return false;
        }

        DeleteAdsData(path, kHmacStreamName);
    } else {
        std::vector<uint8_t> computedHmac;
        if (!TransformFilePass(file,
                               seed,
                               static_cast<uint64_t>(fileSize.QuadPart),
                               true,
                               &computedHmac,
                               error)) {
            CloseHandle(file);
            return false;
        }

        if (!WriteAdsData(path, kHmacStreamName, computedHmac, error)) {
            CloseHandle(file);
            return false;
        }
    }

    CloseHandle(file);
    return true;
}

void ProcessFolderRecursive(const std::wstring& folder,
                            uint64_t& processed,
                            uint64_t& failed,
                            std::wstring& lastError) {
    std::wstring searchPath = folder;
    if (!searchPath.empty() && searchPath.back() != L'\\') {
        searchPath.push_back(L'\\');
    }
    searchPath += L"*";

    WIN32_FIND_DATAW findData = {};
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        lastError = L"Ordner konnte nicht gelesen werden.";
        failed++;
        return;
    }

    do {
        if (findData.cFileName[0] == L'.') {
            continue;
        }
        std::wstring itemPath = folder;
        if (!itemPath.empty() && itemPath.back() != L'\\') {
            itemPath.push_back(L'\\');
        }
        itemPath += findData.cFileName;

        bool isDir = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        bool isReparse = (findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

        if (isDir) {
            if (!isReparse) {
                ProcessFolderRecursive(itemPath, processed, failed, lastError);
            }
            continue;
        }

        std::wstring error;
        if (TransformFile(itemPath, error)) {
            processed++;
        } else {
            failed++;
            lastError = error;
        }
    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);
}

std::wstring RootFromPath(const std::wstring& path) {
    if (path.size() >= 2 && path[1] == L':') {
        std::wstring root = path.substr(0, 2);
        root.push_back(L'\\');
        return root;
    }
    return L"C:\\";
}

enum ControlId {
    kRefreshId = 1,
    kTransformId = 2,
    kOpenId = 3,
    kDeleteId = 4,
    kUpId = 5,
    kTransformFolderId = 6,
    kTransformDriveId = 7
};

struct UiState {
    HWND window = nullptr;
    HWND pathEdit = nullptr;
    HWND listView = nullptr;
    HWND statusBar = nullptr;
    HFONT uiFont = nullptr;
    HIMAGELIST smallIcons = nullptr;
};

struct ItemInfo {
    bool isDirectory = false;
    uint64_t size = 0;
};

void CleanupListViewItems(const UiState& ui);
void UpdateStatusBar(const UiState& ui);

std::wstring FormatSize(uint64_t size) {
    wchar_t buffer[64] = {};
    if (size >= (1024ULL * 1024ULL * 1024ULL)) {
        swprintf_s(buffer, L"%.2f GB", static_cast<double>(size) / (1024.0 * 1024.0 * 1024.0));
    } else if (size >= (1024ULL * 1024ULL)) {
        swprintf_s(buffer, L"%.2f MB", static_cast<double>(size) / (1024.0 * 1024.0));
    } else if (size >= 1024ULL) {
        swprintf_s(buffer, L"%.2f KB", static_cast<double>(size) / 1024.0);
    } else {
        swprintf_s(buffer, L"%llu B", static_cast<unsigned long long>(size));
    }
    return buffer;
}

void PopulateListView(const UiState& ui, const std::wstring& folder) {
    CleanupListViewItems(ui);
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

    std::vector<std::pair<std::wstring, ItemInfo>> entries;
    do {
        if (findData.cFileName[0] == L'.') {
            continue;
        }
        ItemInfo info = {};
        info.isDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (!info.isDirectory) {
            info.size = (static_cast<uint64_t>(findData.nFileSizeHigh) << 32) | findData.nFileSizeLow;
        }
        entries.emplace_back(findData.cFileName, info);
    } while (FindNextFileW(hFind, &findData));
    FindClose(hFind);

    std::stable_sort(entries.begin(), entries.end(),
                     [](const auto& a, const auto& b) {
                         if (a.second.isDirectory != b.second.isDirectory) {
                             return a.second.isDirectory > b.second.isDirectory;
                         }
                         return _wcsicmp(a.first.c_str(), b.first.c_str()) < 0;
                     });

    int index = 0;
    for (const auto& entry : entries) {
        LVITEMW item = {};
        item.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
        item.iItem = index;
        item.pszText = const_cast<LPWSTR>(entry.first.c_str());
        ItemInfo* info = new ItemInfo(entry.second);
        item.lParam = reinterpret_cast<LPARAM>(info);

        std::wstring fullPath = folder;
        if (!fullPath.empty() && fullPath.back() != L'\\') {
            fullPath.push_back(L'\\');
        }
        fullPath += entry.first;
        SHFILEINFOW sfi = {};
        SHGetFileInfoW(fullPath.c_str(),
                       entry.second.isDirectory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL,
                       &sfi,
                       sizeof(sfi),
                       SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
        item.iImage = sfi.iIcon;

        ListView_InsertItem(ui.listView, &item);

        std::wstring typeText = entry.second.isDirectory ? L"Folder" : L"File";
        ListView_SetItemText(ui.listView, index, 1, const_cast<LPWSTR>(typeText.c_str()));
        std::wstring sizeText = entry.second.isDirectory ? L"" : FormatSize(entry.second.size);
        ListView_SetItemText(ui.listView, index, 2, const_cast<LPWSTR>(sizeText.c_str()));
        ++index;
    }
    UpdateStatusBar(ui);
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

bool SelectedItemIsDirectory(const UiState& ui) {
    int index = ListView_GetNextItem(ui.listView, -1, LVNI_SELECTED);
    if (index < 0) {
        return false;
    }
    LVITEMW item = {};
    item.mask = LVIF_PARAM;
    item.iItem = index;
    if (!ListView_GetItem(ui.listView, &item)) {
        return false;
    }
    ItemInfo* info = reinterpret_cast<ItemInfo*>(item.lParam);
    return info && info->isDirectory;
}

void CleanupListViewItems(const UiState& ui) {
    int count = ListView_GetItemCount(ui.listView);
    for (int i = 0; i < count; ++i) {
        LVITEMW item = {};
        item.mask = LVIF_PARAM;
        item.iItem = i;
        if (ListView_GetItem(ui.listView, &item)) {
            delete reinterpret_cast<ItemInfo*>(item.lParam);
        }
    }
}

void UpdateStatusBar(const UiState& ui) {
    int total = ListView_GetItemCount(ui.listView);
    int selected = ListView_GetSelectedCount(ui.listView);
    wchar_t left[128] = {};
    wchar_t right[128] = {};
    swprintf_s(left, L"%d Elemente", total);
    if (selected == 1) {
        int index = ListView_GetNextItem(ui.listView, -1, LVNI_SELECTED);
        LVITEMW item = {};
        item.mask = LVIF_PARAM;
        item.iItem = index;
        if (ListView_GetItem(ui.listView, &item)) {
            ItemInfo* info = reinterpret_cast<ItemInfo*>(item.lParam);
            if (info && !info->isDirectory) {
                std::wstring sizeText = FormatSize(info->size);
                swprintf_s(right, L"%s", sizeText.c_str());
            } else {
                swprintf_s(right, L"Ordner");
            }
        }
    } else if (selected > 1) {
        swprintf_s(right, L"%d ausgewählt", selected);
    } else {
        right[0] = L'\0';
    }
    SendMessageW(ui.statusBar, SB_SETTEXT, 0, reinterpret_cast<LPARAM>(left));
    SendMessageW(ui.statusBar, SB_SETTEXT, 1, reinterpret_cast<LPARAM>(right));
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
            ui.uiFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            ui.pathEdit = CreateWindowExW(0, WC_EDITW, L"C:\\",
                                          WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT,
                                          10, 10, 400, 24, hwnd, nullptr, nullptr, nullptr);
            HWND refreshButton = CreateWindowExW(0, WC_BUTTONW, L"Refresh",
                                                 WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                 420, 10, 100, 24, hwnd,
                                                 reinterpret_cast<HMENU>(kRefreshId), nullptr, nullptr);
            HWND applyButton = CreateWindowExW(0, WC_BUTTONW, L"Encrypt/Decrypt",
                                               WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                               530, 10, 140, 24, hwnd,
                                               reinterpret_cast<HMENU>(kTransformId), nullptr, nullptr);
            HWND openButton = CreateWindowExW(0, WC_BUTTONW, L"Open",
                                              WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                              680, 10, 80, 24, hwnd,
                                              reinterpret_cast<HMENU>(kOpenId), nullptr, nullptr);
            HWND deleteButton = CreateWindowExW(0, WC_BUTTONW, L"Delete",
                                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                770, 10, 80, 24, hwnd,
                                                reinterpret_cast<HMENU>(kDeleteId), nullptr, nullptr);
            HWND upButton = CreateWindowExW(0, WC_BUTTONW, L"Up",
                                            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                            860, 10, 60, 24, hwnd,
                                            reinterpret_cast<HMENU>(kUpId), nullptr, nullptr);
            HWND folderButton = CreateWindowExW(0, WC_BUTTONW, L"Folder",
                                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                930, 10, 80, 24, hwnd,
                                                reinterpret_cast<HMENU>(kTransformFolderId), nullptr, nullptr);
            HWND driveButton = CreateWindowExW(0, WC_BUTTONW, L"Drive",
                                               WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                               1020, 10, 80, 24, hwnd,
                                               reinterpret_cast<HMENU>(kTransformDriveId), nullptr, nullptr);

            RECT rect;
            GetClientRect(hwnd, &rect);
            ui.listView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr,
                                          WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_EDITLABELS,
                                          10, 44, rect.right - 20, rect.bottom - 54,
                                          hwnd, nullptr, nullptr, nullptr);
            ListView_SetExtendedListViewStyle(ui.listView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            SetWindowTheme(ui.listView, L"Explorer", nullptr);

            SHFILEINFOW sfi = {};
            ui.smallIcons = reinterpret_cast<HIMAGELIST>(
                SHGetFileInfoW(L"C:\\", FILE_ATTRIBUTE_DIRECTORY, &sfi, sizeof(sfi),
                               SHGFI_SYSICONINDEX | SHGFI_SMALLICON));
            if (ui.smallIcons) {
                ListView_SetImageList(ui.listView, ui.smallIcons, LVSIL_SMALL);
            }

            LVCOLUMNW column = {};
            column.mask = LVCF_TEXT | LVCF_WIDTH;
            column.pszText = const_cast<LPWSTR>(L"Name");
            column.cx = rect.right - 320;
            ListView_InsertColumn(ui.listView, 0, &column);
            column.pszText = const_cast<LPWSTR>(L"Type");
            column.cx = 120;
            ListView_InsertColumn(ui.listView, 1, &column);
            column.pszText = const_cast<LPWSTR>(L"Size");
            column.cx = 120;
            ListView_InsertColumn(ui.listView, 2, &column);

            ui.statusBar = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr,
                                           WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                                           0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);
            int parts[2] = { rect.right - 200, -1 };
            SendMessageW(ui.statusBar, SB_SETPARTS, 2, reinterpret_cast<LPARAM>(parts));

            SendMessageW(ui.pathEdit, WM_SETFONT, reinterpret_cast<WPARAM>(ui.uiFont), TRUE);
            SendMessageW(refreshButton, WM_SETFONT, reinterpret_cast<WPARAM>(ui.uiFont), TRUE);
            SendMessageW(applyButton, WM_SETFONT, reinterpret_cast<WPARAM>(ui.uiFont), TRUE);
            SendMessageW(openButton, WM_SETFONT, reinterpret_cast<WPARAM>(ui.uiFont), TRUE);
            SendMessageW(deleteButton, WM_SETFONT, reinterpret_cast<WPARAM>(ui.uiFont), TRUE);
            SendMessageW(upButton, WM_SETFONT, reinterpret_cast<WPARAM>(ui.uiFont), TRUE);
            SendMessageW(folderButton, WM_SETFONT, reinterpret_cast<WPARAM>(ui.uiFont), TRUE);
            SendMessageW(driveButton, WM_SETFONT, reinterpret_cast<WPARAM>(ui.uiFont), TRUE);
            SendMessageW(ui.listView, WM_SETFONT, reinterpret_cast<WPARAM>(ui.uiFont), TRUE);
            SendMessageW(ui.statusBar, WM_SETFONT, reinterpret_cast<WPARAM>(ui.uiFont), TRUE);

            PopulateListView(ui, GetEditText(ui.pathEdit));
            return 0;
        }
        case WM_SIZE: {
            RECT rect;
            GetClientRect(hwnd, &rect);
            SendMessageW(ui.statusBar, WM_SIZE, 0, 0);
            RECT statusRect;
            GetWindowRect(ui.statusBar, &statusRect);
            int statusHeight = statusRect.bottom - statusRect.top;
            SetWindowPos(ui.listView, nullptr, 10, 44, rect.right - 20,
                         rect.bottom - 54 - statusHeight, SWP_NOZORDER);
            int parts[2] = { rect.right - 200, -1 };
            SendMessageW(ui.statusBar, SB_SETPARTS, 2, reinterpret_cast<LPARAM>(parts));
            return 0;
        }
        case WM_NOTIFY: {
            LPNMHDR hdr = reinterpret_cast<LPNMHDR>(lparam);
            if (hdr->hwndFrom == ui.listView) {
                if (hdr->code == NM_DBLCLK) {
                    std::wstring path = SelectedFilePath(ui);
                    if (path.empty()) {
                        return 0;
                    }
                    if (SelectedItemIsDirectory(ui)) {
                        SetWindowTextW(ui.pathEdit, path.c_str());
                        PopulateListView(ui, path);
                    } else {
                        ShellExecuteW(hwnd, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    }
                    return 0;
                }
                if (hdr->code == LVN_ENDLABELEDITW) {
                    NMLVDISPINFOW* info = reinterpret_cast<NMLVDISPINFOW*>(lparam);
                    if (info->item.pszText) {
                        wchar_t oldName[MAX_PATH] = {};
                        ListView_GetItemText(ui.listView, info->item.iItem, 0, oldName, MAX_PATH);
                        std::wstring folder = GetEditText(ui.pathEdit);
                        if (!folder.empty() && folder.back() != L'\\') {
                            folder.push_back(L'\\');
                        }
                        std::wstring oldPath = folder + oldName;
                        std::wstring newPath = folder + info->item.pszText;
                        if (!MoveFileW(oldPath.c_str(), newPath.c_str())) {
                            ShowMessage(hwnd, L"Umbenennen fehlgeschlagen.");
                            return 0;
                        }
                        PopulateListView(ui, folder);
                    }
                    return 0;
                }
                if (hdr->code == LVN_ITEMCHANGED) {
                    UpdateStatusBar(ui);
                    return 0;
                }
            }
            break;
        }
        case WM_COMMAND: {
            switch (LOWORD(wparam)) {
                case kRefreshId: {
                    PopulateListView(ui, GetEditText(ui.pathEdit));
                    return 0;
                }
                case kTransformId: {
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
                case kOpenId: {
                    std::wstring path = SelectedFilePath(ui);
                    if (path.empty()) {
                        ShowMessage(hwnd, L"Bitte eine Datei auswählen.");
                        return 0;
                    }
                    ShellExecuteW(hwnd, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    return 0;
                }
                case kDeleteId: {
                    std::wstring path = SelectedFilePath(ui);
                    if (path.empty()) {
                        ShowMessage(hwnd, L"Bitte eine Datei auswählen.");
                        return 0;
                    }
                    if (SelectedItemIsDirectory(ui)) {
                        if (!RemoveDirectoryW(path.c_str())) {
                            ShowMessage(hwnd, L"Ordner konnte nicht gelöscht werden.");
                        }
                    } else {
                        if (!DeleteFileW(path.c_str())) {
                            ShowMessage(hwnd, L"Datei konnte nicht gelöscht werden.");
                        }
                    }
                    PopulateListView(ui, GetEditText(ui.pathEdit));
                    return 0;
                }
                case kUpId: {
                    std::wstring folder = GetEditText(ui.pathEdit);
                    size_t pos = folder.find_last_of(L"\\/");
                    if (pos != std::wstring::npos && pos > 2) {
                        folder = folder.substr(0, pos);
                        SetWindowTextW(ui.pathEdit, folder.c_str());
                        PopulateListView(ui, folder);
                    }
                    return 0;
                }
                case kTransformFolderId: {
                    std::wstring folder = GetEditText(ui.pathEdit);
                    if (folder.empty()) {
                        ShowMessage(hwnd, L"Bitte einen Ordner angeben.");
                        return 0;
                    }
                    uint64_t processed = 0;
                    uint64_t failed = 0;
                    std::wstring lastError;
                    ProcessFolderRecursive(folder, processed, failed, lastError);
                    std::wstring message = L"Fertig. Dateien verarbeitet: " + std::to_wstring(processed);
                    if (failed > 0) {
                        message += L"\nFehler: " + std::to_wstring(failed);
                        if (!lastError.empty()) {
                            message += L"\nLetzter Fehler: " + lastError;
                        }
                    }
                    ShowMessage(hwnd, message);
                    PopulateListView(ui, folder);
                    return 0;
                }
                case kTransformDriveId: {
                    std::wstring folder = GetEditText(ui.pathEdit);
                    std::wstring root = RootFromPath(folder);
                    std::wstring prompt = L"Ganzen Datenträger " + root +
                                          L" verarbeiten? Dies kann lange dauern.";
                    if (MessageBoxW(hwnd, prompt.c_str(), L"MycelFT Explorer",
                                    MB_YESNO | MB_ICONWARNING) != IDYES) {
                        return 0;
                    }
                    uint64_t processed = 0;
                    uint64_t failed = 0;
                    std::wstring lastError;
                    ProcessFolderRecursive(root, processed, failed, lastError);
                    std::wstring message = L"Fertig. Dateien verarbeitet: " + std::to_wstring(processed);
                    if (failed > 0) {
                        message += L"\nFehler: " + std::to_wstring(failed);
                        if (!lastError.empty()) {
                            message += L"\nLetzter Fehler: " + lastError;
                        }
                    }
                    ShowMessage(hwnd, message);
                    PopulateListView(ui, folder);
                    return 0;
                }
                default:
                    break;
            }
            break;
        }
        case WM_DESTROY:
            CleanupListViewItems(ui);
            if (ui.uiFont) {
                DeleteObject(ui.uiFont);
            }
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
                                1180, 640, nullptr, nullptr, instance, nullptr);
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
