// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// -no-pie's real links (#1503), included by c_driver_compatibility_test.c. Each program is linked by the compiler with
// -no-pie and by clang with GNU ld (-fuse-ld=bfd -no-pie), both run, and both read back as `readelf -hlrd` shows them,
// which must agree: ET_EXEC with PT_INTERP, the lowest PT_LOAD at the same fixed base, the same DT_NEEDED libraries, no
// DF_1_PIE, DT_RELR or RELATIVE relocation, and the same symbols relocated by the loader, each by the same class: a GOT
// slot's (GLOB_DAT, or JUMP_SLOT for a call through GNU ld's lazy .plt, where the compiler's stub reads a GLOB_DAT
// slot), R_X86_64_64's, TPOFF64's or a copy's. The programs:
// - a C program that calls libc's functions, takes puts's address, reads stdout and environ through the GOT, sets errno
//   and uses its own thread-locals and an imported one, of a library the test builds with clang (-shared -fPIC);
// - the mixed path: a C main with a -fno-pie object of clang's, whose PC32s to stdout and environ take copies and whose
//   weak function, which nothing defines, is tested by a `mov $f` and called, both at address 0, as GNU ld leaves it.
// Then the link kind's options in clang's order (its getPIE and getStaticPIE): the last of -pie and -no-pie, -static
// over either, and -no-pie with -static-pie refused, naming both.

#define BUSTER_CC_NO_PIE_TEXT 4096
#define BUSTER_CC_NO_PIE_LENGTH(array) (sizeof(array) / sizeof((array)[0]))

// What `readelf -hlrd` shows of an executable that this test compares.
typedef struct CCNoPieImage CCNoPieImage;
struct CCNoPieImage
{
    bool valid;
    uint16_t type;
    bool interpreter;
    uint64_t base;
    bool position_independent;
    uint32_t relative;
    // DT_NEEDED's names in order, each followed by a space; and each relocated symbol's name and class, "name:class",
    // sorted and without repeats, each followed by a space.
    char needed[BUSTER_CC_NO_PIE_TEXT];
    char relocated[BUSTER_CC_NO_PIE_TEXT];
};

// A link kind's options in an order, and the file they must make.
typedef struct CCNoPieOrder CCNoPieOrder;
struct CCNoPieOrder
{
    const char* name;
    const char* arguments[3];
    uint16_t type;
    bool interpreter;
};

static int cc_no_pie_compare(const void* a, const void* b)
{
    return strcmp(*(const char* const*)a, *(const char* const*)b);
}

// The file's bytes, or null; *length their count.
static uint8_t* cc_no_pie_bytes(CCCompatibilityTest* test, const char* name, uint64_t* length)
{
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", test->directory, name);
    FILE* file = fopen(path, "rb");
    uint8_t* bytes = NULL;
    long size = -1;
    if (file != NULL && fseek(file, 0, SEEK_END) == 0)
    {
        size = ftell(file);
    }
    if (size > 0 && fseek(file, 0, SEEK_SET) == 0)
    {
        bytes = malloc((size_t)size);
    }
    if (bytes != NULL && fread(bytes, 1, (size_t)size, file) != (size_t)size)
    {
        free(bytes);
        bytes = NULL;
    }
    if (file != NULL)
    {
        fclose(file);
    }
    *length = bytes != NULL ? (uint64_t)size : 0;
    return bytes;
}

static bool cc_no_pie_inside(uint64_t length, uint64_t offset, uint64_t size)
{
    return offset <= length && size <= length - offset;
}

// A string at `offset` of the string table section `strings`, or "" outside it.
static const char* cc_no_pie_string(const uint8_t* bytes, uint64_t length, const Elf64_Shdr* strings, uint64_t offset)
{
    const bool inside = strings != NULL && cc_no_pie_inside(length, strings->sh_offset, strings->sh_size) && offset < strings->sh_size &&
                        memchr(bytes + strings->sh_offset + offset, 0, strings->sh_size - offset) != NULL;
    return inside ? (const char*)bytes + strings->sh_offset + offset : "";
}

