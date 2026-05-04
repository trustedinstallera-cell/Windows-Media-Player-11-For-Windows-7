#define LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <string>
#include <vector>
#include <locale>
#include <Windows.h>
#include <sddl.h>
#include <comdef.h>
#include <Wbemidl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <accctrl.h>
#include <aclapi.h>
#include <sddl.h>
#include <tlhelp32.h>
#include <winternl.h>
#include <shlwapi.h>
#include <tchar.h>
#include <strsafe.h>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "shlwapi.lib")

std::string g_scriptDir;
bool is64Bit = false;
std::string target;

std::string GetSystem32Path() {
    char szPath[MAX_PATH];

    if (GetSystemDirectoryA(szPath, MAX_PATH)) {
        return std::string(szPath);
    }

    return "";
}

std::vector<std::string> reliedFileItem;

std::vector<std::string> reliedDirItem;

void pause() { system("pause"); }

bool FileExists(const std::string& path) {
    DWORD attr = GetFileAttributesA(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

bool DirExists(const std::string& path) {
    DWORD attr = GetFileAttributesA(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
}

void checkReliedItem() {
    bool terminateProgram = false;
    for (auto& path : reliedFileItem) {
        if (!FileExists(path)) {
            std::cerr << "错误：找不到必需的文件 " << path << std::endl;
            terminateProgram = true;
        }
    }

    for (auto& path : reliedDirItem) {
        if (!DirExists(path)) {
            std::cerr << "错误：找不到必需的目录 " << path << std::endl;
            terminateProgram = true;
        }
    }

    if (terminateProgram) {
        std::clog << "程序将退出。\n";
        pause();
        std::terminate();
    }

}

void checkIf64Bit() {
    SYSTEM_INFO nativeSI;
    GetNativeSystemInfo(&nativeSI);

    switch (nativeSI.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64:
        is64Bit = true;
        break;

    case PROCESSOR_ARCHITECTURE_INTEL:
        is64Bit = false;
        break;

    case PROCESSOR_ARCHITECTURE_ARM:
        is64Bit = false;
        break;

    case PROCESSOR_ARCHITECTURE_ARM64:
        is64Bit = true;
        break;

    case PROCESSOR_ARCHITECTURE_IA64:
        is64Bit = true;
        break;

    default:
        printf("Unknown (0x%x)\n", nativeSI.wProcessorArchitecture);
    }

    if (is64Bit) target = "C:\\Program Files\\Windows Media Player";
    else target = "C:\\Program Files (x86)\\Windows Media Player";

}

// 递归复制目录
bool CopyDirectory(const std::string& src, const std::string& dst) {
    if (!DirExists(src)) return false;
    if (!CreateDirectoryA(dst.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        return false;

    std::string searchPath = src + "\\*.*";
    WIN32_FIND_DATAA ffd;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE)
        return false;

    do {
        if (strcmp(ffd.cFileName, ".") == 0 || strcmp(ffd.cFileName, "..") == 0)
            continue;
        std::string srcFile = src + "\\" + ffd.cFileName;
        std::string dstFile = dst + "\\" + ffd.cFileName;
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!CopyDirectory(srcFile, dstFile))
                return false;
        }
        else {
            if (!CopyFileA(srcFile.c_str(), dstFile.c_str(), FALSE))
                return false;
        }
    } while (FindNextFileA(hFind, &ffd) != 0);
    FindClose(hFind);
    return true;
}

// 获取当前用户的 SID（字符串形式）
std::string GetCurrentUserSid() {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        return "";

    DWORD size = 0;
    GetTokenInformation(hToken, TokenUser, nullptr, 0, &size);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        CloseHandle(hToken);
        return "";
    }
    std::vector<BYTE> buffer(size);
    PTOKEN_USER pTokenUser = reinterpret_cast<PTOKEN_USER>(buffer.data());
    if (!GetTokenInformation(hToken, TokenUser, pTokenUser, size, &size)) {
        CloseHandle(hToken);
        return "";
    }
    CloseHandle(hToken);

    LPSTR sidStr = nullptr;
    if (!ConvertSidToStringSidA(pTokenUser->User.Sid, &sidStr))
        return "";
    std::string result(sidStr);
    LocalFree(sidStr);
    return result;
}

bool GetCurrentUserSid(PSID* ppSid) {
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        return false;
    }

    DWORD dwSize = 0;
    GetTokenInformation(hToken, TokenUser, nullptr, 0, &dwSize);

    PTOKEN_USER pTokenUser = (PTOKEN_USER)malloc(dwSize);
    if (!pTokenUser) {
        CloseHandle(hToken);
        return false;
    }

    BOOL result = GetTokenInformation(hToken, TokenUser,
        pTokenUser, dwSize, &dwSize);
    if (result) {
        // 复制SID
        DWORD sidSize = GetLengthSid(pTokenUser->User.Sid);
        *ppSid = malloc(sidSize);
        CopySid(sidSize, *ppSid, pTokenUser->User.Sid);
    }

    free(pTokenUser);
    CloseHandle(hToken);
    return result == TRUE;
}

