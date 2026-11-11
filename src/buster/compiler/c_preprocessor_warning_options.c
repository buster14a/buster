// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Command-line warning names and the policy for diagnosing unknown ones (#1467). The driver vocabulary includes
// spellings outside __has_warning's group catalog. The ordered option slice is borrowed from PPOptions, just as
// the checker's warning policy borrows it. Resolve the final policy before publishing through the ordinary gate,
// so a later suppression applies to earlier misspellings, and deferred-history replay repeats the same diagnostics.

typedef enum PPWarningOptionKind
{
    PP_WARNING_OPTION_DRIVER,
    PP_WARNING_OPTION_GROUP,
    PP_WARNING_OPTION_ERROR,
    PP_WARNING_OPTION_FATAL,
    PP_WARNING_OPTION_INVALID,
} PPWarningOptionKind;

typedef struct PPWarningOption PPWarningOption;
struct PPWarningOption
{
    const char* name;
    const char* original;
    const char* prefix;
    PPWarningOptionKind kind;
    bool negative;
};

static bool pp_warning_transport(const char* argument)
{
    return strncmp(argument, "-Wa,", 4) == 0 || strncmp(argument, "-Wp,", 4) == 0 || strncmp(argument, "-Wl,", 4) == 0;
}

// Only these positive spellings are driver options with a value. In particular, do not remove an arbitrary =VALUE
// from a warning name: -Wformat=2 is a group, while -Wformat=3 and -Wno-frame-larger-than=64 are unknown names.
static bool pp_warning_driver_option(const char* argument)
{
    static const char* const prefixes[] =
    {
        "-Wlarger-than=",
        "-Wlarger-than-",
        "-Wframe-larger-than=",
        "-Wlarge-by-value-copy=",
        "-Wundef-prefix=",
        "-Wsystem-headers-in-module=",
    };
    bool known = pp_warning_transport(argument);
    for (uint32_t i = 0; !known && i < BUSTER_ARRAY_LENGTH(prefixes); i += 1)
    {
        const char* prefix = prefixes[i];
        known = strncmp(argument, prefix, strlen(prefix)) == 0;
    }
    return known;
}

// The name borrows a NUL-terminated suffix of argv, or a static alias. No fixed buffer limits a diagnostic's name.
// Clang's legacy nonportable-cfstrings options are joined options: their suffix, even empty, is the warning name.
static PPWarningOption pp_warning_option(const char* argument)
{
    PPWarningOption result =
    {
        .name = "",
        .original = "",
        .prefix = "-W",
        .kind = PP_WARNING_OPTION_DRIVER,
    };
    if (strncmp(argument, "-W", 2) == 0 && !pp_warning_driver_option(argument))
    {
        result.kind = PP_WARNING_OPTION_GROUP;
        result.name = argument + 2;
        if (strncmp(argument, "-Wnonportable-cfstrings", sizeof("-Wnonportable-cfstrings") - 1) == 0)
        {
            result.name = argument + sizeof("-Wnonportable-cfstrings") - 1;
        }
        else if (strncmp(argument, "-Wno-nonportable-cfstrings", sizeof("-Wno-nonportable-cfstrings") - 1) == 0)
        {
            result.name = argument + sizeof("-Wno-nonportable-cfstrings") - 1;
        }
        result.original = result.name;
        if (strcmp(result.name, "format=0") == 0)
        {
            result.name = "no-format";
        }
        result.negative = strncmp(result.name, "no-", 3) == 0;
        result.name += result.negative ? 3 : 0;
        result.prefix = result.negative ? "-Wno-" : "-W";
        if (strncmp(result.name, "error", 5) == 0)
        {
            const bool bare = result.name[5] == 0;
            const bool valid = bare || result.name[5] == '=' || strcmp(result.name + 5, "-implicit-function-declaration") == 0;
            result.kind = valid ? PP_WARNING_OPTION_ERROR : PP_WARNING_OPTION_INVALID;
            result.prefix = valid ? "-Werror=" : "-Werror";
            result.name += valid ? (bare ? 5 : 6) : 0;
        }
        else if (strncmp(result.name, "fatal-errors", 12) == 0)
        {
            const bool bare = result.name[12] == 0;
            const bool valid = bare || ((result.name[12] == '=' || result.name[12] == '-') && result.name[13] != 0);
            result.kind = valid ? PP_WARNING_OPTION_FATAL : PP_WARNING_OPTION_INVALID;
            result.prefix = valid ? "-Wfatal-errors=" : "-Wfatal-errors";
            result.name += valid ? (bare ? 12 : 13) : 0;
        }
    }
    return result;
}

