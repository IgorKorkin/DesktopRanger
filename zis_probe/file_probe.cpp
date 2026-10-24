// Four real file operations. Windows build uses Win32; POSIX branch is only
// for checking the probe's reporting logic on the preparation machine.
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Advapi32.lib")
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
enum class Status { Allowed, Denied, NotExposed, Error };
struct Result { Status status; unsigned long error; };

const char* Name(Status s) {
    switch (s) {
    case Status::Allowed: return "ALLOWED";
    case Status::Denied: return "DENIED";
    case Status::NotExposed: return "NOT_EXPOSED";
    default: return "ERROR";
    }
}

Result Failure(unsigned long error, bool absenceExpected) {
#ifdef _WIN32
    const bool denied = error == ERROR_ACCESS_DENIED;
    const bool absent = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
#else
    const bool denied = error == EACCES || error == EPERM;
    const bool absent = error == ENOENT || error == ENOTDIR;
#endif
    if (denied) return {Status::Denied, error};
    if (absent && absenceExpected) return {Status::NotExposed, error};
    return {Status::Error, error};
}

Result Access(const fs::path& path, bool update, bool absenceExpected) {
    // OPEN_EXISTING / no O_CREAT: a missing canary must not be recreated.
    // Update exactly one byte at offset 0; do not truncate or print contents.
    char byte = 'X';
#ifdef _WIN32
    HANDLE h = CreateFileW(path.c_str(), update ? GENERIC_WRITE : GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return Failure(GetLastError(), absenceExpected);
    DWORD transferred = 0;
    const BOOL ok = update ? WriteFile(h, &byte, 1, &transferred, nullptr)
                           : ReadFile(h, &byte, 1, &transferred, nullptr);
    const DWORD error = ok ? (transferred == 1 ? ERROR_SUCCESS : ERROR_HANDLE_EOF)
                           : GetLastError();
    CloseHandle(h);
#else
    const int h = open(path.c_str(), update ? O_WRONLY : O_RDONLY);
    if (h < 0) return Failure(errno, absenceExpected);
    const auto n = update ? pwrite(h, &byte, 1, 0) : pread(h, &byte, 1, 0);
    const unsigned long error = n == 1 ? 0 : (n < 0 ? errno : EIO);
    close(h);
#endif
    return error == 0 ? Result{Status::Allowed, 0} : Failure(error, false);
}

#ifdef _WIN32
std::wstring QueryError(DWORD error) {
    return L"UNKNOWN (Win32 " + std::to_wstring(error) + L")";
}

std::wstring DesktopName() {
    // Borrowed handle: do not CloseDesktop(GetThreadDesktop(...)).
    HDESK desktop = GetThreadDesktop(GetCurrentThreadId());
    if (!desktop) return QueryError(GetLastError());
    DWORD bytes = 0;
    GetUserObjectInformationW(desktop, UOI_NAME, nullptr, 0, &bytes);
    if (!bytes) return QueryError(GetLastError());
    std::vector<wchar_t> name((bytes + sizeof(wchar_t) - 1) / sizeof(wchar_t));
    if (!GetUserObjectInformationW(desktop, UOI_NAME, name.data(), bytes, &bytes))
        return QueryError(GetLastError());
    return name.data();
}

std::wstring IntegrityLevel() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return QueryError(GetLastError());
    DWORD bytes = 0;
    GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &bytes);
    if (!bytes) {
        const DWORD error = GetLastError();
        CloseHandle(token);
        return QueryError(error);
    }
    std::vector<BYTE> buffer(bytes);
    const BOOL ok = GetTokenInformation(token, TokenIntegrityLevel,
                                       buffer.data(), bytes, &bytes);
    const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(token);
    if (!ok) return QueryError(error);
    const auto label = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(buffer.data());
    PSID sid = label->Label.Sid;
    if (!IsValidSid(sid) || *GetSidSubAuthorityCount(sid) == 0)
        return QueryError(ERROR_INVALID_SID);
    const DWORD rid = *GetSidSubAuthority(sid, *GetSidSubAuthorityCount(sid) - 1);
    const wchar_t* name = rid < SECURITY_MANDATORY_LOW_RID ? L"Untrusted"
        : rid < SECURITY_MANDATORY_MEDIUM_RID ? L"Low"
        : rid < SECURITY_MANDATORY_HIGH_RID ? L"Medium"
        : rid < SECURITY_MANDATORY_SYSTEM_RID ? L"High"
        : rid == SECURITY_MANDATORY_SYSTEM_RID ? L"System" : L"Other";
    return std::wstring(name) + L" (RID " + std::to_wstring(rid) + L")";
}

