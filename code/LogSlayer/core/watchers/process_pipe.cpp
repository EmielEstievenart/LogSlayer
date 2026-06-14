#include "process_pipe.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

#ifdef _WIN32
#    define NOMINMAX
#    include <windows.h>
#else
#    include <cerrno>
#    include <csignal>
#    include <fcntl.h>
#    if __has_include(<sys/types.h>)
#        include <sys/types.h>
#    endif
#    if __has_include(<sys/wait.h>)
#        include <sys/wait.h>
#    endif
#    include <unistd.h>
#endif

namespace slayerlog
{

namespace
{

#ifdef _WIN32

using Handle = HANDLE;

std::string quote_windows_argument(const std::string& argument)
{
    if (argument.find_first_of(" \t\"") == std::string::npos)
    {
        return argument;
    }

    std::string quoted;
    quoted.push_back('"');
    std::size_t backslash_count = 0;
    for (const char character : argument)
    {
        if (character == '\\')
        {
            ++backslash_count;
            continue;
        }

        if (character == '"')
        {
            quoted.append(backslash_count * 2 + 1, '\\');
            quoted.push_back('"');
            backslash_count = 0;
            continue;
        }

        quoted.append(backslash_count, '\\');
        backslash_count = 0;
        quoted.push_back(character);
    }

    quoted.append(backslash_count * 2, '\\');
    quoted.push_back('"');
    return quoted;
}

std::string build_command_line(const std::string& executable, const std::vector<std::string>& arguments)
{
    std::string command_line = quote_windows_argument(executable);
    for (const std::string& argument : arguments)
    {
        command_line.push_back(' ');
        command_line.append(quote_windows_argument(argument));
    }

    return command_line;
}

Handle invalid_handle() noexcept
{
    return nullptr;
}

void close_handle_if_valid(Handle& handle) noexcept
{
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(handle);
    }

    handle = nullptr;
}

std::size_t read_from_handle(Handle handle, char* buffer, std::size_t buffer_size, bool& end_of_stream)
{
    end_of_stream         = false;
    DWORD bytes_available = 0;
    if (PeekNamedPipe(handle, nullptr, 0, nullptr, &bytes_available, nullptr) == 0)
    {
        const DWORD error = GetLastError();
        if (error == ERROR_BROKEN_PIPE)
        {
            end_of_stream = true;
            return 0;
        }

        throw std::runtime_error("PeekNamedPipe failed");
    }

    if (bytes_available == 0)
    {
        return 0;
    }

    const DWORD bytes_to_read = static_cast<DWORD>(std::min<std::size_t>(buffer_size, bytes_available));
    DWORD bytes_read          = 0;
    if (ReadFile(handle, buffer, bytes_to_read, &bytes_read, nullptr) == 0)
    {
        const DWORD error = GetLastError();
        if (error == ERROR_BROKEN_PIPE)
        {
            end_of_stream = true;
            return 0;
        }

        throw std::runtime_error("ReadFile failed");
    }

    return static_cast<std::size_t>(bytes_read);
}

#else

int invalid_handle() noexcept
{
    return -1;
}

void close_handle_if_valid(int& handle) noexcept
{
    if (handle >= 0)
    {
        ::close(handle);
    }

    handle = -1;
}

std::size_t read_from_handle(int handle, char* buffer, std::size_t buffer_size, bool& end_of_stream)
{
    end_of_stream            = false;
    const ssize_t bytes_read = ::read(handle, buffer, buffer_size);
    if (bytes_read == 0)
    {
        end_of_stream = true;
        return 0;
    }

    if (bytes_read < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        {
            return 0;
        }

        throw std::runtime_error("read failed");
    }

    return static_cast<std::size_t>(bytes_read);
}

void set_nonblocking(int handle)
{
    const int flags = ::fcntl(handle, F_GETFL, 0);
    if (flags < 0)
    {
        throw std::runtime_error("fcntl(F_GETFL) failed");
    }

    if (::fcntl(handle, F_SETFL, flags | O_NONBLOCK) != 0)
    {
        throw std::runtime_error("fcntl(F_SETFL) failed");
    }
}

#endif

} // namespace