// The existing catalog is sorted by the full -W spelling, so the suffixes have the same order. Empty is Clang's
// historical -W/-Wno- alias for -Wextra; everything and system-headers are controls, not catalog groups.
static bool pp_warning_group_known(const char* name)
{
    bool known = name[0] == 0;
    uint32_t low = 0;
    uint32_t high = BUSTER_ARRAY_LENGTH(pp_features_warning);
    while (!known && low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const int compared = strcmp(name, pp_features_warning[middle].name + 2);
        known = compared == 0;
        high = compared < 0 ? middle : high;
        low = compared > 0 ? middle + 1 : low;
    }
    return known;
}

// clang's warning groups' tree (#1447, diagnose_if's fourth argument): each edge a group (its index in pp_features_warning)
// and a group that holds it, sorted by the first. Generated from clang 23.1.5's `diagtool tree` (LLVM's
// DiagnosticGroups.td), by its indentation, leaving out the remark groups and the bare -W (the catalog's empty name,
// -Wextra's alias, which reads -Wextra's edges).
static const uint16_t pp_warning_group_children[672] =
{
    1, 19, 23, 38, 39, 42, 43, 52, 54, 67, 71, 83, 84, 85, 87, 88, 89, 91, 91, 93, 99, 103, 104, 105,
    109, 109, 110, 110, 112, 112, 113, 114, 114, 114, 115, 116, 116, 116, 117, 117, 117, 118, 119, 119, 119, 120, 120, 120,
    121, 123, 124, 124, 124, 125, 125, 126, 126, 126, 128, 133, 133, 133, 134, 134, 135, 136, 136, 136, 137, 138, 140, 141,
    142, 155, 155, 157, 158, 158, 159, 159, 160, 160, 162, 162, 164, 165, 166, 171, 178, 178, 179, 180, 185, 186, 192, 192,
    195, 196, 198, 199, 203, 208, 210, 211, 213, 222, 225, 226, 227, 228, 229, 230, 231, 238, 239, 240, 241, 242, 243, 247,
    248, 249, 250, 251, 255, 255, 256, 257, 258, 259, 260, 260, 262, 262, 263, 264, 265, 266, 267, 268, 268, 269, 269, 270,
    270, 271, 271, 272, 272, 274, 274, 274, 275, 276, 278, 281, 282, 283, 284, 284, 285, 286, 286, 286, 287, 288, 289, 290,
    290, 297, 299, 300, 303, 304, 306, 307, 311, 318, 324, 327, 327, 328, 332, 333, 335, 336, 337, 343, 352, 354, 358, 362,
    365, 366, 368, 369, 370, 370, 371, 372, 373, 374, 376, 377, 377, 378, 380, 380, 382, 382, 383, 385, 385, 386, 390, 391,
    400, 401, 408, 409, 409, 411, 411, 412, 413, 413, 414, 414, 415, 415, 416, 416, 417, 419, 419, 420, 420, 421, 421, 422,
    423, 423, 424, 424, 426, 426, 427, 427, 428, 428, 428, 429, 430, 430, 430, 431, 431, 432, 432, 433, 434, 434, 435, 436,
    436, 437, 438, 438, 439, 449, 456, 460, 461, 462, 463, 464, 464, 465, 466, 467, 468, 470, 470, 473, 475, 476, 477, 478,
    479, 480, 481, 482, 483, 484, 485, 486, 487, 489, 493, 499, 505, 506, 513, 514, 518, 521, 521, 521, 529, 529, 531, 533,
    554, 556, 559, 561, 565, 567, 568, 569, 570, 571, 572, 573, 574, 575, 576, 577, 578, 579, 580, 581, 582, 583, 584, 584,
    585, 586, 587, 588, 589, 590, 591, 592, 594, 595, 597, 598, 599, 601, 609, 613, 617, 618, 619, 620, 620, 621, 621, 622,
    623, 623, 624, 625, 626, 626, 627, 627, 628, 629, 629, 630, 632, 633, 634, 634, 635, 635, 636, 638, 638, 639, 640, 641,
    642, 643, 643, 644, 645, 646, 647, 648, 649, 650, 651, 653, 657, 658, 661, 663, 664, 666, 687, 688, 693, 698, 706, 707,
    708, 709, 711, 712, 715, 721, 722, 728, 728, 730, 731, 734, 736, 741, 741, 745, 748, 752, 757, 771, 772, 775, 775, 776,
    776, 777, 780, 788, 794, 795, 796, 797, 798, 799, 799, 800, 801, 809, 810, 811, 812, 820, 820, 822, 823, 824, 833, 836,
    836, 840, 844, 845, 846, 848, 849, 852, 852, 852, 852, 853, 853, 853, 854, 854, 854, 854, 854, 855, 855, 855, 855, 856,
    856, 856, 856, 856, 856, 857, 857, 857, 857, 857, 858, 858, 858, 858, 858, 858, 859, 859, 859, 859, 859, 860, 860, 860,
    860, 860, 860, 860, 860, 861, 861, 861, 861, 861, 861, 864, 866, 868, 868, 869, 872, 878, 892, 893, 894, 894, 899, 904,
    909, 910, 911, 911, 913, 916, 916, 917, 919, 920, 922, 923, 923, 924, 925, 926, 934, 935, 936, 937, 938, 938, 939, 943,
    945, 946, 947, 947, 949, 950, 955, 958, 959, 959, 960, 965, 966, 967, 970, 973, 975, 976, 977, 983, 986, 1003, 1004, 1005,
    1007, 1013, 1016, 1017, 1023, 1024, 1025, 1026, 1028, 1029, 1030, 1030, 1031, 1032, 1032, 1033, 1034, 1035, 1036, 1037, 1038, 1042, 1043, 1045,
    1046, 1050, 1051, 1052, 1054, 1056, 1067, 1070, 1074, 1080, 1081, 1082, 1087, 1088, 1089, 1095, 1099, 1099, 1103, 1104, 1104, 1105, 1107, 1109,
    1110, 1111, 1112, 1113, 1115, 1116, 1117, 1118, 1119, 1120, 1132, 1134, 1135, 1136, 1137, 1138, 1140, 1141, 1143, 1145, 1146, 1147, 1147, 1151,
    1152, 1153, 1154, 1156, 1157, 1160, 1161, 1163, 1163, 1163, 1163, 1166, 1171, 1171, 1172, 1172, 1173, 1174, 1174, 1175, 1177, 1181, 1185, 1185,
};