bool IsWMPInstalled() {
    HRESULT hr;
    IWbemLocator* pLoc = NULL;
    IWbemServices* pSvc = NULL;
    IEnumWbemClassObject* pEnumerator = NULL;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) return false;

    hr = CoInitializeSecurity(NULL, -1, NULL, NULL,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL, EOAC_NONE, NULL);

    hr = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
        IID_IWbemLocator, (LPVOID*)&pLoc);
    if (FAILED(hr)) {
        CoUninitialize();
        return false;
    }

    hr = pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), NULL, NULL, 0, 0, 0, 0, &pSvc);
    if (FAILED(hr)) {
        pLoc->Release();
        CoUninitialize();
        return false;
    }

    hr = CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
        NULL, RPC_C_AUTHN_LEVEL_CALL,
        RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);

// 查询 Windows Media Player 可选功能
    hr = pSvc->ExecQuery(bstr_t("WQL"),
        bstr_t("SELECT * FROM Win32_OptionalFeature WHERE Name = 'WindowsMediaPlayer'"),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        NULL, &pEnumerator);

    bool isEnabled = false;
    if (SUCCEEDED(hr)) {
        IWbemClassObject* pclsObj = NULL;
        ULONG uReturn = 0;

        if (pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn) == S_OK) {
            VARIANT vtProp;
            VariantInit(&vtProp);

            // 获取 InstallState 属性
            if (SUCCEEDED(pclsObj->Get(L"InstallState", 0, &vtProp, 0, 0))) {
                // 1 = Enabled, 2 = Disabled, 3 = Absent
                isEnabled = (vtProp.ulVal == 1);
                VariantClear(&vtProp);
            }
            pclsObj->Release();
        }
        pEnumerator->Release();
    }

    pSvc->Release();
    pLoc->Release();
    CoUninitialize();

    return isEnabled;
}

BOOL EnableShutdownPrivilege(DWORD DesiredAccess) {
    HANDLE hToken;
    TOKEN_PRIVILEGES tp;
    LUID luid;

    // 1. 打开当前进程的访问令牌
    if (!OpenProcessToken(GetCurrentProcess(),
        DesiredAccess,
        &hToken)) {
        printf("OpenProcessToken 失败，错误码: %lu\n", GetLastError());
        return FALSE;
    }

    // 2. 获取关机特权的 LUID
    if (!LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &luid)) {
        printf("LookupPrivilegeValue 失败，错误码: %lu\n", GetLastError());
        CloseHandle(hToken);
        return FALSE;
    }

    // 3. 设置特权结构
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;  // 启用特权

    // 4. 调整令牌特权
    if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL)) {
        printf("AdjustTokenPrivileges 失败，错误码: %lu\n", GetLastError());
        CloseHandle(hToken);
        return FALSE;
    }

    // 5. 检查是否成功启用了特权
    if (GetLastError() == ERROR_NOT_ALL_ASSIGNED) {
        printf("警告：未获得所有特权，可能权限不足\n");
        CloseHandle(hToken);
        return FALSE;
    }

    CloseHandle(hToken);
    return TRUE;
}

