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
#pragma comment(lib, "version.lib")

std::string g_scriptDir;
bool is64Bit = false;
std::string target;
bool skipReinstall = false;

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

std::string tolower(std::string str) {
    for (char& ch : str) {
        ch = tolower(ch);
    }
    return str;
}

INT getWindowsVersion() {
    wchar_t systemPath[MAX_PATH];
    GetSystemDirectoryW(systemPath, MAX_PATH);
    std::wstring filePath = std::wstring(systemPath) + L"\\ntoskrnl.exe";

    DWORD verSize = GetFileVersionInfoSizeW(filePath.c_str(), nullptr );
    if (verSize == 0) return false;

    std::vector<BYTE> verData(verSize);
    if (!GetFileVersionInfoW(filePath.c_str(), 0, verSize, verData.data()))
        return false;

    VS_FIXEDFILEINFO* pFixedInfo = nullptr;
    UINT dwLen = 0;
    if (!VerQueryValueW(verData.data(), L"\\", (LPVOID*)&pFixedInfo, &dwLen))
        return false;

    DWORD major = HIWORD(pFixedInfo->dwFileVersionMS);
    DWORD minor = LOWORD(pFixedInfo->dwFileVersionMS);

    // Windows Vista: WMP 11 original, version 6.0
    if (major < 6 ||
        major == 6 && minor == 0) {
        MessageBoxA(GetConsoleWindow(), "程序无法在您的操作系统上运行。\n您的系统已有官方支持的 Windows Media Player 版本，无需使用本工具。",
            "错误", MB_ICONERROR | MB_OK);
        exit(2);
    }
    else if (major > 6||(major==6&&minor>=2)) {
        skipReinstall = true;
    }
}

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

    case PROCESSOR_ARCHITECTURE_IA64:
        is64Bit = true;
        break;

    default:
        printf("Unknown (0x%x)\n", nativeSI.wProcessorArchitecture);
    }

    if (is64Bit) target = "C:\\Program Files\\Windows Media Player";
    else target = "C:\\Program Files (x86)\\Windows Media Player";

}

// 辅助函数：检查路径是否为目录（如果存在）
bool IsDirectory(const std::string& path) {
    DWORD attrs = GetFileAttributesA(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool CopyDirectory(const std::string& src, const std::string& dst, bool overwrite) {
    // 1. 检查源目录是否存在
    if (!DirExists(src)) return false;

    // 2. 创建目标目录（处理已存在的情况）
    if (!CreateDirectoryA(dst.c_str(), nullptr)) {
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) {
            return false;
        }
        // 如果目标已存在，检查是否为目录
        if (!IsDirectory(dst)) {
            return false;  // 目标路径存在但不是目录
        }
    }

    // 3. 遍历源目录
    std::string searchPath = src + "\\*.*";
    WIN32_FIND_DATAA ffd;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) {
        return false;
    }

    bool success = true;
    do {
        // 跳过 . 和 ..
        if (strcmp(ffd.cFileName, ".") == 0 || strcmp(ffd.cFileName, "..") == 0)
            continue;

        std::string srcFile = src + "\\" + ffd.cFileName;
        std::string dstFile = dst + "\\" + ffd.cFileName;

        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // 递归复制子目录（保持覆盖标志不变）
            if (!CopyDirectory(srcFile, dstFile, overwrite)) {
                success = false;
                break;
            }
        }
        else {
            // 复制文件：overwrite = true 时覆盖，false 时不覆盖
            // CopyFileA 的第三个参数 bFailIfExists: TRUE=存在则失败, FALSE=覆盖
            BOOL bFailIfExists = overwrite ? FALSE : TRUE;
            if (!CopyFileA(srcFile.c_str(), dstFile.c_str(), bFailIfExists)) {
                // 如果因为文件存在而失败，且 overwrite=false，这不是错误
                if (!(!overwrite && GetLastError() == ERROR_FILE_EXISTS)) {
                    success = false;
                    break;
                }
            }
        }
    } while (FindNextFileA(hFind, &ffd) != 0);

    // 4. 检查遍历是否因错误结束
    DWORD findError = GetLastError();
    if (findError != ERROR_NO_MORE_FILES) {
        success = false;
    }

    FindClose(hFind);
    return success;
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

std::vector<std::string> GetFilesNonRecursive(std::string searchPath) {
    std::vector<std::string> dllFiles;

    WIN32_FIND_DATAA ffd;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &ffd);

    if (hFind == INVALID_HANDLE_VALUE) {
        return dllFiles;  // 返回空向量
    }

    do {
        // 跳过目录和特殊目录项
        if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            dllFiles.push_back(ffd.cFileName);
        }
    } while (FindNextFileA(hFind, &ffd) != 0);

    FindClose(hFind);
    return dllFiles;
}