static const uint16_t pp_warning_group_parents[672] =
{
    216, 3, 212, 41, 36, 36, 36, 687, 53, 829, 829, 829, 203, 208, 823, 93, 823, 92, 208, 687, 831, 102, 102, 102,
    106, 111, 109, 290, 109, 918, 107, 113, 356, 829, 113, 113, 599, 829, 108, 109, 697, 117, 123, 401, 829, 81, 123, 829,
    122, 129, 128, 401, 829, 127, 130, 125, 131, 703, 132, 137, 401, 829, 135, 143, 144, 137, 171, 829, 145, 401, 146, 140,
    148, 81, 853, 161, 82, 161, 161, 356, 157, 596, 157, 1102, 829, 167, 168, 172, 177, 354, 177, 687, 687, 208, 193, 687,
    174, 829, 197, 197, 208, 705, 212, 212, 212, 63, 224, 224, 224, 823, 224, 224, 224, 102, 238, 241, 238, 238, 241, 250,
    147, 250, 687, 829, 33, 253, 253, 253, 253, 253, 253, 354, 253, 261, 260, 262, 212, 253, 266, 253, 319, 253, 331, 253,
    332, 253, 336, 253, 337, 125, 253, 517, 253, 253, 1001, 280, 253, 253, 253, 831, 253, 125, 253, 907, 253, 253, 253, 253,
    1181, 296, 456, 299, 302, 302, 305, 829, 102, 1014, 829, 354, 357, 829, 335, 331, 208, 335, 335, 829, 687, 3, 330, 831,
    829, 208, 366, 366, 600, 687, 687, 371, 371, 371, 387, 371, 388, 377, 371, 387, 371, 388, 382, 371, 387, 371, 687, 392,
    354, 829, 407, 407, 829, 407, 829, 407, 407, 829, 407, 829, 407, 829, 407, 829, 407, 407, 829, 407, 829, 407, 829, 407,
    407, 829, 407, 829, 407, 829, 407, 829, 407, 730, 829, 407, 407, 829, 835, 407, 829, 407, 829, 432, 407, 982, 407, 407,
    829, 407, 407, 829, 407, 453, 70, 535, 463, 463, 851, 354, 458, 464, 687, 63, 481, 335, 480, 472, 208, 466, 466, 208,
    478, 102, 475, 809, 482, 482, 482, 482, 482, 102, 829, 504, 504, 504, 511, 616, 687, 171, 354, 813, 208, 530, 687, 532,
    829, 102, 829, 829, 566, 592, 592, 571, 582, 588, 584, 587, 584, 587, 567, 579, 582, 588, 587, 568, 567, 568, 565, 587,
    584, 587, 565, 566, 584, 584, 587, 566, 208, 705, 823, 823, 829, 831, 610, 1014, 616, 616, 616, 616, 829, 616, 829, 616,
    616, 829, 616, 616, 616, 829, 616, 829, 616, 616, 829, 616, 616, 616, 616, 829, 616, 829, 616, 616, 690, 616, 616, 616,
    616, 616, 829, 616, 616, 616, 646, 616, 616, 616, 616, 19, 687, 687, 663, 354, 376, 354, 19, 687, 687, 829, 208, 708,
    511, 189, 321, 371, 714, 1014, 721, 208, 209, 354, 354, 733, 829, 203, 774, 687, 687, 208, 687, 744, 771, 475, 774, 478,
    774, 751, 829, 829, 793, 793, 793, 793, 793, 793, 800, 793, 793, 208, 829, 823, 687, 19, 819, 821, 19, 823, 688, 12,
    91, 841, 999, 1080, 851, 851, 848, 109, 154, 157, 853, 111, 156, 161, 109, 121, 152, 157, 855, 111, 122, 153, 161, 109,
    121, 125, 150, 157, 857, 111, 122, 127, 151, 161, 109, 121, 125, 134, 157, 859, 111, 122, 127, 135, 161, 109, 121, 125,
    134, 139, 157, 861, 862, 111, 122, 127, 135, 161, 863, 865, 867, 869, 870, 871, 873, 687, 600, 892, 687, 892, 407, 688,
    687, 909, 171, 909, 915, 914, 915, 915, 831, 829, 925, 224, 921, 688, 687, 925, 933, 687, 935, 935, 687, 688, 354, 944,
    944, 944, 943, 946, 943, 944, 823, 478, 354, 705, 208, 84, 687, 687, 1014, 1087, 784, 793, 978, 169, 1087, 12, 354, 208,
    687, 1014, 19, 19, 399, 1025, 687, 1025, 1026, 1025, 774, 1025, 1025, 12, 1025, 1056, 1025, 1056, 1056, 1056, 1027, 102, 829, 1044,
    1044, 1044, 1044, 1051, 687, 1027, 1065, 91, 829, 1156, 825, 1081, 687, 1087, 1087, 70, 687, 851, 354, 1143, 1155, 1150, 1108, 1108,
    1107, 1107, 1107, 1108, 1114, 1114, 1114, 1114, 1114, 941, 354, 687, 1134, 1138, 354, 1134, 1156, 1157, 1134, 1134, 1134, 1134, 1148, 354,
    1134, 1134, 1156, 1134, 1134, 915, 687, 137, 166, 439, 868, 1167, 19, 1172, 407, 1170, 1171, 840, 1175, 841, 687, 1182, 407, 829,
};

