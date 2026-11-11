// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// A clang that stays up (#2071's prototype): it reads clang command lines from its standard input and runs each one as
// the clang executable would, through clang's own Driver, with every cc1 job in this process (as a single-input clang
// runs its cc1 job), and replies with the run's exit status and the streams the request captured. A test that compares
// with thousands of clang runs then pays clang's start (about 10 ms of a 12 ms oracle run: the dynamic loader and the
// libraries' initializers) once, not per run. It is C++ because clang's driver and frontend have no C API.
//
// Protocol, native-endian 32-bit integers, on standard input and output (which the requests' runs never see):
//   request: count, streams, variables; then the working directory, the standard input's path if streams has bit 4,
//            count arguments and variables environment entries (NAME=VALUE), each string its length and its bytes.
//            Streams' bit 1 captures standard output and bit 2 standard error, each discarded without its bit (2:
//            standard error alone, 1: standard output alone, 3: both); with bit 4 the run's standard input reads that
//            file (from the directory), else /dev/null. Argument 0 names the clang, as execvp finds it. The run takes
//            the directory and exactly that environment, as the client's child would. A count of 0, alone, asks for
//            the version this server's clang libraries are (CLANG_VERSION_STRING): status 0 and that one stream.
//   reply:   status (the exit status the clang process would have had), then each captured stream, standard output's
//            first: its length and its bytes.
// A request the server can't take (an input it can't open, a descriptor it can't move) ends its service: the client
// reads the end of its replies.
//
// Usage: clang-server [--version], its standard input and output connecting it to one client (pipes, or one socket, as
// oracle_cache.c's OracleServer has it). It exits at the end of its input.
// tools/clang-server/build.sh builds it against the libraries of the clang it stands for.

#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/DiagnosticIDs.h>
#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Basic/Version.h>
#include <clang/CodeGen/ObjectFilePCHContainerWriter.h>
#include <clang/Driver/Compilation.h>
#include <clang/Driver/Driver.h>
#include <clang/Driver/ToolChain.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/CompilerInvocation.h>
#include <clang/Frontend/TextDiagnosticBuffer.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Frontend/Utils.h>
#include <clang/FrontendTool/Utils.h>
#include <clang/Serialization/ObjectFilePCHContainerReader.h>
#include <clang/Serialization/PCHContainerOperations.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/Timer.h>
#include <llvm/Support/VirtualFileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Host.h>

#include <cstdint>
#include <cstdio>
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

// One length-prefixed string of the protocol.
bool read_string(int descriptor, std::string& text)
{
    uint32_t length = 0;
    bool read = read_all(descriptor, &length, sizeof(length));
    text.assign(read ? length : 0, '\0');
    read = read && read_all(descriptor, text.data(), length);
    return read;
}

// One cc1 job, as clang's cc1_main runs it, but freeing what it made: the driver's -disable-free lets a process that is
// about to exit skip that, and this one doesn't exit. As cc1_main, its compiler instance reads and writes modules wrapped
// in object files (-gmodules' format, "obj"), which without these fails as a fatal error, an abort of the server.
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
    auto containers = std::make_shared<clang::PCHContainerOperations>();
    containers->registerWriter(std::make_unique<clang::ObjectFilePCHContainerWriter>());
    containers->registerReader(std::make_unique<clang::ObjectFilePCHContainerReader>());
    auto instance = std::make_unique<clang::CompilerInstance>(std::make_shared<clang::CompilerInvocation>(), containers);
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