ProcessPipe::ProcessPipe(std::string executable, std::vector<std::string> arguments)
{
#ifdef _WIN32
    SECURITY_ATTRIBUTES security_attributes {};
    security_attributes.nLength        = sizeof(security_attributes);
    security_attributes.bInheritHandle = TRUE;

    HANDLE stdout_write = invalid_handle();
    HANDLE stderr_write = invalid_handle();
    HANDLE stdin_read   = invalid_handle();
    HANDLE stdin_write  = invalid_handle();

    if (CreatePipe(&_stdout_read_handle, &stdout_write, &security_attributes, 0) == 0 || SetHandleInformation(_stdout_read_handle, HANDLE_FLAG_INHERIT, 0) == 0 ||
        CreatePipe(&_stderr_read_handle, &stderr_write, &security_attributes, 0) == 0 || SetHandleInformation(_stderr_read_handle, HANDLE_FLAG_INHERIT, 0) == 0 || CreatePipe(&stdin_read, &stdin_write, &security_attributes, 0) == 0)
    {
        close_handle_if_valid(stdout_write);
        close_handle_if_valid(stderr_write);
        close_handle_if_valid(stdin_read);
        close_handle_if_valid(stdin_write);
        close();
        throw std::runtime_error("Failed to create process pipes");
    }

    SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA startup_info {};
    startup_info.cb         = sizeof(startup_info);
    startup_info.dwFlags    = STARTF_USESTDHANDLES;
    startup_info.hStdInput  = stdin_read;
    startup_info.hStdOutput = stdout_write;
    startup_info.hStdError  = stderr_write;

    PROCESS_INFORMATION process_info {};
    std::string command_line = build_command_line(executable, arguments);
    if (CreateProcessA(nullptr, command_line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup_info, &process_info) == 0)
    {
        close_handle_if_valid(stdout_write);
        close_handle_if_valid(stderr_write);
        close_handle_if_valid(stdin_read);
        close_handle_if_valid(stdin_write);
        close();
        throw std::runtime_error("Failed to start process: " + executable);
    }

    _process_handle = process_info.hProcess;
    _thread_handle  = process_info.hThread;
    close_handle_if_valid(stdout_write);
    close_handle_if_valid(stderr_write);
    close_handle_if_valid(stdin_read);
    close_handle_if_valid(stdin_write);
#else
    int stdout_pipe[2] {-1, -1};
    int stderr_pipe[2] {-1, -1};
    if (::pipe(stdout_pipe) != 0 || ::pipe(stderr_pipe) != 0)
    {
        if (stdout_pipe[0] >= 0)
        {
            ::close(stdout_pipe[0]);
            ::close(stdout_pipe[1]);
        }
        if (stderr_pipe[0] >= 0)
        {
            ::close(stderr_pipe[0]);
            ::close(stderr_pipe[1]);
        }
        throw std::runtime_error("Failed to create process pipes");
    }

    _pid = ::fork();
    if (_pid < 0)
    {
        ::close(stdout_pipe[0]);
        ::close(stdout_pipe[1]);
        ::close(stderr_pipe[0]);
        ::close(stderr_pipe[1]);
        throw std::runtime_error("Failed to fork process");
    }

    if (_pid == 0)
    {
        ::dup2(stdout_pipe[1], STDOUT_FILENO);
        ::dup2(stderr_pipe[1], STDERR_FILENO);
        ::close(stdout_pipe[0]);
        ::close(stdout_pipe[1]);
        ::close(stderr_pipe[0]);
        ::close(stderr_pipe[1]);

        std::vector<char*> argv;
        argv.reserve(arguments.size() + 2);
        argv.push_back(executable.data());
        for (std::string& argument : arguments)
        {
            argv.push_back(argument.data());
        }
        argv.push_back(nullptr);

        ::execvp(executable.c_str(), argv.data());
        _exit(127);
    }

    _stdout_read_fd = stdout_pipe[0];
    _stderr_read_fd = stderr_pipe[0];
    ::close(stdout_pipe[1]);
    ::close(stderr_pipe[1]);
    set_nonblocking(_stdout_read_fd);
    set_nonblocking(_stderr_read_fd);
#endif
}

ProcessPipe::~ProcessPipe()
{
    close();
}

ProcessPipe::ProcessPipe(ProcessPipe&& other) noexcept
{
    *this = std::move(other);
}