// The index in pp_features_warning of the group named `name` (without its -W), or UINT32_MAX; the empty name is -Wextra's.
static __attribute__((unused)) uint32_t pp_warning_group_index(const char* name)
{
    const char* named = name[0] == 0 ? "extra" : name;
    uint32_t result = UINT32_MAX;
    uint32_t low = 0;
    uint32_t high = BUSTER_ARRAY_LENGTH(pp_features_warning);
    while (result == UINT32_MAX && low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const int compared = strcmp(named, pp_features_warning[middle].name + 2);
        result = compared == 0 ? middle : result;
        high = compared < 0 ? middle : high;
        low = compared > 0 ? middle + 1 : low;
    }
    return result;
}

// Whether group `holder` is group `group` or holds it, through clang's tree (pp_warning_group_parents): a walk up from
// `group`, its stack and its visited set bounded by the catalog.
static __attribute__((unused)) bool pp_warning_group_holds(uint32_t holder, uint32_t group)
{
    enum { GROUPS = BUSTER_ARRAY_LENGTH(pp_features_warning) };
    uint64_t visited[(GROUPS + 63) / 64] = { 0 };
    uint16_t stack[GROUPS];
    uint32_t depth = 0;
    bool found = holder == group;
    if (group < GROUPS)
    {
        stack[depth++] = (uint16_t)group;
        visited[group / 64] |= (uint64_t)1 << (group % 64);
    }
    while (!found && depth != 0)
    {
        const uint32_t child = stack[--depth];
        // The first edge of the child: the edges are sorted by it.
        uint32_t low = 0;
        uint32_t high = BUSTER_ARRAY_LENGTH(pp_warning_group_children);
        while (low < high)
        {
            const uint32_t middle = low + (high - low) / 2;
            low = pp_warning_group_children[middle] < child ? middle + 1 : low;
            high = pp_warning_group_children[middle] < child ? high : middle;
        }
        for (uint32_t e = low; !found && e < BUSTER_ARRAY_LENGTH(pp_warning_group_children) && pp_warning_group_children[e] == child; e += 1)
        {
            const uint32_t parent = pp_warning_group_parents[e];
            found = parent == holder;
            const bool fresh = (visited[parent / 64] & ((uint64_t)1 << (parent % 64))) == 0;
            visited[parent / 64] |= (uint64_t)1 << (parent % 64);
            if (fresh && depth < GROUPS)
            {
                stack[depth++] = (uint16_t)parent;
            }
        }
    }
    return found;
}