// The clang's path, as its driver's main finds it (GetExecutablePath), whose directory the driver's resource directory
// and tools are found from: with canonical prefixes (the default), the file the clang process would run, which a
// symbolic link on PATH leads to (its /proc/self/exe); with -no-canonical-prefixes, argument 0 as given where that
// exists, else as PATH finds it. Argument 0 alone (execvp's PATH search) would put a clang reached through a link in the
// link's directory: /usr/local/bin/clang's resource directory /usr/local/lib/clang/23 rather than its own.
std::string executable_path(const std::vector<std::string>& request)
{
    bool canonical = true;
    for (size_t i = 1; i < request.size(); i += 1)
    {
        canonical = request[i] == "-canonical-prefixes" ? true : request[i] == "-no-canonical-prefixes" ? false : canonical;
    }
    std::string executable = request[0];
    const bool searched = canonical ? executable.find('/') == std::string::npos : !llvm::sys::fs::exists(executable);
    if (searched)
    {
        llvm::ErrorOr<std::string> found = llvm::sys::findProgramByName(executable);
        if (found)
        {
            executable = *found;
        }
    }
    llvm::SmallString<256> real;
    if (canonical && !llvm::sys::fs::real_path(executable, real))
    {
        executable = std::string(real);
    }
    return executable;
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
    const std::string executable = executable_path(request);
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

int main(int argc, char** argv)
{
    // --version: the clang release these libraries are, which tools/clang-server/build.sh checks.
    if (argc == 2 && std::strcmp(argv[1], "--version") == 0)
    {
        std::printf("%s\n", CLANG_VERSION_STRING);
        return 0;
    }
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmPrinters();
    llvm::InitializeAllAsmParsers();
    // The protocol's own descriptors; the runs' standard input is empty and their outputs are each request's.
    const int requests = dup(STDIN_FILENO);
    const int replies = dup(STDOUT_FILENO);
    const int empty = open("/dev/null", O_RDWR | O_CLOEXEC);
    bool serving = requests >= 0 && replies >= 0 && empty >= 0 && dup2(empty, STDIN_FILENO) >= 0;
    std::vector<std::string> environment;
    while (serving)
    {
        uint32_t header[3] = { 0, 0, 0 };
        serving = read_all(requests, header, sizeof(header));
        const bool capture_output = (header[1] & 1) != 0;
        const bool capture_errors = (header[1] & 2) != 0;
        const bool reads_input = (header[1] & 4) != 0;
        std::string directory;
        std::string input;
        serving = serving && (header[0] == 0 || read_string(requests, directory));
        serving = serving && (header[0] == 0 || !reads_input || read_string(requests, input));
        std::vector<std::string> request;
        for (uint32_t i = 0; serving && i < header[0]; i += 1)
        {
            std::string argument;
            serving = read_string(requests, argument);
            request.push_back(std::move(argument));
        }
        std::vector<std::string> variables;
        for (uint32_t i = 0; serving && i < header[2]; i += 1)
        {
            std::string variable;
            serving = read_string(requests, variable);
            variables.push_back(std::move(variable));
        }
        if (serving && header[0] != 0)
        {
            // The client's environment, exactly: putenv keeps the strings, which `environment` holds until the next
            // request's clearenv has let go of them.
            clearenv();
            environment = std::move(variables);
            for (std::string& variable : environment)
            {
                putenv(variable.data());
            }
            serving = chdir(directory.c_str()) == 0;
        }
        int32_t status = 0;
        std::string captured[2];
        if (serving && header[0] == 0)
        {
            captured[1] = CLANG_VERSION_STRING;
        }
        else if (serving)
        {
            // Each captured stream into a file in memory, the other to /dev/null; the input from its file.
            const int output = capture_output ? memfd_create("clang-server-output", MFD_CLOEXEC) : -1;
            const int errors = capture_errors ? memfd_create("clang-server-errors", MFD_CLOEXEC) : -1;
            const int source = reads_input ? open(input.c_str(), O_RDONLY | O_CLOEXEC) : -1;
            const int saved_output = dup(STDOUT_FILENO);
            const int saved_errors = dup(STDERR_FILENO);
            serving = (!capture_output || output >= 0) && (!capture_errors || errors >= 0) && (!reads_input || source >= 0) && saved_output >= 0 &&
                      saved_errors >= 0 && dup2(capture_output ? output : empty, STDOUT_FILENO) >= 0 &&
                      dup2(capture_errors ? errors : empty, STDERR_FILENO) >= 0 && (!reads_input || dup2(source, STDIN_FILENO) >= 0);
            status = serving ? run_clang(request) : 1;
            llvm::outs().flush();
            llvm::errs().flush();
            serving = serving && dup2(saved_output, STDOUT_FILENO) >= 0 && dup2(saved_errors, STDERR_FILENO) >= 0 && dup2(empty, STDIN_FILENO) >= 0;
            const int captures[2] = { output, errors };
            for (uint32_t c = 0; c < 2; c += 1)
            {
                struct stat size = {};
                serving = serving && (captures[c] < 0 || (fstat(captures[c], &size) == 0 && lseek(captures[c], 0, SEEK_SET) == 0));
                captured[c].resize(serving && captures[c] >= 0 ? static_cast<size_t>(size.st_size) : 0);
                serving = serving && read_all(captures[c], captured[c].data(), captured[c].size());
            }
            const int descriptors[5] = { output, errors, source, saved_output, saved_errors };
            for (int descriptor : descriptors)
            {
                if (descriptor >= 0)
                {
                    close(descriptor);
                }
            }
        }
        const bool streams[2] = { header[0] != 0 && capture_output, header[0] == 0 || capture_errors };
        serving = serving && write_all(replies, &status, sizeof(status));
        for (uint32_t c = 0; c < 2; c += 1)
        {
            const uint32_t length = static_cast<uint32_t>(captured[c].size());
            serving = serving && (!streams[c] || (write_all(replies, &length, sizeof(length)) && write_all(replies, captured[c].data(), captured[c].size())));
        }
    }
    return 0;
}
