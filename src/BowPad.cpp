// This file is part of BowPad.
//
// Copyright (C) 2013-2018, 2020-2025 - Stefan Kueng
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// See <http://www.gnu.org/licenses/> for a copy of the full license text
//

#include "stdafx.h"
#include "BowPad.h"
#include "MainWindow.h"
#include "CmdLineParser.h"
#include "KeyboardShortcutHandler.h"
#include "BaseDialog.h"
#include "AppUtils.h"
#include "SmartHandle.h"
#include "PathUtils.h"
#include "StringUtils.h"
#include "ProgressDlg.h"
#include "DownloadFile.h"
#include "SysInfo.h"
#include "OnOutOfScope.h"
#include "version.h"
#include "CommandHandler.h"
#include "JumpListHelpers.h"
#include "ResString.h"
#include "PackageRegistration.h"
#include <wrl.h>
using Microsoft::WRL::ComPtr;

#pragma comment(lib, "Rpcrt4.lib")

HINSTANCE   g_hInst;
HINSTANCE   g_hRes;
bool        firstInstance = false;
IUIImagePtr g_emptyIcon;

static void RegisterWin11ContextMenu(bool doRegister)
{
}

static void LoadLanguage(HINSTANCE hInstance)
{
    // load the language dll if required
    std::wstring lang = CIniSettings::Instance().GetString(L"UI", L"language", L"");
    if (!lang.empty())
    {
        std::wstring langDllPath = CAppUtils::GetDataPath(hInstance);
        langDllPath += L"\\BowPad_";
        langDllPath += lang;
        langDllPath += L".lang";
        if (!CAppUtils::HasSameMajorVersion(langDllPath))
        {
            // the language dll does not exist or does not match:
            // try downloading the new language dll right now
            // so the user gets the selected language immediately after
            // updating BowPad
            std::wstring sLangURL = CStringUtils::Format(L"https://github.com/stefankueng/BowPad/raw/%d.%d.%d/Languages/%s/BowPad_%s.lang", BP_VERMAJOR, BP_VERMINOR, BP_VERMICRO, LANGPLAT, lang.c_str());

            // note: text below is in English and not translatable because
            // we try to download the translation file here, so there's no
            // point in having this translated...
            CProgressDlg progDlg;
            progDlg.SetTitle(L"BowPad Update");
            progDlg.SetLine(1, L"Downloading BowPad Language file...");
            progDlg.ResetTimer();
            progDlg.SetTime();
            progDlg.ShowModal(nullptr);

            CDownloadFile fileDownloader(L"BowPad", &progDlg);

            if (!fileDownloader.DownloadFile(sLangURL, langDllPath))
            {
                DeleteFile(langDllPath.c_str());
            }
        }
        if (CAppUtils::HasSameMajorVersion(langDllPath))
        {
            g_hRes = LoadLibraryEx(langDllPath.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES | LOAD_LIBRARY_AS_IMAGE_RESOURCE | LOAD_LIBRARY_AS_DATAFILE);
            if (g_hRes == nullptr)
                g_hRes = g_hInst;
        }
    }
}

static void SetIcon()
{
}

static void SetUserStringKey(LPCWSTR keyName, LPCWSTR subKeyName, const std::wstring& keyValue)
{
}

static void RegisterContextMenu(bool bAdd)
{
}

static void SetJumplist(LPCTSTR appID)
{
}

