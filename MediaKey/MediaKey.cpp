#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocketServer.h>
#include <shellapi.h>
#include <algorithm>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "ncrypt.lib")
#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "advapi32.lib")

class MediaControllerApp
{
public:
    MediaControllerApp()
        : m_server(3001)
    {
    }

    int Run()
    {
        if (!AcquireSingleInstance())
        {
            return 0;
        }

        const bool is_background_mode = HasBackgroundFlag();
        if (is_background_mode)
        {
            HideConsoleWindow();
        }

        std::cout << "[START] Media Controller" << std::endl;
        ix::initNetSystem();

        s_instance = this;
        SetConsoleCtrlHandler(ConsoleControlHandler, TRUE);

        EnsureStartupRegistration();

        if (!StartWebSocketServer())
        {
            ix::uninitNetSystem();
            return 1;
        }

        if (!InstallKeyboardHook())
        {
            m_server.stop();
            ix::uninitNetSystem();
            return 1;
        }

        MSG msg = {};
        while (GetMessage(&msg, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        UninstallKeyboardHook();
        m_server.stop();
        ix::uninitNetSystem();
        if (m_instance_mutex != nullptr)
        {
            CloseHandle(m_instance_mutex);
            m_instance_mutex = nullptr;
        }
        return 0;
    }
private:
    static MediaControllerApp* s_instance;
    static constexpr const wchar_t* k_run_key_path = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    static constexpr const wchar_t* k_run_value_name = L"MediaKeyController";

    ix::WebSocketServer m_server;
    std::vector<std::weak_ptr<ix::WebSocket>> m_clients;
    std::mutex m_client_mutex;
    HHOOK m_hook = nullptr;
    HANDLE m_instance_mutex = nullptr;

    bool AcquireSingleInstance()
    {
        m_instance_mutex = CreateMutexW(nullptr, FALSE, L"Local\\MediaKeyControllerInstance");
        if (m_instance_mutex == nullptr)
        {
            return true;
        }

        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            CloseHandle(m_instance_mutex);
            m_instance_mutex = nullptr;
            return false;
        }

        return true;
    }

    static BOOL WINAPI ConsoleControlHandler(DWORD ControlType)
    {
        if ((ControlType == CTRL_C_EVENT || ControlType == CTRL_BREAK_EVENT || ControlType == CTRL_CLOSE_EVENT) &&
            s_instance != nullptr)
        {
            s_instance->RequestStop();
            return TRUE;
        }

        return FALSE;
    }

    static bool HasBackgroundFlag()
    {
        int argument_count = 0;
        LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
        if (arguments == nullptr)
        {
            return false;
        }

        bool has_background_flag = false;
        for (int index = 1; index < argument_count; ++index)
        {
            if (_wcsicmp(arguments[index], L"--background") == 0)
            {
                has_background_flag = true;
                break;
            }
        }

        LocalFree(arguments);
        return has_background_flag;
    }

    static void HideConsoleWindow()
    {
        HWND console_window = GetConsoleWindow();
        if (console_window != nullptr)
        {
            ShowWindow(console_window, SW_HIDE);
        }
    }

    void EnsureStartupRegistration() const
    {
        wchar_t source_executable_path[MAX_PATH] = {};
        const DWORD source_path_length = GetModuleFileNameW(nullptr, source_executable_path, MAX_PATH);
        if (source_path_length == 0 || source_path_length >= MAX_PATH)
        {
            return;
        }

        wchar_t local_app_data[MAX_PATH] = {};
        const DWORD local_app_data_length = GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH);
        if (local_app_data_length == 0 || local_app_data_length >= MAX_PATH)
        {
            return;
        }

        std::wstring startup_directory = local_app_data;
        startup_directory += L"\\MediaKeyController";
        CreateDirectoryW(startup_directory.c_str(), nullptr);

        std::wstring startup_executable_path = startup_directory + L"\\MediaKey.exe";
        CopyFileW(source_executable_path, startup_executable_path.c_str(), FALSE);

        std::wstring source_directory = source_executable_path;
        const size_t separator_index = source_directory.find_last_of(L"\\/");
        if (separator_index != std::wstring::npos)
        {
            source_directory.resize(separator_index);
            const std::wstring source_zlib_path = source_directory + L"\\zlib1.dll";
            const std::wstring target_zlib_path = startup_directory + L"\\zlib1.dll";
            CopyFileW(source_zlib_path.c_str(), target_zlib_path.c_str(), FALSE);
        }

        std::wstring startup_command = L"\"";
        startup_command += startup_executable_path;
        startup_command += L"\" --background";

        HKEY run_key = nullptr;
        const LONG open_result = RegCreateKeyExW(
            HKEY_CURRENT_USER,
            k_run_key_path,
            0,
            nullptr,
            REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE,
            nullptr,
            &run_key,
            nullptr);
        if (open_result != ERROR_SUCCESS)
        {
            return;
        }

        const BYTE* data = reinterpret_cast<const BYTE*>(startup_command.c_str());
        const DWORD data_size = static_cast<DWORD>((startup_command.size() + 1) * sizeof(wchar_t));
        RegSetValueExW(run_key, k_run_value_name, 0, REG_SZ, data, data_size);
        RegCloseKey(run_key);

        wchar_t app_data[MAX_PATH] = {};
        const DWORD app_data_length = GetEnvironmentVariableW(L"APPDATA", app_data, MAX_PATH);
        if (app_data_length == 0 || app_data_length >= MAX_PATH)
        {
            return;
        }

        std::wstring startup_script_path = app_data;
        startup_script_path += L"\\Microsoft\\Windows\\Start Menu\\Programs\\Startup\\MediaKeyController.cmd";

        const std::wstring script_content =
            L"@echo off\r\n"
            L"start \"\" \"" + startup_executable_path + L"\" --background\r\n";

        HANDLE script_file = CreateFileW(
            startup_script_path.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (script_file == INVALID_HANDLE_VALUE)
        {
            return;
        }

        int required_size = WideCharToMultiByte(CP_ACP, 0, script_content.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (required_size <= 0)
        {
            CloseHandle(script_file);
            return;
        }

        std::vector<char> script_bytes(static_cast<size_t>(required_size));
        const int converted_size = WideCharToMultiByte(
            CP_ACP,
            0,
            script_content.c_str(),
            -1,
            script_bytes.data(),
            required_size,
            nullptr,
            nullptr);
        if (converted_size <= 1)
        {
            CloseHandle(script_file);
            return;
        }

        DWORD written = 0;
        WriteFile(script_file, script_bytes.data(), static_cast<DWORD>(converted_size - 1), &written, nullptr);
        CloseHandle(script_file);
    }

    static LRESULT CALLBACK KeyboardProc(int NCode, WPARAM WParam, LPARAM LParam)
    {
        if (NCode == HC_ACTION && (WParam == WM_KEYDOWN || WParam == WM_SYSKEYDOWN) && s_instance != nullptr)
        {
            const auto* keyboard_data = reinterpret_cast<KBDLLHOOKSTRUCT*>(LParam);
            if (keyboard_data != nullptr)
            {
                s_instance->HandleMediaKey(static_cast<int>(keyboard_data->vkCode));
            }
        }

        return CallNextHookEx(nullptr, NCode, WParam, LParam);
    }

    bool StartWebSocketServer()
    {
        m_server.setOnConnectionCallback(
            [this](std::weak_ptr<ix::WebSocket> WebSocket, std::shared_ptr<ix::ConnectionState>)
            {
                auto socket = WebSocket.lock();
                if (!socket)
                {
                    return;
                }

                {
                    std::lock_guard<std::mutex> lock_guard(m_client_mutex);
                    m_clients.push_back(socket);
                }

                std::cout << "[WS] client connected" << std::endl;

                socket->setOnMessageCallback(
                    [this](const ix::WebSocketMessagePtr& Message)
                    {
                        if (Message->type == ix::WebSocketMessageType::Close)
                        {
                            CleanupClients();
                        }
                    });
            });

        for (int attempt = 1; attempt <= 30; ++attempt)
        {
            const auto listen_result = m_server.listen();
            if (listen_result.first)
            {
                m_server.start();
                std::cout << "[WS] server started on ws://127.0.0.1:3001" << std::endl;
                return true;
            }

            std::cout << "[ERR] websocket listen failed (" << attempt << "/30): " << listen_result.second << std::endl;
            Sleep(1000);
        }

        return false;
    }

    bool InstallKeyboardHook()
    {
        m_hook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(nullptr), 0);
        if (!m_hook)
        {
            std::cout << "[ERR] hook install failed" << std::endl;
            return false;
        }

        std::cout << "[OK] Hook installed" << std::endl;
        return true;
    }

    void UninstallKeyboardHook()
    {
        if (m_hook)
        {
            UnhookWindowsHookEx(m_hook);
            m_hook = nullptr;
        }
    }

    void RequestStop() const
    {
        PostQuitMessage(0);
    }

    void HandleMediaKey(int VirtualKey)
    {
        std::string command;

        switch (VirtualKey)
        {
        case VK_MEDIA_NEXT_TRACK:
            command = "forward";
            break;
        case VK_MEDIA_PREV_TRACK:
            command = "back";
            break;
        case VK_MEDIA_PLAY_PAUSE:
            command = "playpause";
            break;
        default:
            break;
        }

        if (!command.empty())
        {
            std::cout << "[KEY] " << command << std::endl;
            Broadcast(command);
        }
    }

    void CleanupClients()
    {
        std::lock_guard<std::mutex> lock_guard(m_client_mutex);
        m_clients.erase(
            std::remove_if(
                m_clients.begin(),
                m_clients.end(),
                [](const std::weak_ptr<ix::WebSocket>& Client)
                {
                    const auto socket = Client.lock();
                    return !socket || socket->getReadyState() != ix::ReadyState::Open;
                }),
            m_clients.end());
    }

    void Broadcast(const std::string& Message)
    {
        std::vector<std::shared_ptr<ix::WebSocket>> active_clients;

        {
            std::lock_guard<std::mutex> lock_guard(m_client_mutex);
            for (auto iterator = m_clients.begin(); iterator != m_clients.end();)
            {
                auto socket = iterator->lock();
                if (!socket || socket->getReadyState() != ix::ReadyState::Open)
                {
                    iterator = m_clients.erase(iterator);
                    continue;
                }

                active_clients.push_back(socket);
                ++iterator;
            }
        }

        for (const auto& client : active_clients)
        {
            client->send(Message);
        }

        std::cout << "[SEND] " << Message << std::endl;
    }
};

MediaControllerApp* MediaControllerApp::s_instance = nullptr;

int main()
{
    MediaControllerApp app;
    return app.Run();
}