bool DeleteDirectoryRecursive(const std::string& path) {
    // 路径需要以双NULL结尾
    std::string searchPath = path;
    searchPath += "\\*.*";
    searchPath += '\0';  // 第一个结尾符

    // 构造 SHFILEOPSTRUCTA
    SHFILEOPSTRUCTA shfo = { 0 };
    shfo.wFunc = FO_DELETE;
    shfo.pFrom = searchPath.c_str();
    shfo.pTo = nullptr;
    shfo.fFlags = FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI;

    // 执行删除
    int result = SHFileOperationA(&shfo);

    // 删除目录本身
    //RemoveDirectoryA(path.c_str());

    return (result == 0);
}

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_ACP);
    SetConsoleCP(CP_ACP);

    if (argc != 2 || std::string(argv[1]) != "--using-ti-mode") {
        MessageBoxA(GetConsoleWindow(), "程序必须使用配套的 launch.bat 进行启动，否则无法写入 System32 文件夹的配置。",
            "错误", MB_ICONERROR | MB_OK);
        exit(1);
    }

    getWindowsVersion();

    std::cout << "本程序将安装 Windows Media Player 11.\n";

    // 设置脚本路径
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    g_scriptDir = path;
    size_t pos = g_scriptDir.find_last_of("\\");
    if (pos != std::string::npos)
        g_scriptDir = g_scriptDir.substr(0, pos);

    // ★★★ 关键修复1：先检测系统位数 ★★★
    checkIf64Bit();

    // ★★★ 再填充依赖项（此时 is64Bit 已正确设置）★★★
    reliedFileItem = {
        GetSystem32Path() + "\\reg.exe",
        is64Bit ? g_scriptDir + "\\SetOpeningMethod_x64.reg" : g_scriptDir + "\\SetOpeningMethod_x86.reg"
    };

    // 根据系统位数选择源目录
    if (is64Bit) {
        reliedDirItem = {
            g_scriptDir + "\\x64\\Program Files",      // 64位程序文件
            g_scriptDir + "\\x86\\Program Files",      // 32位程序文件
            g_scriptDir + "\\x86\\Windows"             // System32 文件
        };
    }
    else {
        reliedDirItem = {
            g_scriptDir + "\\x86\\Program Files",      // 32位程序文件
            g_scriptDir + "\\x86\\Program Files",      // 同上（占位）
            g_scriptDir + "\\x86\\Windows"             // System32 文件
        };
    }

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


    std::cout << "正在解除 DLL 注册...";
    std::vector<std::string> targetA = GetFilesNonRecursive("C:\\Program Files\\Windows Media Player\\*.*");
    std::vector<std::string> targetB = GetFilesNonRecursive("C:\\Program Files (x86)\\Windows Media Player\\*.*");
    for (auto& str : targetA) {
        if (tolower(str) != "qasf.dll") system(("regsvr32 /s \"C:\\Program Files\\Windows Media Player\\" + str + "\"").c_str());
    }
    for (auto& str : targetB) {
        if (tolower(str) != "qasf.dll") system(("regsvr32 /s \"C:\\Program Files (x86)\\Windows Media Player\\" + str + "\"").c_str());
    }

    std::cout << "\n";

    std::cout << "\n正在清除不需要的文件...";
    DeleteDirectoryRecursive("C:\\Program Files\\Windows Media Player");
    DeleteDirectoryRecursive("C:\\Program Files (x86)\\Windows Media Player");

            system("DISM /online /enable-feature /featurename:MediaPlayback /All /NoRestart");

    std::cout << "\n正在复制文件...\n";

    std::string targetString;
    if (is64Bit) {
        targetString = "C:\\Program Files\\Windows Media Player";
        std::cout << "从 " << reliedDirItem[0] << " 复制到 " + targetString + " ...";
        CopyDirectory(reliedDirItem[0], targetString, true);
        std::cout << "\n";

        targetString = "C:\\Program Files (x86)\\Windows Media Player";
        std::cout << "从 " << reliedDirItem[1] << " 复制到 " + targetString + " ...";
        CopyDirectory(reliedDirItem[1], targetString, true);
        std::cout << "\n";
    }
    else {
        targetString = "C:\\Program Files\\Windows Media Player";
        std::cout << "从 " << reliedDirItem[0] << " 复制到 " + targetString + " ...";
        CopyDirectory(reliedDirItem[0], targetString, true);
        std::cout << "\n";
    }

    std::string sourceSystem32 = reliedDirItem[2] + "\\System32";
    targetString = "C:\\Windows\\System32";
    std::cout << "从 " << sourceSystem32 << " 复制到 " + targetString + " ...";
    CopyDirectory(sourceSystem32, targetString, true);
    std::cout << "\n";

        // ★★★ 关键修复3：使用绝对路径 ★★★
        std::string dllSearchPath = g_scriptDir + "\\x86\\Windows\\System32\\*.dll";
        std::vector<std::string> targetDlls = GetFilesNonRecursive(dllSearchPath);
        std::cout << "正在注册 DLL 文件...";
        for (auto& dll : targetDlls) {
            std::string fullPath = g_scriptDir + "\\x86\\Windows\\System32\\" + dll;
            system(("regsvr32 /s \"" + fullPath + "\"").c_str());
        }
        std::cout << "\n";


    removeInstalledVersion();
    fixToStartMenu();

    std::cout << "安装已完成。";
    pause();

    return 0;
}