static void ForwardToOtherInstance(HWND hBowPadWnd, LPCTSTR lpCmdLine, CCmdLineParser& parser)
{
    if (::IsIconic(hBowPadWnd))
        ::ShowWindow(hBowPadWnd, SW_RESTORE);
    // if the window is not yet visible, we wait a little bit
    // and we don't make the window visible here: the message we send
    // to open the file might get handled before the RegisterAndCreateWindow
    // in MainWindow.cpp hasn't finished yet. Just let that function make
    // the window visible in the right position.
    if (IsWindowVisible(hBowPadWnd))
    {
        // check if it's on the current virtual desktop
        IVirtualDesktopManager* pvdm = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_VirtualDesktopManager,
                                       nullptr, CLSCTX_ALL, IID_PPV_ARGS(&pvdm))))
        {
            BOOL isCurrent = FALSE;
            if (pvdm && SUCCEEDED(pvdm->IsWindowOnCurrentVirtualDesktop(hBowPadWnd, &isCurrent)))
            {
                if (!isCurrent)
                {
                    pvdm->Release();
                    pvdm = nullptr;

                    // move it to the current virtual desktop
                    SendMessage(hBowPadWnd, WM_MOVETODESKTOP, 0, reinterpret_cast<LPARAM>(GetForegroundWindow()));
                }
                else
                    ::SetForegroundWindow(hBowPadWnd);
            }
        }
        else
            ::SetForegroundWindow(hBowPadWnd);
        if (pvdm)
            pvdm->Release();
    }
    else
        Sleep(500);

    size_t cmdLineLen = wcslen(lpCmdLine);
    if (cmdLineLen)
    {
        COPYDATASTRUCT cds = {};
        cds.dwData         = CD_COMMAND_LINE;
        if (!parser.HasVal(L"path") && !parser.HasKey(L"wait"))
        {
            // create our own command line with all paths converted to long/full paths
            // since the CWD of the other instance is most likely different
            int                nArgs;
            std::wstring       sCmdLine;
            const std::wstring commandLine = GetCommandLineW();
            LPWSTR*            szArgList   = CommandLineToArgvW(commandLine.c_str(), &nArgs);
            if (szArgList)
            {
                OnOutOfScope(LocalFree(szArgList););
                bool bOmitNext = false;
                for (int i = 0; i < nArgs; i++)
                {
                    if (bOmitNext)
                    {
                        bOmitNext = false;
                        continue;
                    }
                    if ((szArgList[i][0] != '/') && (szArgList[i][0] != '-'))
                    {
                        std::wstring path = szArgList[i];
                        CPathUtils::NormalizeFolderSeparators(path);
                        path = CPathUtils::GetLongPathname(path);
                        if (!PathFileExists(path.c_str()))
                        {
                            auto pathPos = commandLine.find(szArgList[i]);
                            if (pathPos != std::wstring::npos)
                            {
                                auto tempPath = commandLine.substr(pathPos);
                                if (PathFileExists(tempPath.c_str()))
                                {
                                    CPathUtils::NormalizeFolderSeparators(tempPath);
                                    path = CPathUtils::GetLongPathname(tempPath);
                                    sCmdLine += L"\"" + path + L"\" ";
                                    break;
                                }
                            }
                        }
                        sCmdLine += L"\"" + path + L"\" ";
                    }
                    else
                    {
                        if (wcscmp(&szArgList[i][1], L"z") == 0)
                            bOmitNext = true;
                        else
                        {
                            sCmdLine += szArgList[i];
                            sCmdLine += L" ";
                        }
                    }
                }
            }
            auto ownCmdLine = std::make_unique<wchar_t[]>(sCmdLine.size() + 2);
            wcscpy_s(ownCmdLine.get(), sCmdLine.size() + 2, sCmdLine.c_str());
            cds.cbData = static_cast<DWORD>((sCmdLine.size() + 1) * sizeof(wchar_t));
            cds.lpData = ownCmdLine.get();
            SendMessage(hBowPadWnd, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds));
        }
        else
        {
            cds.cbData = static_cast<DWORD>((cmdLineLen + 1) * sizeof(wchar_t));
            cds.lpData = static_cast<PVOID>(const_cast<LPWSTR>(lpCmdLine));
            SendMessage(hBowPadWnd, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds));
        }
    }
}

static HWND FindAndWaitForBowPad()
{
    // don't start another instance: reuse the existing one
    // find the window of the existing instance
    ResString    clsResName(g_hInst, IDC_BOWPAD);
    std::wstring clsName    = static_cast<LPCWSTR>(clsResName) + CAppUtils::GetSessionID();

    HWND         hBowPadWnd = ::FindWindow(clsName.c_str(), nullptr);
    // if we don't have a window yet, wait a little while
    // to give the other process time to create the window
    for (int i = 0; !hBowPadWnd && i < 20; i++)
    {
        Sleep(100);
        hBowPadWnd = ::FindWindow(clsName.c_str(), nullptr);
    }
    // also wait for the window to become visible first
    for (int i = 0; !IsWindowVisible(hBowPadWnd) && i < 20; i++)
    {
        Sleep(100);
    }
    return hBowPadWnd;
}

static void ShowBowPadCommandLineHelp()
{
    std::wstring sMessage = CStringUtils::Format(L"BowPad version %d.%d.%d.%d\nusage: BowPad.exe /path:\"PATH\" [/line:number] [/multiple]\nor: BowPad.exe PATH [/line:number] [/multiple]\nwith /multiple forcing BowPad to open a new instance even if there's already an instance running.", BP_VERMAJOR, BP_VERMINOR, BP_VERMICRO, BP_VERBUILD);
    MessageBox(nullptr, sMessage.c_str(), L"BowPad", MB_ICONINFORMATION);
}