static bool pp_warning_option_known(PPWarningOption option)
{
    const bool control = option.kind == PP_WARNING_OPTION_GROUP &&
                         (strcmp(option.name, "everything") == 0 || strcmp(option.name, "system-headers") == 0);
    return option.kind == PP_WARNING_OPTION_DRIVER || control ||
           (option.kind != PP_WARNING_OPTION_INVALID && pp_warning_group_known(option.name));
}

typedef enum PPWarningOptionSeverity
{
    PP_WARNING_OPTION_IGNORED,
    PP_WARNING_OPTION_WARNING,
    PP_WARNING_OPTION_AS_ERROR,
    PP_WARNING_OPTION_AS_FATAL,
} PPWarningOptionSeverity;

// Unknown-option diagnostics are default warnings. The global promotion switches and explicit group mappings are
// independent: -Wno-error=NAME survives a later global -Werror, but an explicit -Werror=NAME still sets an error.
// -w and dependency-only preprocessing suppress this default-warning diagnostic even after explicit promotion.
static PPWarningOptionSeverity pp_warning_option_severity(const PPOptions* options)
{
    PPWarningOptionSeverity mapped = PP_WARNING_OPTION_WARNING;
    bool errors = false;
    bool fatal = false;
    bool no_error = false;
    bool no_fatal = false;
    bool quiet = options->suppress_warnings;
    const uint32_t count = options->semantic_warning_option_count;
    const char* const* arguments = options->semantic_warning_options;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const char* argument = arguments[i];
        const PPWarningOption option = pp_warning_option(argument);
        const bool matches = strcmp(option.name, "unknown-warning-option") == 0;
        quiet |= strcmp(argument, "-w") == 0;
        if (option.kind == PP_WARNING_OPTION_ERROR && option.name[0] == 0)
        {
            errors = !option.negative;
        }
        else if (option.kind == PP_WARNING_OPTION_FATAL && option.name[0] == 0)
        {
            fatal = !option.negative;
        }
        else if (option.kind == PP_WARNING_OPTION_GROUP && strcmp(option.name, "everything") == 0 && option.negative)
        {
            mapped = PP_WARNING_OPTION_IGNORED;
            no_fatal = false;
        }
        else if (matches && option.kind == PP_WARNING_OPTION_GROUP)
        {
            mapped = option.negative ? PP_WARNING_OPTION_IGNORED : mapped == PP_WARNING_OPTION_IGNORED ? PP_WARNING_OPTION_WARNING : mapped;
            no_fatal = false;
        }
        else if (matches && option.kind == PP_WARNING_OPTION_ERROR)
        {
            if (option.negative)
            {
                mapped = mapped >= PP_WARNING_OPTION_AS_ERROR ? PP_WARNING_OPTION_WARNING : mapped;
                no_error = true;
            }
            else
            {
                mapped = PP_WARNING_OPTION_AS_ERROR;
                no_fatal = false;
            }
        }
        else if (matches && option.kind == PP_WARNING_OPTION_FATAL)
        {
            if (option.negative)
            {
                mapped = mapped == PP_WARNING_OPTION_AS_FATAL ? PP_WARNING_OPTION_AS_ERROR : mapped;
                no_fatal = true;
            }
            else
            {
                mapped = PP_WARNING_OPTION_AS_FATAL;
                no_fatal = false;
            }
        }
    }
    mapped = mapped == PP_WARNING_OPTION_WARNING && errors && !no_error ? PP_WARNING_OPTION_AS_ERROR : mapped;
    mapped = mapped == PP_WARNING_OPTION_AS_ERROR && fatal && !no_fatal ? PP_WARNING_OPTION_AS_FATAL : mapped;
    return quiet ? PP_WARNING_OPTION_IGNORED : mapped;
}