// A relocation type's class in the comparison, or null for one it doesn't compare.
static const char* cc_no_pie_class(uint32_t type)
{
    const char* result = NULL;
    if (type == R_X86_64_GLOB_DAT || type == R_X86_64_JUMP_SLOT)
    {
        result = "got";
    }
    else if (type == R_X86_64_64)
    {
        result = "abs64";
    }
    else if (type == R_X86_64_TPOFF64)
    {
        result = "tpoff";
    }
    else if (type == R_X86_64_COPY)
    {
        result = "copy";
    }
    return result;
}

static CCNoPieImage cc_no_pie_image(CCCompatibilityTest* test, const char* name, const char* const* referenced)
{
    CCNoPieImage image = { .base = UINT64_MAX };
    uint64_t length = 0;
    uint8_t* bytes = cc_no_pie_bytes(test, name, &length);
    Elf64_Ehdr header = { 0 };
    bool valid = bytes != NULL && length >= sizeof(header);
    if (valid)
    {
        memcpy(&header, bytes, sizeof(header));
    }
    valid = valid && memcmp(header.e_ident, ELFMAG, SELFMAG) == 0 && header.e_ident[EI_CLASS] == ELFCLASS64 &&
            cc_no_pie_inside(length, header.e_phoff, (uint64_t)header.e_phnum * sizeof(Elf64_Phdr)) &&
            cc_no_pie_inside(length, header.e_shoff, (uint64_t)header.e_shnum * sizeof(Elf64_Shdr)) && header.e_shentsize == sizeof(Elf64_Shdr);
    image.type = header.e_type;
    for (uint32_t p = 0; valid && p < header.e_phnum; p += 1)
    {
        Elf64_Phdr program;
        memcpy(&program, bytes + header.e_phoff + p * sizeof(program), sizeof(program));
        image.interpreter = image.interpreter || program.p_type == PT_INTERP;
        image.base = program.p_type == PT_LOAD && program.p_vaddr < image.base ? program.p_vaddr : image.base;
    }
    const Elf64_Shdr* sections = valid ? (const Elf64_Shdr*)(const void*)(bytes + header.e_shoff) : NULL;
    const char* entries[BUSTER_CC_NO_PIE_TEXT / 4];
    char names[BUSTER_CC_NO_PIE_TEXT];
    uint32_t entry_count = 0;
    uint64_t used = 0;
    for (uint32_t s = 0; valid && s < header.e_shnum; s += 1)
    {
        Elf64_Shdr section;
        memcpy(&section, &sections[s], sizeof(section));
        Elf64_Shdr linked = { 0 };
        Elf64_Shdr strings = { 0 };
        const bool has_link = section.sh_link < header.e_shnum;
        if (has_link)
        {
            memcpy(&linked, &sections[section.sh_link], sizeof(linked));
        }
        const bool has_strings = has_link && linked.sh_link < header.e_shnum;
        if (has_strings)
        {
            memcpy(&strings, &sections[linked.sh_link], sizeof(strings));
        }
        valid = cc_no_pie_inside(length, section.sh_offset, section.sh_type == SHT_NOBITS ? 0 : section.sh_size);
        for (uint64_t at = 0; valid && section.sh_type == SHT_DYNAMIC && has_link && at + sizeof(Elf64_Dyn) <= section.sh_size; at += sizeof(Elf64_Dyn))
        {
            Elf64_Dyn entry;
            memcpy(&entry, bytes + section.sh_offset + at, sizeof(entry));
            image.position_independent = image.position_independent || (entry.d_tag == DT_FLAGS_1 && (entry.d_un.d_val & DF_1_PIE) != 0) || entry.d_tag == DT_RELR;
            if (entry.d_tag == DT_NEEDED)
            {
                const uint64_t taken = strlen(image.needed);
                snprintf(image.needed + taken, sizeof(image.needed) - taken, "%s ", cc_no_pie_string(bytes, length, &linked, entry.d_un.d_val));
            }
        }
        const bool symbols = has_strings && linked.sh_type == SHT_DYNSYM && cc_no_pie_inside(length, linked.sh_offset, linked.sh_size);
        for (uint64_t at = 0; valid && section.sh_type == SHT_RELA && at + sizeof(Elf64_Rela) <= section.sh_size; at += sizeof(Elf64_Rela))
        {
            Elf64_Rela relocation;
            memcpy(&relocation, bytes + section.sh_offset + at, sizeof(relocation));
            const uint32_t type = (uint32_t)ELF64_R_TYPE(relocation.r_info);
            const uint64_t index = ELF64_R_SYM(relocation.r_info);
            image.relative += type == R_X86_64_RELATIVE ? 1 : 0;
            Elf64_Sym symbol = { 0 };
            const bool named = symbols && index != 0 && index < linked.sh_size / sizeof(Elf64_Sym);
            if (named)
            {
                memcpy(&symbol, bytes + linked.sh_offset + index * sizeof(Elf64_Sym), sizeof(symbol));
            }
            const char* class = cc_no_pie_class(type);
            // A copy by the program's own name for it (`referenced`) among those .dynsym defines at it: the linkers name
            // its relocation by different aliases (GNU ld environ's by __environ) and export different ones of them.
            const char* label = cc_no_pie_string(bytes, length, &strings, symbol.st_name);
            for (uint64_t d = 1; named && type == R_X86_64_COPY && d < linked.sh_size / sizeof(Elf64_Sym); d += 1)
            {
                Elf64_Sym other;
                memcpy(&other, bytes + linked.sh_offset + d * sizeof(Elf64_Sym), sizeof(other));
                const char* alias = cc_no_pie_string(bytes, length, &strings, other.st_name);
                for (uint32_t r = 0; other.st_shndx != SHN_UNDEF && other.st_value == relocation.r_offset && referenced[r] != NULL; r += 1)
                {
                    label = strcmp(alias, referenced[r]) == 0 ? referenced[r] : label;
                }
            }
            const int written = named && class != NULL && entry_count < BUSTER_CC_NO_PIE_LENGTH(entries) ? snprintf(names + used, sizeof(names) - used, "%s:%s", label, class) : -1;
            if (written > 0 && (uint64_t)written < sizeof(names) - used)
            {
                entries[entry_count] = names + used;
                entry_count += 1;
                used += (uint64_t)written + 1;
            }
        }
    }
    qsort(entries, entry_count, sizeof(entries[0]), cc_no_pie_compare);
    for (uint32_t e = 0; e < entry_count; e += 1)
    {
        const uint64_t taken = strlen(image.relocated);
        if (e == 0 || strcmp(entries[e], entries[e - 1]) != 0)
        {
            snprintf(image.relocated + taken, sizeof(image.relocated) - taken, "%s ", entries[e]);
        }
    }
    image.valid = valid;
    free(bytes);
    return image;
}