static void ParseCommandLine(CCmdLineParser& parser, CMainWindow* mainWindow)
{
    if (parser.HasVal(L"path"))
    {
        size_t line = static_cast<size_t>(-1);
        if (parser.HasVal(L"line"))
        {
            line = parser.GetLongLongVal(L"line") - 1LL;
        }
        mainWindow->SetFileToOpen(parser.GetVal(L"path"), line);
        if (parser.HasKey(L"elevate") && parser.HasKey(L"savepath"))
        {
            mainWindow->SetElevatedSave(parser.GetVal(L"path"), parser.GetVal(L"savepath"), static_cast<long>(line));
            mainWindow->SetFileOpenMRU(false);
            firstInstance = false;
        }
        if (parser.HasKey(L"tabmove") && parser.HasKey(L"savepath"))
        {
            std::wstring title = parser.HasVal(L"title") ? parser.GetVal(L"title") : L"";
            mainWindow->SetTabMove(parser.GetVal(L"path"), parser.GetVal(L"savepath"), !!parser.HasKey(L"modified"), static_cast<long>(line), title, parser.GetVal(L"posinfopath"));
            mainWindow->SetFileOpenMRU(false);
        }
    }
    else
    {
        // find out if there are paths specified without the key/value pair syntax
        int                nArgs;

        const std::wstring commandLine = GetCommandLineW();
        LPWSTR*            szArgList   = CommandLineToArgvW(commandLine.c_str(), &nArgs);
        if (szArgList)
        {
            OnOutOfScope(LocalFree(szArgList););
            size_t line = static_cast<size_t>(-1);
            if (parser.HasVal(L"line"))
            {
                line = parser.GetLongLongVal(L"line") - 1LL;
            }

            bool bOmitNext = false;
            for (int i = 1; i < nArgs; i++)
            {
                if (bOmitNext)
                {
                    bOmitNext = false;
                    continue;
                }
                if ((szArgList[i][0] != '/') && (szArgList[i][0] != '-'))
                {
                    auto pathPos = commandLine.find(szArgList[i]);
                    if (pathPos != std::wstring::npos)
                    {
                        auto tempPath = commandLine.substr(pathPos);
                        if (PathFileExists(tempPath.c_str()))
                        {
                            CPathUtils::NormalizeFolderSeparators(tempPath);
                            auto path = CPathUtils::GetLongPathname(tempPath);
                            mainWindow->SetFileToOpen(path, line);
                            break;
                        }
                    }

                    std::wstring path = szArgList[i];
                    CPathUtils::NormalizeFolderSeparators(path);
                    path = CPathUtils::GetLongPathname(path);
                    if (!PathFileExists(path.c_str()))
                    {
                        pathPos = commandLine.find(szArgList[i]);
                        if (pathPos != std::wstring::npos)
                        {
                            auto tempPath = commandLine.substr(pathPos);
                            if (PathFileExists(tempPath.c_str()))
                            {
                                CPathUtils::NormalizeFolderSeparators(tempPath);
                                path = CPathUtils::GetLongPathname(tempPath);
                                mainWindow->SetFileToOpen(path, line);
                                break;
                            }
                        }
                    }
                    mainWindow->SetFileToOpen(path, line);
                }
                else
                {
                    if (wcscmp(&szArgList[i][1], L"z") == 0)
                        bOmitNext = true;
                }
            }
        }
    }
}

int bpMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPCTSTR lpCmdLine, int nCmdShow, bool bAlreadyRunning, HANDLE& hAppMutex)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(nCmdShow);

    SetDllDirectory(L"");
    HRESULT hr = CoInitialize(nullptr);
    if (FAILED(hr))
        return -1;
    OnOutOfScope(CoUninitialize(););

    auto parser = std::make_unique<CCmdLineParser>(lpCmdLine);
    if (parser->HasKey(L"?") || parser->HasKey(L"help"))
    {
        ShowBowPadCommandLineHelp();
        return 0;
    }
    if (parser->HasKey(L"register"))
    {
        RegisterContextMenu(true);
        return 0;
    }
    if ((parser->HasKey(L"unregister")) || (parser->HasKey(L"deregister")))
    {
        RegisterContextMenu(false);
        return 0;
    }
    if (parser->HasKey(L"registerwin11contextmenu"))
    {
        RegisterWin11ContextMenu(true);
        return 0;
    }
    if (parser->HasKey(L"unregisterwin11contextmenu"))
    {
        RegisterWin11ContextMenu(false);
        return 0;
    }

    bool isAdminMode = SysInfo::Instance().IsUACEnabled() && SysInfo::Instance().IsElevated();
    if (parser->HasKey(L"admin") && !isAdminMode)
    {
        std::wstring     modPath    = CPathUtils::GetModulePath();
        SHELLEXECUTEINFO shExecInfo = {sizeof(SHELLEXECUTEINFO)};

        shExecInfo.hwnd             = nullptr;
        shExecInfo.lpVerb           = L"runas";
        shExecInfo.lpFile           = modPath.c_str();
        shExecInfo.lpParameters     = parser->getCmdLine();
        shExecInfo.nShow            = SW_NORMAL;

        if (ShellExecuteEx(&shExecInfo))
            return 0;
    }
    if (bAlreadyRunning && !parser->HasKey(L"multiple") && !parser->HasKey(L"wait"))
    {
        HWND hBowPadWnd = FindAndWaitForBowPad();
        if (hBowPadWnd)
        {
            ForwardToOtherInstance(hBowPadWnd, lpCmdLine, *parser);
            return 0;
        }
    }
    if (parser->HasKey(L"wait"))
    {
        // create a new command line, but without
        // the /wait switch, instead add the /newifmissing switch
        // and of course add the name of the mutex to use for
        // synchronisation
        std::wstring newCommandLine = parser->getCmdLine();
        auto         lowerCmdLine   = CStringUtils::to_lower(newCommandLine);
        auto         pos            = lowerCmdLine.find(L"/wait");
        if (pos != std::wstring::npos)
            newCommandLine.erase(pos, 5);
        else
        {
            pos = lowerCmdLine.find(L"-wait");
            if (pos != std::wstring::npos)
                newCommandLine.erase(pos, 5);
        }

        UUID uuid;
        UuidCreate(&uuid);
        wchar_t* wszUuid = nullptr;
        UuidToString(&uuid, reinterpret_cast<RPC_WSTR*>(&wszUuid));
        std::wstring sUuid = wszUuid;
        RpcStringFree(reinterpret_cast<RPC_WSTR*>(&wszUuid));
        std::wstring sMutex = L"BowPad_" + sUuid;
        newCommandLine += (L" /waitMutex:" + sMutex);
        std::wstring modPath = CPathUtils::GetModulePath();
        newCommandLine       = L"\"" + modPath + L"\" " + newCommandLine;

        if (!bAlreadyRunning)
        {
            // here we start a new BP instance that does the real work
            auto hMutex = CreateMutex(nullptr, false, sMutex.c_str());
            OnOutOfScope(CloseHandle(hMutex));
            // close this apps single-instance mutex since this process is used only for waiting
            CloseHandle(hAppMutex);
            hAppMutex = nullptr;

            STARTUPINFO startupInfo{};
            startupInfo.cb = sizeof(STARTUPINFO);
            PROCESS_INFORMATION processInfo{};
            if (CreateProcess(modPath.c_str(), newCommandLine.data(), nullptr, nullptr, false, 0, nullptr, nullptr, &startupInfo, &processInfo))
            {
                OnOutOfScope(CloseHandle(processInfo.hThread);
                             CloseHandle(processInfo.hProcess););
                // wait for the new BP instance to start up
                if (WaitForInputIdle(processInfo.hProcess, 10000) == 0)
                {
                    // wait for the document to be opened
                    auto evt = CreateEvent(nullptr, TRUE, FALSE, (sMutex + L"_").c_str());
                    OnOutOfScope(CloseHandle(evt));
                    if (WaitForSingleObject(evt, 10000) == WAIT_OBJECT_0)
                    {
                        // now wait until the tab is closed again or the process exits
                        HANDLE handles[2] = {hMutex, processInfo.hProcess};
                        WaitForMultipleObjects(2, handles, FALSE, INFINITE);
                        return 0;
                    }
                }
            }
        }
        else
        {
            HWND hBowPadWnd = FindAndWaitForBowPad();
            if (hBowPadWnd)
            {
                auto hMutex = CreateMutex(nullptr, false, sMutex.c_str());
                OnOutOfScope(CloseHandle(hMutex));
                // there's already a BP process running: tell it to open the file
                // and then we use this process to just wait for the file to be
                // closed
                ForwardToOtherInstance(hBowPadWnd, newCommandLine.c_str(), *parser);
                DWORD pid = 0;
                GetWindowThreadProcessId(hBowPadWnd, &pid);
                auto hProcess = OpenProcess(SYNCHRONIZE, false, pid);
                OnOutOfScope(CloseHandle(hProcess));
                // now wait until the tab is closed again or the process exits
                HANDLE handles[2] = {hMutex, hProcess};
                WaitForMultipleObjects(2, handles, FALSE, INFINITE);
                return 0;
            }
        }
    }

    auto appID = isAdminMode ? APP_ID_ELEVATED : APP_ID;
    SetAppID(appID);
    SetJumplist(appID);

    CIniSettings::Instance().SetIniPath(CAppUtils::GetDataPath() + L"\\settings");
    LoadLanguage(hInstance);

    SetIcon();

    CAppUtils::CreateImage(MAKEINTRESOURCE(IDB_EMPTY), g_emptyIcon);

    if (parser->HasKey(L"elevate") && parser->HasVal(L"savepath") && parser->HasVal(L"path"))
    {
        // note: MoveFileEx won't work for some reason, but
        // writing to the target file will.
        BOOL ret = FALSE;
        {
            CAutoFile hRead = CreateFile(parser->GetVal(L"path"), GENERIC_READ, FILE_SHARE_DELETE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (hRead)
            {
                CAutoFile hWrite = CreateFile(parser->GetVal(L"savepath"), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hWrite)
                {
                    DWORD dwRead  = 0;
                    DWORD dwWrite = 0;
                    BYTE  buffer[4096]{};

                    ret = ReadFile(hRead, buffer, sizeof(buffer) - 1, &dwRead, nullptr);
                    while (ret && dwRead)
                    {
                        WriteFile(hWrite, buffer, dwRead, &dwWrite, nullptr);
                        ret = ReadFile(hRead, buffer, sizeof(buffer) - 1, &dwRead, nullptr);
                    }
                }
            }
        }
        DeleteFile(parser->GetVal(L"path"));
        return ret;
    }

    auto mainWindow = std::make_unique<CMainWindow>(g_hRes);

    if (!mainWindow->RegisterAndCreateWindow())
        return -1;

    ParseCommandLine(*parser, mainWindow.get());

    // Don't need the parser any more so don't keep it around taking up space.
    parser.reset();

    // force CWD to the install path to avoid the CWD being locked:
    // if BowPad is started from another path (e.g. via double click on a text file in
    // explorer), the CWD is the directory of that file. As long as BowPad runs with the CWD
    // set to that dir, that dir can't be removed or renamed due to the lock.
    ::SetCurrentDirectory(CPathUtils::GetModuleDir().c_str());

    std::wstring params = L" /multiple";
    if (isAdminMode)
        params += L" /admin";
    auto         modulePath = CPathUtils::GetLongPathname(CPathUtils::GetModulePath());
    std::wstring sIconPath  = CStringUtils::Format(L"%s,-%d", modulePath.c_str(), IDI_BOWPAD);
    if (modulePath.find(' ') != std::wstring::npos)
        modulePath = L"\"" + modulePath + L"\"";
    SetRelaunchCommand(*mainWindow.get(), appID, (modulePath + params).c_str(), L"BowPad", sIconPath.c_str());

    // Main message loop:
    MSG   msg = {nullptr};
    auto& kb  = CKeyboardShortcutHandler::Instance();
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!kb.TranslateAccelerator(*mainWindow.get(), msg.message, msg.wParam, msg.lParam) &&
            !CDialog::IsDialogMessage(&msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    CCommandHandler::ShutDown();
    g_emptyIcon = nullptr;
    Animator::ShutDown();
    return static_cast<int>(msg.wParam);
}

int APIENTRY wWinMain(_In_ HINSTANCE     hInstance,
                      _In_opt_ HINSTANCE hPrevInstance,
                      _In_ LPTSTR        lpCmdLine,
                      _In_ int           nCmdShow)
{
    g_hInst                = hInstance;
    g_hRes                 = hInstance;

    const std::wstring sID = L"BowPad_EFA99E4D-68EB-4EFA-B8CE-4F5B41104540_" + CAppUtils::GetSessionID();
    ::SetLastError(NO_ERROR); // Don't do any work between these 3 statements to spoil the error code.
    HANDLE hAppMutex   = ::CreateMutex(nullptr, false, sID.c_str());
    DWORD  mutexStatus = GetLastError();
    OnOutOfScope(if (hAppMutex) CloseHandle(hAppMutex););
    bool bAlreadyRunning = (mutexStatus == ERROR_ALREADY_EXISTS || mutexStatus == ERROR_ACCESS_DENIED);
    firstInstance        = !bAlreadyRunning;

    auto mainResult      = bpMain(hInstance, hPrevInstance, lpCmdLine, nCmdShow, bAlreadyRunning, hAppMutex);

    Scintilla_ReleaseResources();

    // Be careful shutting down Scintilla's resources here if any
    // global static objects contain things like CScintillaWnd as members
    // as they will destruct AFTER WinMain. That won't be a good thing
    // if we've released Scintilla resources IN WinMain.

    return mainResult;
}