std::string GetLastErrorStr() {
    DWORD err = GetLastError();
    if (err == 0) return "";
    LPSTR buf = nullptr;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&buf, 0, nullptr);
    std::string result(buf ? buf : "");
    LocalFree(buf);
    return result;
}

BOOL DeleteKeyRecursively(HKEY hKeyRoot, LPCTSTR lpszSubKey) {
    HKEY hKey;
    LONG lResult = RegOpenKeyEx(hKeyRoot, lpszSubKey, 0, KEY_READ | KEY_WRITE, &hKey);
    if (lResult != ERROR_SUCCESS)
        return FALSE;

    // 枚举并删除所有子项
    TCHAR szSubKeyName[256];
    DWORD dwSize = 256;
    while (RegEnumKeyEx(hKey, 0, szSubKeyName, &dwSize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
        DeleteKeyRecursively(hKey, szSubKeyName);
        dwSize = 256;
    }

    RegCloseKey(hKey);
    return RegDeleteKey(hKeyRoot, lpszSubKey) == ERROR_SUCCESS;
}

void removeInstalledVersion() {
    std::cout << "正在删除版本校验配置...";
    HKEY hKeyRemove = HKEY_LOCAL_MACHINE;
    const wchar_t* lpStrA = L"SOFTWARE\\Wow6432Node\\Microsoft\\MediaPlayer\\Setup\\Installed Versions";
    BOOL resultRemove = 0;
    resultRemove = DeleteKeyRecursively(hKeyRemove, lpStrA);
    
    const wchar_t* lpStrB = L"SOFTWARE\\Microsoft\\MediaPlayer\\Setup\\Installed Versions";
    resultRemove = DeleteKeyRecursively(hKeyRemove, lpStrB);
    std::cout << "\n";
}

void fixToStartMenu() {
    std::cout << "复制到开始菜单...";
    std::string usernameStr;
    std::string dstLnk;
    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    if (hNtdll) {
        typedef LONG(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
        RtlGetVersionPtr RtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hNtdll, "RtlGetVersion");
        if (RtlGetVersion) {
            RTL_OSVERSIONINFOW osvi = { sizeof(osvi) };
            LONG result = RtlGetVersion(&osvi);  // 调用函数填充数据
            if (result == 0) {  // 0 表示成功
                if (osvi.dwMajorVersion == 6 && osvi.dwMinorVersion == 0) {  // Windows Vista
                    // 获取用户名环境变量

                    const int usernameSize = 32768;
                    char* username = new char[usernameSize];
                    DWORD usernameLen = GetEnvironmentVariableA("USERNAME", username, usernameSize);

                    if (usernameLen == 0) {
                        std::cerr << "\n错误：无法正确调用GetEnvironmentVariable函数。请手动输入用户名：";
                        do {
                            std::cin >> usernameStr;
                        } while ((usernameStr.empty() || usernameStr.length() >= usernameSize)
                            && std::cout << "\n错误：用户名应该不为空且长度小于" << usernameSize && std::cout << "\n重新输入用户名：");
                    }
                    else {
                        usernameStr = username;
                    }

                    // 构建完整路径
                    dstLnk = "C:\\Users\\";
                    dstLnk += usernameStr;
                    dstLnk += "\\AppData\\Roaming\\Microsoft\\Internet Explorer\\Quick Launch";

                    delete[] username;
                }
                else {
                    char startMenuPath[MAX_PATH];
                    SHGetFolderPathA(nullptr, CSIDL_COMMON_PROGRAMS, nullptr, 0, startMenuPath);
                    dstLnk = std::string(startMenuPath);
                }
            }
        }

        dstLnk += "\\Windows Media Player.lnk";
        // 复制快捷方式到开始菜单

        std::string srcLnk;
        //std::string dstLnk = std::string(startMenuPath) + "\\Windows Media Player.lnk";
        if (is64Bit) {
            srcLnk = g_scriptDir + "\\Windows Media Player.lnk";
        }
        else {
            srcLnk = g_scriptDir + "\\x86\\Windows Media Player.lnk";
        }
        if (FileExists(srcLnk)) {
            if (CopyFileA(srcLnk.c_str(), dstLnk.c_str(), FALSE))
                std::cout << "快捷方式复制成功。\n";
            else
                std::cerr << "快捷方式复制失败: " << GetLastErrorStr() << std::endl;
        }
        else {
            std::cerr << "找不到快捷方式文件。\n";
        }
    }
}

int main(int argc, char** argv) {
	SetConsoleOutputCP(CP_ACP);
	SetConsoleCP(CP_ACP);

    if (argc != 2 || std::string(argv[1]) != "--using-ti-mode") {
        MessageBoxA(NULL, "程序必须使用配套的 launch.bat 进行启动，否则无法写入 System32 文件夹的配置。",
            "错误", MB_ICONERROR | MB_OK);
        exit(1);
    }

    std::cout << "本程序将安装 Windows Media Player 11.\n";

    // 设置脚本路径
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    g_scriptDir = path;
    size_t pos = g_scriptDir.find_last_of("\\");
    if (pos != std::string::npos)
        g_scriptDir = g_scriptDir.substr(0, pos);

    reliedFileItem = {
    GetSystem32Path() + "\\reg.exe",
    is64Bit ? g_scriptDir + "\\SetOpeningMethod_x64.reg" : g_scriptDir + "\\SetOpeningMethod_x86.reg"
    };

    reliedDirItem= {
    is64Bit ? g_scriptDir + "\\Program Files" : g_scriptDir + "\\Program Files (x86)",
    g_scriptDir + "\\Program Files (x86)",
    g_scriptDir + "\\Windows"
    };

    checkIf64Bit();

    checkReliedItem();
    
    std::string choice = "\0";

    // 检查系统盘符
    char* sysDrive = getenv("SystemDrive");
    if (sysDrive == NULL) {
        std::cout << "警告：无法检查系统盘符。请确认系统盘为 C:，然后按 ENTER 键继续。";
    }
    else {
        std::cout << "请按 ENTER 键以开始安装...";
    }
    (void)std::getline(std::cin, choice);

    if (!IsWMPInstalled()) {
        std::cout << "注意：原版 Windows Media Player 未安装。程序需要原版 WMP 安装。可能要求重新启动操作系统。请保存您的工作。\n";
        std::cout << "请按 ENTER 键以继续...";
        (void)std::getline(std::cin, choice);
        system("DISM /online /enable-feature /featurename:WindowsMediaPlayer");
        std::cout << "\n";
        std::cout << "正在重新启动操作系统...";
        BOOL res = EnableShutdownPrivilege(TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY);
        if (res) {
            ExitWindowsEx(EWX_REBOOT, SHTDN_REASON_MAJOR_APPLICATION | SHTDN_REASON_MINOR_HOTFIX_UNINSTALL);
            return 0;
        }
        else {
            std::cout << "\n错误：您可能没有关闭计算机的特权。错误代码：";
            std::cout << GetLastError() << std::endl;
        }
        pause();
        return 2;
    }

    std::cout << "正在复制文件...\n";

    std::string targetString;
    if (is64Bit) {
        targetString = "C:\\Program Files\\Windows Media Player";
        std::cout << "从"<<reliedDirItem[0]<<" 复制到 " + targetString + " ...";
        CopyDirectory(reliedDirItem[0], targetString);
        std::cout << "\n";

        targetString = "C:\\Program Files (x86)\\Windows Media Player";
        std::cout << "从" << reliedDirItem[1] << " 复制到 " + targetString + " ...";
        CopyDirectory(reliedDirItem[1], targetString);
        std::cout << "\n";
    }
    else {
        const std::string targetString = "C:\\Program Files\\Windows Media Player";
        std::cout << "从" << reliedDirItem[1] << " 复制到 " + targetString + " ...";
        CopyDirectory(reliedDirItem[1], targetString);
        std::cout << "\n";
    }

    targetString = "C:\\Windows";
    std::cout << "从" << reliedDirItem[2] << " 复制到 " + targetString + " ...";
    CopyDirectory(reliedDirItem[2], targetString);
    std::cout << "\n";

    removeInstalledVersion();
    fixToStartMenu();

    std::cout << "安装已完成。";
    pause();

    return 0;

}