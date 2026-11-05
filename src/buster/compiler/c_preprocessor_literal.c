// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Diagnostics for numeric constants refused by #if/#elif's integer reader (#1254). The shared classifier supplies
// malformed-literal errors; only well-formed floating constants get the preprocessor-specific message. Included
// before pp_parse_integer. Valid integers never call this scalar, cold path or pay for a second spelling scan.

#include "c_check_literal_diagnostic.c"

static __attribute__((noinline, cold)) void pp_diagnose_integer(Preprocessor* pp, const uint8_t* spelling, uint32_t length, uint32_t location)
{
    CheckLiteralDiagnostic diagnostic;
    const bool malformed = check_literal_diagnose(spelling, length, pp->options.language.standard >= 2023, &diagnostic);
    if (malformed)
    {
        const uint32_t offset = diagnostic.offset;
        const uint32_t at = pp_spelling_offset_location(pp, location, offset);
        const char* radix = diagnostic.radix == 2 ? "binary" : diagnostic.radix == 8 ? "octal" : "decimal";
        switch (diagnostic.kind)
        {
            case CHECK_LITERAL_DIAGNOSTIC_EXPONENT:
                pp_diagnose(pp, PP_SEVERITY_ERROR, at, "exponent has no digits");
                break;
            case CHECK_LITERAL_DIAGNOSTIC_DIGIT:
                pp_diagnose(pp, PP_SEVERITY_ERROR, at, "invalid digit '%c' in %s constant", diagnostic.character, radix);
                break;
            case CHECK_LITERAL_DIAGNOSTIC_SUFFIX:
                pp_diagnose(pp, PP_SEVERITY_ERROR, at, "invalid suffix '%.*s' on %s constant", (int)(length - offset),
                            (const char*)spelling + offset, diagnostic.floating ? "floating" : "integer");
                break;
            case CHECK_LITERAL_DIAGNOSTIC_HEX_EXPONENT:
            case CHECK_LITERAL_DIAGNOSTIC_HEX_SIGNIFICAND:
                pp_diagnose(pp, PP_SEVERITY_ERROR, at, "hexadecimal floating constant requires %s",
                            diagnostic.kind == CHECK_LITERAL_DIAGNOSTIC_HEX_EXPONENT ? "an exponent" : "a significand");
                break;
            case CHECK_LITERAL_DIAGNOSTIC_SEPARATOR:
                pp_diagnose(pp, PP_SEVERITY_ERROR, at, "digit separator cannot appear at %s of digit sequence", diagnostic.floating ? "end" : "start");
                break;
            case CHECK_LITERAL_DIAGNOSTIC_TOO_LARGE:
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, "integer literal is too large to be represented in any integer type");
                break;
            case CHECK_LITERAL_DIAGNOSTIC_SIZE_T:
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, "'size_t' suffix for literals is a C++23 feature");
                break;
            default:
                break;
        }
    }
    else
    {
        // Keep the reader's own refusal for extensions it cannot evaluate; a valid hex float is floating too.
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, diagnostic.floating ? "floating point literal in preprocessor expression" : "invalid integer constant '%.*s' in preprocessor expression",
                    (int)length, (const char*)spelling);
    }
}