bool LowerFileProbeIntegrity() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_DEFAULT, &token)) {
        std::cerr << "OpenProcessToken(low control): " << GetLastError() << "\n";
        return false;
    }
    PSID sid = nullptr;
    if (!ConvertStringSidToSidW(L"S-1-16-4096", &sid)) {
        const DWORD error = GetLastError();
        CloseHandle(token);
        std::cerr << "ConvertStringSidToSidW(low control): " << error << "\n";
        return false;
    }
    TOKEN_MANDATORY_LABEL label{};
    label.Label.Sid = sid;
    label.Label.Attributes = SE_GROUP_INTEGRITY;
    const BOOL ok = SetTokenInformation(token, TokenIntegrityLevel, &label,
        static_cast<DWORD>(sizeof(label) + GetLengthSid(sid)));
    const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    LocalFree(sid);
    CloseHandle(token);
    if (!ok) std::cerr << "SetTokenInformation(low control): " << error << "\n";
    return ok != FALSE;
}

std::wstring AppContainerContext() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return QueryError(GetLastError());
    DWORD isContainer = 0, bytes = 0;
    if (!GetTokenInformation(token, TokenIsAppContainer, &isContainer, sizeof(isContainer), &bytes)) {
        const DWORD error = GetLastError();
        CloseHandle(token);
        return QueryError(error);
    }
    std::wstring result = L"TokenIsAppContainer=" + std::to_wstring(isContainer);
    if (isContainer) {
        GetTokenInformation(token, TokenAppContainerSid, nullptr, 0, &bytes);
        std::vector<BYTE> storage(bytes);
        if (!bytes || !GetTokenInformation(token, TokenAppContainerSid, storage.data(), bytes, &bytes)) {
            const DWORD error = GetLastError();
            CloseHandle(token);
            return result + L"; SID=" + QueryError(error);
        }
        const auto info = reinterpret_cast<TOKEN_APPCONTAINER_INFORMATION *>(storage.data());
        LPWSTR sid = nullptr;
        if (ConvertSidToStringSidW(info->TokenAppContainer, &sid)) {
            result += L"; SID=" + std::wstring(sid);
            LocalFree(sid);
        } else { result += L"; SID=" + QueryError(GetLastError()); }
    }
    CloseHandle(token);
    return result;
}

bool SaveFileContext(const fs::path &root, const std::string &mode, const std::string &backend) {
    std::wostringstream context;
    context << L"BackendLabel=" << std::wstring(backend.begin(), backend.end())
        << L"\nMode=" << std::wstring(mode.begin(), mode.end())
        << L"\nDesktop=" << DesktopName() << L"\nIntegrity=" << IntegrityLevel()
        << L"\n" << AppContainerContext()
        << L"\nBackendLabel is supplied by the launcher; retain its actual API log.\n";
    const auto value = context.str();
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return false;
    std::string utf8(bytes, '\0');
    if (!WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        utf8.data(), bytes, nullptr, nullptr)) return false;
    std::ofstream file(root / "sources" / "file-probe-context.txt", std::ios::binary | std::ios::trunc);
    file << utf8;
    file.close();
    std::cout << utf8;
    return static_cast<bool>(file);
}

bool ShowReport(const std::string& backend, const std::array<Result, 4>& results,
                bool csvOk) {
    const std::wstring backendName = backend == "classic" ? L"Classic AppContainer"
        : backend == "cpis" ? L"CPIS / SBOX" : backend == "psec" ? L"PSEC 1.1"
        : std::wstring(backend.begin(), backend.end());
    std::wostringstream text;
    text << L"Backend: " << backendName << L"\nDesktop: " << DesktopName()
         << L"\nIntegrity: " << IntegrityLevel() << L"\n\n";
    const wchar_t* labels[] = {L"Read  sources/main.cpp", L"Write sources/main.cpp",
                              L"Read  secrets/token.txt", L"Write secrets/token.txt"};
    for (size_t i = 0; i < results.size(); ++i) {
        const std::string status = Name(results[i].status);
        text << labels[i] << L" : " << std::wstring(status.begin(), status.end())
             << L" (" << results[i].error << L")\n";
    }
    text << L"\nCSV: " << (csvOk ? L"sources/file-probe-results.csv" : L"ERROR")
         << L"\nBackend is a launcher label; inspect the actual results.";
    if (MessageBoxW(nullptr, text.str().c_str(), L"KCon File Isolation Probe",
                    MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND) == 0) {
        std::cerr << "MessageBoxW failed: " << GetLastError() << "\n";
        return false;
    }
    return true;
}
#endif

