#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace slayerlog
{

class ProcessPipe
{
public:
    ProcessPipe(std::string executable, std::vector<std::string> arguments);
    ~ProcessPipe();

    ProcessPipe(const ProcessPipe&)            = delete;
    ProcessPipe& operator=(const ProcessPipe&) = delete;

    ProcessPipe(ProcessPipe&& other) noexcept;
    ProcessPipe& operator=(ProcessPipe&& other) noexcept;

    std::size_t read_stdout(char* buffer, std::size_t buffer_size, bool& end_of_stream);
    std::size_t read_stderr(char* buffer, std::size_t buffer_size, bool& end_of_stream);
    bool running();
    int wait();
    void terminate();

private:
    void close();

#ifdef _WIN32
    void* _process_handle     = nullptr;
    void* _thread_handle      = nullptr;
    void* _stdout_read_handle = nullptr;
    void* _stderr_read_handle = nullptr;
#else
    int _pid            = -1;
    int _stdout_read_fd = -1;
    int _stderr_read_fd = -1;
#endif
    bool _waited   = false;
    int _exit_code = 0;
};

} // namespace slayerlog