// This runs before the prelude, in both recorded and deferred-history paths. Reuse the ordinary publication gate
// and counters; no source pragma can retroactively alter a command-line diagnostic. The optional typo hint is not
// emitted: __has_warning's catalog also includes empty groups, which Clang deliberately excludes from suggestions.
static void pp_warning_options_diagnose(Preprocessor* pp)
{
    const PPWarningOptionSeverity mapped = pp_warning_option_severity(&pp->options);
    const uint32_t count = pp->options.semantic_warning_option_count;
    const char* const* arguments = pp->options.semantic_warning_options;
    const PPSeverity severity = mapped == PP_WARNING_OPTION_AS_FATAL ? PP_SEVERITY_MAPPED_FATAL :
                                mapped == PP_WARNING_OPTION_AS_ERROR ? PP_SEVERITY_MAPPED_ERROR : PP_SEVERITY_WARNING_ALWAYS;
    for (uint32_t i = 0; mapped != PP_WARNING_OPTION_IGNORED && !pp->fatal && i < count; i += 1)
    {
        const char* argument = arguments[i];
        const PPWarningOption option = pp_warning_option(argument);
        if (!pp_warning_option_known(option))
        {
            if (option.kind == PP_WARNING_OPTION_INVALID)
            {
                pp_diagnose(pp, severity, 0, "unknown %s warning specifier: '-W%s'", option.prefix, option.original);
            }
            else
            {
                pp_diagnose(pp, severity, 0, "unknown warning option '%s%s'", option.prefix, option.name);
            }
        }
    }
}