int Run(const fs::path& root, const std::string& mode, const std::string& backend,
        bool messagebox = false) {
    if (mode != "baseline" && mode != "sandbox" && mode != "container") {
        std::cerr << "Mode must be baseline, sandbox or container.\n";
        return 64;
    }
    if (backend.empty() || backend.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")
            != std::string::npos) return 64;
    const bool baseline = mode == "baseline";
    const bool container = mode == "container";
    const Status secretExpected = baseline ? Status::Allowed
        : container ? Status::NotExposed : Status::Denied;
    struct Test { const char* operation; fs::path file; bool update;
                  bool absenceExpected; Status expected; };
    const std::array<Test, 4> tests{{
        {"sources.read", root / "sources" / "main.cpp", false, false, Status::Allowed},
        {"sources.update", root / "sources" / "main.cpp", true, false, Status::Allowed},
        {"secrets.read", root / "secrets" / "token.txt", false, container, secretExpected},
        {"secrets.update", root / "secrets" / "token.txt", true, container, secretExpected}
    }};
    const fs::path logPath = root / "sources" / "file-probe-results.csv";
    std::ofstream log(logPath, std::ios::binary | std::ios::trunc);
    if (!log) {
        std::cerr << "Cannot create CSV in the explicitly writable sources directory.\n";
        if (!messagebox) return 3;
    }
#ifdef _WIN32
    const char* domain = "Win32";
    if (!SaveFileContext(root, mode, backend)) {
        std::cerr << "Cannot persist actual token/Desktop context.\n";
        return 3;
    }
#else
    const char* domain = "POSIX";
#endif
    const char* header = "backend,operation,expected,actual,error_domain,error_code,match\n";
    std::cout << header;
    log << header;
    bool passed = true;
    std::array<Result, 4> results{};
    size_t index = 0;
    for (const auto& test : tests) {
        const Result r = Access(test.file, test.update, test.absenceExpected);
        results[index++] = r;
        const bool match = r.status == test.expected;
        passed = passed && match;
        const std::string line = backend + "," + test.operation + "," + Name(test.expected)
            + "," + Name(r.status) + "," + domain + "," + std::to_string(r.error)
            + "," + (match ? "true" : "false") + "\n";
        std::cout << line;
        log << line;
    }
    log.flush();
#ifdef _WIN32
    if (messagebox && !ShowReport(backend, results, static_cast<bool>(log))) return 3;
#endif
    if (!log) return 3;
    return passed ? 0 : 2;
}

#ifdef _WIN32
int RunFileProbe(int argc, wchar_t** argv) {
    if (argc < 4 || argc > 6) {
        std::cerr << "Usage: zis_probe.exe --file-test ROOT baseline|sandbox|container BACKEND [--messagebox] [--low-integrity]\n";
        return 64;
    }
    bool messagebox = false, lowIntegrity = false;
    for (int i = 4; i < argc; ++i) {
        const std::wstring option(argv[i]);
        if (option == L"--messagebox" && !messagebox) messagebox = true;
        else if (option == L"--low-integrity" && !lowIntegrity) lowIntegrity = true;
        else return 64;
    }
    if (lowIntegrity && (std::wstring(argv[2]) != L"baseline" || !LowerFileProbeIntegrity())) return 3;
    // Mode/backend are ASCII identifiers; filesystem paths stay UTF-16.
    const auto ascii = [](const wchar_t* value) {
        std::string result;
        for (; *value; ++value) {
            if (*value > 127) return std::string{};
            result.push_back(static_cast<char>(*value));
        }
        return result;
    };
    return Run(fs::path(argv[1]), ascii(argv[2]), ascii(argv[3]), messagebox);
}
#endif