// The compiler's and clang's links of the same inputs (`arguments`, after which each puts -o and its output's name),
// run with `expected` as their output, and compared, a copy by its name among `referenced` (NULL-terminated).
static void cc_no_pie_compare_links(CCCompatibilityTest* test, const char* name, const char* const* arguments, uint32_t count, const char* expected,
                                    const char* const* referenced)
{
    const char* ours[32] = { "-no-pie" };
    const char* theirs[32] = { "-fuse-ld=bfd", "-no-pie" };
    uint32_t our_count = 1;
    uint32_t their_count = 2;
    for (uint32_t i = 0; i < count && their_count < 28; i += 1)
    {
        ours[our_count++] = arguments[i];
        theirs[their_count++] = arguments[i];
    }
    ours[our_count++] = "-o";
    ours[our_count++] = "no_pie_ours";
    ours[our_count] = NULL;
    theirs[their_count++] = "-o";
    theirs[their_count++] = "no_pie_theirs";
    theirs[their_count] = NULL;
    CCCompatibilityRun linked = cc_compat_run(test, test->compiler, NULL, ours);
    CCCompatibilityRun reference = cc_compat_run(test, test->clang, NULL, theirs);
    cc_compat_expect(test, linked.status == 0, name);
    cc_compat_expect(test, reference.status == 0, "clang with GNU ld links -no-pie");
    if (linked.status != 0 || reference.status != 0)
    {
        fprintf(stderr, "  %s: ours: %s; clang's: %s\n", name, linked.errors != NULL ? linked.errors : "(missing)", reference.errors != NULL ? reference.errors : "(missing)");
    }
    const char* no_arguments[] = { NULL };
    CCCompatibilityRun ran = linked.status == 0 ? cc_compat_run(test, "./no_pie_ours", NULL, no_arguments) : (CCCompatibilityRun){ .status = -1 };
    CCCompatibilityRun ran_reference = reference.status == 0 ? cc_compat_run(test, "./no_pie_theirs", NULL, no_arguments) : (CCCompatibilityRun){ .status = -1 };
    const bool runs = ran.status == 0 && ran.output != NULL && strcmp(ran.output, expected) == 0;
    cc_compat_expect(test, runs, "-no-pie executable runs");
    cc_compat_expect(test, ran_reference.status == 0 && ran_reference.output != NULL && strcmp(ran_reference.output, expected) == 0, "clang's -no-pie executable runs");
    if (!runs)
    {
        fprintf(stderr, "  %s: status %d, output %s\n", name, ran.status, ran.output != NULL ? ran.output : "(missing)");
    }
    const CCNoPieImage image = linked.status == 0 ? cc_no_pie_image(test, "no_pie_ours", referenced) : (CCNoPieImage){ 0 };
    const CCNoPieImage oracle = reference.status == 0 ? cc_no_pie_image(test, "no_pie_theirs", referenced) : (CCNoPieImage){ 0 };
    const bool both = image.valid && oracle.valid;
    cc_compat_expect(test, both && image.type == ET_EXEC && oracle.type == ET_EXEC && image.interpreter && oracle.interpreter, "-no-pie is ET_EXEC with PT_INTERP");
    cc_compat_expect(test, both && image.base == oracle.base, "-no-pie loads at GNU ld's fixed base");
    cc_compat_expect(test, both && !image.position_independent && !oracle.position_independent && image.relative == 0 && oracle.relative == 0,
                     "-no-pie has no DF_1_PIE, RELR or RELATIVE relocation");
    cc_compat_expect(test, both && strcmp(image.needed, oracle.needed) == 0, "-no-pie needs GNU ld's libraries");
    const bool relocated = both && strcmp(image.relocated, oracle.relocated) == 0;
    cc_compat_expect(test, relocated, "-no-pie relocates GNU ld's symbols by their classes");
    if (both && (!relocated || strcmp(image.needed, oracle.needed) != 0 || image.base != oracle.base))
    {
        fprintf(stderr, "  %s: ours needs %s at %#llx, relocates %s\n  GNU ld's needs %s at %#llx, relocates %s\n", name, image.needed, (unsigned long long)image.base,
                image.relocated, oracle.needed, (unsigned long long)oracle.base, oracle.relocated);
    }
    cc_compat_release(&ran);
    cc_compat_release(&ran_reference);
    cc_compat_release(&linked);
    cc_compat_release(&reference);
}