ProcessPipe& ProcessPipe::operator=(ProcessPipe&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    close();
#ifdef _WIN32
    _process_handle           = other._process_handle;
    _thread_handle            = other._thread_handle;
    _stdout_read_handle       = other._stdout_read_handle;
    _stderr_read_handle       = other._stderr_read_handle;
    other._process_handle     = nullptr;
    other._thread_handle      = nullptr;
    other._stdout_read_handle = nullptr;
    other._stderr_read_handle = nullptr;
#else
    _pid                  = other._pid;
    _stdout_read_fd       = other._stdout_read_fd;
    _stderr_read_fd       = other._stderr_read_fd;
    other._pid            = -1;
    other._stdout_read_fd = -1;
    other._stderr_read_fd = -1;
#endif
    _waited    = other._waited;
    _exit_code = other._exit_code;
    return *this;
}

std::size_t ProcessPipe::read_stdout(char* buffer, std::size_t buffer_size, bool& end_of_stream)
{
#ifdef _WIN32
    return read_from_handle(static_cast<HANDLE>(_stdout_read_handle), buffer, buffer_size, end_of_stream);
#else
    return read_from_handle(_stdout_read_fd, buffer, buffer_size, end_of_stream);
#endif
}

std::size_t ProcessPipe::read_stderr(char* buffer, std::size_t buffer_size, bool& end_of_stream)
{
#ifdef _WIN32
    return read_from_handle(static_cast<HANDLE>(_stderr_read_handle), buffer, buffer_size, end_of_stream);
#else
    return read_from_handle(_stderr_read_fd, buffer, buffer_size, end_of_stream);
#endif
}

bool ProcessPipe::running()
{
    if (_waited)
    {
        return false;
    }

#ifdef _WIN32
    const DWORD wait_result = WaitForSingleObject(static_cast<HANDLE>(_process_handle), 0);
    if (wait_result == WAIT_TIMEOUT)
    {
        return true;
    }

    DWORD exit_code = 0;
    GetExitCodeProcess(static_cast<HANDLE>(_process_handle), &exit_code);
    _exit_code = static_cast<int>(exit_code);
    _waited    = true;
    return false;
#else
    int status         = 0;
    const pid_t result = ::waitpid(_pid, &status, WNOHANG);
    if (result == 0)
    {
        return true;
    }

    if (result == _pid)
    {
        _waited    = true;
        _exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        return false;
    }

    return false;
#endif
}

int ProcessPipe::wait()
{
    if (_waited)
    {
        return _exit_code;
    }

#ifdef _WIN32
    WaitForSingleObject(static_cast<HANDLE>(_process_handle), INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(static_cast<HANDLE>(_process_handle), &exit_code);
    _exit_code = static_cast<int>(exit_code);
#else
    int status = 0;
    ::waitpid(_pid, &status, 0);
    _exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
    _waited = true;
    return _exit_code;
}

void ProcessPipe::terminate()
{
    if (_waited)
    {
        return;
    }

#ifdef _WIN32
    if (_process_handle != nullptr)
    {
        TerminateProcess(static_cast<HANDLE>(_process_handle), 1);
    }
#else
    if (_pid > 0)
    {
        ::kill(_pid, SIGTERM);
    }
#endif
}

void ProcessPipe::close()
{
#ifdef _WIN32
    HANDLE stdout_handle = static_cast<HANDLE>(_stdout_read_handle);
    close_handle_if_valid(stdout_handle);
    _stdout_read_handle = nullptr;

    HANDLE stderr_handle = static_cast<HANDLE>(_stderr_read_handle);
    close_handle_if_valid(stderr_handle);
    _stderr_read_handle = nullptr;

    HANDLE thread_handle = static_cast<HANDLE>(_thread_handle);
    close_handle_if_valid(thread_handle);
    _thread_handle = nullptr;

    if (_process_handle != nullptr)
    {
        if (!_waited)
        {
            terminate();
            wait();
        }

        HANDLE process_handle = static_cast<HANDLE>(_process_handle);
        close_handle_if_valid(process_handle);
        _process_handle = nullptr;
    }
#else
    close_handle_if_valid(_stdout_read_fd);
    close_handle_if_valid(_stderr_read_fd);
    if (_pid > 0)
    {
        if (!_waited)
        {
            terminate();
            wait();
        }
        _pid = -1;
    }
#endif
}

} // namespace slayerlog
