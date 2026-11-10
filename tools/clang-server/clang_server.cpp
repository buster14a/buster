// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// A clang that stays up (#2071's prototype): it reads clang command lines from its standard input and runs each one as
// the clang executable would, through clang's own Driver, with every cc1 job in this process (as a single-input clang
// runs its cc1 job), and replies with the run's exit status and the stream the request captured. A test that compares
// with thousands of clang runs then pays clang's start (about 10 ms of a 12 ms oracle run: the dynamic loader and the
// libraries' initializers) once, not per run. It is C++ because clang's driver and frontend have no C API.
//
// Protocol, native-endian 32-bit integers, on standard input and output (which the requests' runs never see):
//   request: count, stream (2: capture standard error and discard standard output; 1: the other way), then count
//            arguments, each its length and its bytes; argument 0 names the clang, as execvp finds it. A count of 0
//            asks for the version this server's clang libraries are (CLANG_VERSION_STRING), with status 0.
//   reply:   status (the exit status the clang process would have had), length, the captured bytes.
//
// Usage: clang-server, its standard input and output pipes to one client. It exits at the end of its input. Built by
// tools/clang-server/build.sh against the libraries of the clang it stands for.

#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/DiagnosticIDs.h>
#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Basic/Version.h>
#include <clang/Driver/Compilation.h>
#include <clang/Driver/Driver.h>
#include <clang/Driver/ToolChain.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/CompilerInvocation.h>
#include <clang/Frontend/TextDiagnosticBuffer.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Frontend/Utils.h>
#include <clang/FrontendTool/Utils.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/Timer.h>
#include <llvm/Support/VirtualFileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Host.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
bool read_all(int descriptor, void* data, size_t size)
{
    char* at = static_cast<char*>(data);
    bool complete = true;
    while (complete && size != 0)
    {
        const ssize_t got = read(descriptor, at, size);
        complete = got > 0;
        if (complete)
        {
            at += got;
            size -= static_cast<size_t>(got);
        }
    }
    return complete;
}

bool write_all(int descriptor, const void* data, size_t size)
{
    const char* at = static_cast<const char*>(data);
    bool complete = true;
    while (complete && size != 0)
    {
        const ssize_t put = write(descriptor, at, size);
        complete = put > 0;
        if (complete)
        {
            at += put;
            size -= static_cast<size_t>(put);
        }
    }
    return complete;
}

// One cc1 job, as clang's cc1_main runs it, but freeing what it made: the driver's -disable-free lets a process that is
// about to exit skip that, and this one doesn't exit.
int run_cc1(llvm::SmallVectorImpl<const char*>& argv)
{
    std::vector<const char*> arguments;
    for (size_t i = 2; i < argv.size(); i += 1)
    {
        if (std::strcmp(argv[i], "-disable-free") != 0)
        {
            arguments.push_back(argv[i]);
        }
    }
    llvm::cl::ResetAllOptionOccurrences();
    auto instance = std::make_unique<clang::CompilerInstance>();
    clang::DiagnosticOptions parse_options;
    auto* buffer = new clang::TextDiagnosticBuffer;
    clang::DiagnosticsEngine parse_diagnostics(llvm::makeIntrusiveRefCnt<clang::DiagnosticIDs>(), parse_options, buffer);
    bool success = clang::CompilerInvocation::CreateFromArgs(instance->getInvocation(), arguments, parse_diagnostics, argv[0]);
    instance->createDiagnostics();
    int result = 1;
    if (instance->hasDiagnostics())
    {
        buffer->FlushDiagnostics(instance->getDiagnostics());
        if (success)
        {
            success = clang::ExecuteCompilerInvocation(instance.get());
        }
        result = success ? 0 : 1;
    }
    llvm::TimerGroup::clearAll();
    return result;
}