static void cc_compat_no_pie(CCCompatibilityTest* test)
{
    bool files = cc_compat_write(test, "no_pie_dependency.c", "__thread int dependency_tls = 9;\nint dependency_value(void) { return dependency_tls; }\n");
    files &= cc_compat_write(test, "no_pie.c",
                             "#include <errno.h>\n#include <stdio.h>\n"
                             "extern char** environ;\nextern __thread int dependency_tls;\nint dependency_value(void);\n"
                             "static _Thread_local int own = 3;\n_Thread_local int own_global = 4;\n"
                             "int main(void)\n{\n    int (*put)(const char*) = puts;\n    errno = 0;\n    dependency_tls += 1;\n    own += own_global;\n"
                             "    char line[64];\n    snprintf(line, sizeof(line), \"no-pie %d %d %d %d\", dependency_tls, dependency_value(), own, environ != 0);\n"
                             "    put(line);\n    fflush(stdout);\n    return dependency_tls == 10 && dependency_value() == 10 && own == 7 && put == puts && errno == 0 ? 0 : 1;\n}\n");
    files &= cc_compat_write(test, "no_pie_helper.c",
                             "#include <stdio.h>\nextern char** environ;\nextern void no_pie_maybe(void) __attribute__((weak));\n"
                             "int no_pie_helper(int x)\n{\n    if (no_pie_maybe)\n    {\n        no_pie_maybe();\n    }\n"
                             "    fprintf(stdout, \"helper %d %d\\n\", x, environ != 0);\n    return x + 5;\n}\n");
    files &= cc_compat_write(test, "no_pie_mixed.c", "int no_pie_helper(int x);\nint main(void) { return no_pie_helper(37) == 42 ? 0 : 1; }\n");
    cc_compat_expect(test, files, "write -no-pie fixtures");
    const char* const library[] = { "-shared", "-fPIC", "no_pie_dependency.c", "-o", "libnopiedependency.so", NULL };
    CCCompatibilityRun built = cc_compat_run(test, test->clang, NULL, library);
    const char* const helper[] = { "-O1", "-fno-pie", "-c", "no_pie_helper.c", "-o", "no_pie_helper.o", NULL };
    CCCompatibilityRun compiled = cc_compat_run(test, test->clang, NULL, helper);
    cc_compat_expect(test, built.status == 0 && compiled.status == 0, "clang builds the -no-pie fixtures' library and -fno-pie object");
    if (files && built.status == 0 && compiled.status == 0)
    {
        const char* const referenced[] = { "environ", "stdout", NULL };
        const char* const program[] = { "no_pie.c", "-L.", "-lnopiedependency", "-Wl,-rpath,$ORIGIN" };
        cc_no_pie_compare_links(test, "-no-pie links libc's functions and data, TLS and a library's thread-local", program, BUSTER_CC_NO_PIE_LENGTH(program),
                                "no-pie 10 10 7 1\n", referenced);
        const char* const mixed[] = { "no_pie_mixed.c", "no_pie_helper.o" };
        cc_no_pie_compare_links(test, "-no-pie links a -fno-pie object's copies and weak function", mixed, BUSTER_CC_NO_PIE_LENGTH(mixed), "helper 37 1\n",
                                referenced);
    }
    cc_compat_release(&built);
    cc_compat_release(&compiled);
    // The link kind's options in clang's order: the type, whether there is an interpreter, and the run.
    static const CCNoPieOrder orders[] = {
        { "-no-pie after -pie is position dependent", { "-pie", "-no-pie", NULL }, ET_EXEC, true },
        { "-pie after -no-pie is a PIE", { "-no-pie", "-pie", NULL }, ET_DYN, true },
        { "-static over -no-pie", { "-no-pie", "-static", NULL }, ET_EXEC, false },
        { "-static before -no-pie", { "-static", "-no-pie", NULL }, ET_EXEC, false },
    };
    for (uint32_t o = 0; o < BUSTER_CC_NO_PIE_LENGTH(orders); o += 1)
    {
        const char* args[] = { orders[o].arguments[0], orders[o].arguments[1], "main.c", "-o", "program", NULL };
        CCCompatibilityRun linked = cc_compat_run(test, test->compiler, NULL, args);
        const bool elf = linked.status == 0 && cc_compat_elf(test, "program", orders[o].type, orders[o].interpreter);
        const char* no_arguments[] = { NULL };
        CCCompatibilityRun executed = elf ? cc_compat_run(test, "./program", NULL, no_arguments) : (CCCompatibilityRun){ .status = -1 };
        cc_compat_expect(test, elf && executed.status == 0 && executed.output != NULL && strcmp(executed.output, "CC smoke\n") == 0, orders[o].name);
        cc_compat_release(&executed);
        cc_compat_release(&linked);
    }
    for (uint32_t order = 0; order < 2; order += 1)
    {
        const char* args[] = { order == 0 ? "-no-pie" : "-static-pie", order == 0 ? "-static-pie" : "-no-pie", "main.c", "-o", "refused", NULL };
        CCCompatibilityRun refused = cc_compat_run(test, test->compiler, NULL, args);
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/refused", test->directory);
        cc_compat_expect(test, refused.status != 0 && refused.errors != NULL && strstr(refused.errors, "'-no-pie' along with '-static-pie'") != NULL && access(path, F_OK) != 0,
                         "-no-pie with -static-pie is refused, as clang refuses it");
        cc_compat_release(&refused);
    }
}
