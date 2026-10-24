// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// External test oracle for preserved pragma tokens. Clang's ordinary -dump-tokens action omits
// pragmas. This external adapter requires Clang's C++ frontend API.
// This adapter uses the installed Clang API, not copied Clang implementation. See docs/preprocessor-fuzz.md.
#include <clang/Basic/Version.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Lex/Pragma.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/raw_ostream.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#if CLANG_VERSION_MAJOR != 23
#error "The preprocessor fuzz oracle requires Clang 23."
#endif

class OmpPragmaTokens : public clang::PragmaHandler
{
public:
    OmpPragmaTokens() : clang::PragmaHandler("omp") {}

    void HandlePragma(clang::Preprocessor& pp, clang::PragmaIntroducer, clang::Token&) override
    {
        // The directive's name was read without expansion. The operands after it expand in the live preprocessor,
        // so counters, disabled macros, nested _Pragma and subsequent definitions keep their actual source order.
        clang::Token token;
        do
        {
            pp.Lex(token);
            if (!token.isOneOf(clang::tok::eod, clang::tok::eof))
            {
                llvm::errs() << "BUSTER_PPF_PRAGMA ";
                pp.DumpToken(token, true);
                llvm::errs() << '\n';
            }
        }
        while (!token.isOneOf(clang::tok::eod, clang::tok::eof));
    }
};

class UnknownPragmaTokens : public clang::PragmaHandler
{
public:
    void HandlePragma(clang::Preprocessor& pp, clang::PragmaIntroducer, clang::Token& token) override
    {
        // Clang's -E printer preserves unknown pragmas without macro replacement. The first token is already read;
        // keep it too, since _Pragma may produce a malformed literal as the directive's first token.
        while (!token.isOneOf(clang::tok::eod, clang::tok::eof))
        {
            llvm::errs() << "BUSTER_PPF_PRAGMA ";
            pp.DumpToken(token, true);
            llvm::errs() << '\n';
            pp.LexUnexpandedToken(token);
        }
    }
};

class PragmaTokenAction : public clang::PreprocessorFrontendAction
{
    void ExecuteAction() override
    {
        clang::Preprocessor& pp = getCompilerInstance().getPreprocessor();
        OmpPragmaTokens handler;
        UnknownPragmaTokens unknown;
        UnknownPragmaTokens gcc;
        UnknownPragmaTokens clang;
        pp.AddPragmaHandler(&handler);
        pp.AddPragmaHandler(&unknown);
        pp.AddPragmaHandler("GCC", &gcc);
        pp.AddPragmaHandler("clang", &clang);
        pp.EnterMainSourceFile();
        clang::Token token;
        do
        {
            pp.Lex(token);
            pp.DumpToken(token, true);
            llvm::errs() << '\n';
        }
        while (!token.is(clang::tok::eof));
        pp.RemovePragmaHandler(&handler);
        pp.RemovePragmaHandler(&unknown);
        pp.RemovePragmaHandler("GCC", &gcc);
        pp.RemovePragmaHandler("clang", &clang);
    }
};

int main(int argc, char** argv)
{
    const bool version = argc == 2 && std::strcmp(argv[1], "--version") == 0;
    bool valid = argc == 2 && (version || std::strcmp(argv[1], "-std=c17") == 0 || std::strcmp(argv[1], "-std=c23") == 0);
    std::string source;
    int byte = EOF;
    if (valid && !version)
    {
        while ((byte = std::getchar()) != EOF && source.size() < (1u << 20))
        {
            source.push_back((char)byte);
        }
        valid = byte == EOF && !std::ferror(stdin);
    }
    if (valid)
    {
        std::printf("clang version %s\n", CLANG_VERSION_STRING);
        if (!version)
        {
            const std::vector<std::string> flags = {
                "--no-default-config",
                "-E",
                "-P",
                "-undef",
                "-nostdinc",
                argv[1],
                "-w",
                "-ferror-limit=0",
                "-fno-color-diagnostics",
                "-fno-caret-diagnostics",
                "-x",
                "c",
            };
            valid = clang::tooling::runToolOnCodeWithArgs(std::make_unique<PragmaTokenAction>(), source, flags, "<stdin>", "clang");
        }
    }
    return valid && !std::ferror(stdout) ? 0 : 1;
}