// One clang command line, as clang's driver main runs it: diagnostics options from the arguments, the driver's
// diagnostics prefixed by the program's name, the compilation built and executed, the first failing command's status.
int run_clang(const std::vector<std::string>& request)
{
    std::vector<const char*> argv;
    for (const std::string& argument : request)
    {
        argv.push_back(argument.c_str());
    }
    std::string executable = request[0];
    if (executable.find('/') == std::string::npos)
    {
        llvm::ErrorOr<std::string> found = llvm::sys::findProgramByName(executable);
        if (found)
        {
            executable = *found;
        }
    }
    std::unique_ptr<clang::DiagnosticOptions> options = clang::CreateAndPopulateDiagOpts(argv);
    auto* printer = new clang::TextDiagnosticPrinter(llvm::errs(), *options);
    printer->setPrefix(std::string(llvm::sys::path::stem(request[0])));
    clang::DiagnosticsEngine diagnostics(llvm::makeIntrusiveRefCnt<clang::DiagnosticIDs>(), *options, printer);
    clang::ProcessWarningOptions(diagnostics, *options, *llvm::vfs::getRealFileSystem(), false);
    clang::driver::Driver driver(executable, llvm::sys::getDefaultTargetTriple(), diagnostics);
    driver.setTargetAndMode(clang::driver::ToolChain::getTargetAndModeFromProgramName(request[0]));
    driver.CC1Main = run_cc1;
    std::unique_ptr<clang::driver::Compilation> compilation(driver.BuildCompilation(argv));
    int result = 1;
    if (compilation && !compilation->containsError())
    {
        llvm::SmallVector<std::pair<int, const clang::driver::Command*>, 4> failing;
        result = driver.ExecuteCompilation(*compilation, failing);
        for (const auto& command : failing)
        {
            if (result == 0)
            {
                result = command.first;
            }
            if (command.first < 0)
            {
                result = 1;
            }
        }
    }
    return result;
}
} // namespace

int main()
{
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmPrinters();
    llvm::InitializeAllAsmParsers();
    // The protocol's own descriptors; the runs' standard input is empty and their outputs are each request's.
    const int requests = dup(STDIN_FILENO);
    const int replies = dup(STDOUT_FILENO);
    const int empty = open("/dev/null", O_RDWR | O_CLOEXEC);
    bool serving = requests >= 0 && replies >= 0 && empty >= 0 && dup2(empty, STDIN_FILENO) >= 0;
    while (serving)
    {
        uint32_t header[2] = { 0, 0 };
        serving = read_all(requests, header, sizeof(header));
        std::vector<std::string> request;
        for (uint32_t i = 0; serving && i < header[0]; i += 1)
        {
            uint32_t length = 0;
            serving = read_all(requests, &length, sizeof(length));
            std::string argument(serving ? length : 0, '\0');
            serving = serving && read_all(requests, argument.data(), length);
            request.push_back(std::move(argument));
        }
        int32_t status = 0;
        std::string captured;
        if (serving && header[0] == 0)
        {
            captured = CLANG_VERSION_STRING;
        }
        else if (serving)
        {
            const int capture = memfd_create("clang-server-capture", MFD_CLOEXEC);
            const int saved_output = dup(STDOUT_FILENO);
            const int saved_errors = dup(STDERR_FILENO);
            const bool errors = header[1] != 1;
            serving = capture >= 0 && saved_output >= 0 && saved_errors >= 0 &&
                      dup2(errors ? empty : capture, STDOUT_FILENO) >= 0 && dup2(errors ? capture : empty, STDERR_FILENO) >= 0;
            status = serving ? run_clang(request) : 1;
            llvm::outs().flush();
            llvm::errs().flush();
            serving = serving && dup2(saved_output, STDOUT_FILENO) >= 0 && dup2(saved_errors, STDERR_FILENO) >= 0;
            struct stat size = {};
            serving = serving && fstat(capture, &size) == 0 && lseek(capture, 0, SEEK_SET) == 0;
            captured.resize(serving ? static_cast<size_t>(size.st_size) : 0);
            serving = serving && read_all(capture, captured.data(), captured.size());
            close(capture);
            close(saved_output);
            close(saved_errors);
        }
        const uint32_t reply[2] = { static_cast<uint32_t>(status), static_cast<uint32_t>(captured.size()) };
        serving = serving && write_all(replies, reply, sizeof(reply)) && write_all(replies, captured.data(), captured.size());
    }
    return 0;
}
