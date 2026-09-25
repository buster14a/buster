/* Diagnostic-only extraction of the pinned production interner. No repository edits. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require(int condition, char const *message)
{
    if (!condition) { fprintf(stderr, "prepare: %s\n", message); exit(1); }
}

static char *read_file(char const *path)
{
    FILE *file = fopen(path, "rb");
    require(file != NULL, "cannot open source");
    require(fseek(file, 0, SEEK_END) == 0, "seek failed");
    long size = ftell(file);
    require(size > 0 && size <= 16 * 1024 * 1024, "source size outside bound");
    require(fseek(file, 0, SEEK_SET) == 0, "rewind failed");
    char *text = malloc((size_t)size + 1);
    require(text != NULL, "allocation failed");
    require(fread(text, 1, (size_t)size, file) == (size_t)size, "short read");
    text[size] = 0;
    require(fclose(file) == 0, "close failed");
    return text;
}

/* Exact occurrence counts make source drift fail closed, not silently misinstrument. */
static char *replace(char *input, char const *old, char const *new_text, unsigned expected)
{
    size_t old_size = strlen(old), new_size = strlen(new_text);
    unsigned count = 0;
    for (char const *p = input; (p = strstr(p, old)) != NULL; p += old_size) { count += 1; }
    require(count == expected, old);
    size_t capacity = strlen(input) + 1 + (new_size > old_size ? (new_size - old_size) * count : 0);
    char *output = malloc(capacity);
    require(output != NULL, "replacement allocation failed");
    char *write = output;
    char const *read = input;
    char const *match;
    while ((match = strstr(read, old)) != NULL)
    {
        size_t prefix = (size_t)(match - read);
        memcpy(write, read, prefix); write += prefix;
        memcpy(write, new_text, new_size); write += new_size;
        read = match + old_size;
    }
    strcpy(write, read);
    free(input);
    return output;
}

int main(int argc, char **argv)
{
    require(argc == 3, "usage: prepare pinned-c_source.c output-kernel.inc");
    char *source = read_file(argv[1]);
    char *begin = strstr(source, "struct CSymbolSlot\n{");
    char *end = strstr(source, "\nBUSTER_C_SHARED String8 const c_declaration_keyword_spellings[]");
    require(begin != NULL && end != NULL && end > begin, "kernel anchors missing");
    size_t size = (size_t)(end - begin);
    char *text = malloc(size + 1);
    require(text != NULL, "kernel allocation failed");
    memcpy(text, begin, size); text[size] = 0;
    free(source);
    text = replace(text, "BUSTER_C_INTERNAL CSymbolKey c_symbol_key", "static u32 probe_hash(CSymbolKey key, String8 name);\n\nBUSTER_C_INTERNAL CSymbolKey c_symbol_key", 1);
    text = replace(text, "c_symbol_slot_hash(key, name.length)", "probe_hash(key, name)", 2);
    text = replace(text, "c_symbol_slot_hash((CSymbolKey){.low = entry.low, .high = entry.high}, entry.length_and_id >> 32)", "probe_hash((CSymbolKey){.low = entry.low, .high = entry.high}, table->names[(u32)entry.length_and_id])", 1);
    text = replace(text, "    u64 end = name.length - 8;", "    probe_counts.middle_calls += 1;\n    u64 end = name.length - 8;", 1);
    text = replace(text, "        memcpy(&stored_word, stored.pointer + offset, sizeof(stored_word));", "        probe_counts.middle_words += 1;\n        memcpy(&stored_word, stored.pointer + offset, sizeof(stored_word));", 1);
    text = replace(text, "    memcpy(&stored_word, stored.pointer + end - 8, sizeof(stored_word));", "    probe_counts.middle_words += 1;\n    memcpy(&stored_word, stored.pointer + end - 8, sizeof(stored_word));", 1);
    text = replace(text, "        CSymbolSlot* entry = &table->slots[slot];", "        probe_counts.query_slots += 1;\n        CSymbolSlot* entry = &table->slots[slot];", 1);
    text = replace(text, "            CSymbolSlot entry = table->slots[old_slot];", "            probe_counts.rehash_scan += 1;\n            CSymbolSlot entry = table->slots[old_slot];", 1);
    text = replace(text, "while (slots[rebuilt_slot].length_and_id)", "while ((probe_counts.rehash_slots += 1, slots[rebuilt_slot].length_and_id))", 1);
    text = replace(text, "while (table->slots[slot].length_and_id)", "while ((probe_counts.requery_slots += 1, table->slots[slot].length_and_id))", 1);
    FILE *output = fopen(argv[2], "wb");
    require(output != NULL, "cannot open output");
    size_t length = strlen(text);
    require(fwrite(text, 1, length, output) == length, "short write");
    require(fclose(output) == 0, "output close failed");
    printf("EXTRACTED original_bytes=%zu instrumented_bytes=%zu\n", size, length);
    free(text);
    return 0;
}
