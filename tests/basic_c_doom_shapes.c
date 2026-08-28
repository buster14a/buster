// The C shapes DoomGeneric is built out of, each of which the C frontend used
// to reject or, worse, lower to something that ran wrong.
//
// Every one of these was found by compiling upstream DoomGeneric unmodified
// (see `./build/build test_doom` and the harness notes in AGENTS.md); the file
// and the construct that found it are named on each case. The program returns
// zero when every shape behaves, and the number of the first shape that did
// not otherwise, so a failure names the construct instead of a hash.
//
// Two of the shapes are about what reaches the object file rather than about
// what the program computes, and both are checked by the fact that this file
// links at all:
//
//   * `doom_shape_unused_import` is declared inside a function and used
//     nowhere. Emitting an undefined symbol for it would fail the link,
//     because nothing anywhere defines it.
//   * `doom_shape_imported_keys` takes the addresses of objects that only
//     tests/basic_c_doom_shapes_import.c defines and that this file names
//     nowhere else. Without a relocation the array holds null pointers, and
//     shape 9 dereferences them.

#define DOOM_SHAPE_LANES 8

typedef int doom_fixed_t;

// Shape 1 (tables.c): the declaration spells the bound as a constant
// expression and the definition spells the folded number, through a typedef of
// the element type. C requires the two to be one type.
extern const doom_fixed_t doom_shape_table[DOOM_SHAPE_LANES / 2];
const int doom_shape_table[4] = {10, 20, 30, 40};

typedef struct DoomShapeNode DoomShapeNode;
struct DoomShapeNode
{
    DoomShapeNode* link;
    int value;
};

// Shape 8 (sounds.c): a static initializer that takes the address of an
// element of the object being defined.
DoomShapeNode doom_shape_nodes[3] = {
    {0, 1},
    {&doom_shape_nodes[0], 2},
    {&doom_shape_nodes[2], 3},
};

typedef struct DoomShapeModule DoomShapeModule;
struct DoomShapeModule
{
    int (*init)(int);
    const char* name;
};

static int doom_shape_module_init(int value)
{
    return value * 3;
}

static DoomShapeModule doom_shape_module = {doom_shape_module_init, "module"};
static DoomShapeModule* doom_shape_modules[1] = {&doom_shape_module};

extern int doom_shape_import_a;
extern int doom_shape_import_b;
extern int doom_shape_import_c;

// Shape 9 (g_game.c): a static table of pointers to objects another
// translation unit defines.
static int* doom_shape_imported_keys[] = {&doom_shape_import_a, &doom_shape_import_b, &doom_shape_import_c};

static int doom_shape_add(int a, int b, int c)
{
    return a + b + c;
}

static int doom_shape_multiply(int a, int b, int c)
{
    return a * b * c;
}

int main(void)
{
    // Shape 2 (f_wipe.c): a block-scope array of function pointers, declared
    // with a parenthesized declarator whose group ends in `]` rather than in
    // the declared name.
    static int (*doom_shape_wipes[])(int, int, int) = {doom_shape_add, doom_shape_multiply};
    // Shape 4 (d_main.c): an automatic two-dimensional character array whose
    // rows are bare string literals.
    char doom_shape_names[3][8] = {"one", "two", "three"};
    char doom_shape_source[4] = {0};
    char doom_shape_destination[4] = {0};
    char* doom_shape_read = doom_shape_source;
    char* doom_shape_write = doom_shape_destination;
    int index;

    // Shape 5 (i_video.c): a function declared inside a function body and
    // called there. Shape 7 (i_sound.c): one declared and never used, which
    // must not become an undefined symbol.
    extern int doom_shape_helper(int value);
    extern int doom_shape_unused_import;

    if (doom_shape_table[0] != 10 || doom_shape_table[3] != 40)
    {
        return 1;
    }
    if (doom_shape_wipes[0](1, 2, 3) != 6 || doom_shape_wipes[1](2, 3, 4) != 24)
    {
        return 2;
    }
    // Shape 3 (i_sound.c, w_file.c, m_menu.c): a call through a function
    // pointer reached by subscripting a table of structure pointers.
    if (doom_shape_modules[0]->init(5) != 15)
    {
        return 3;
    }
    if (doom_shape_names[0][0] != 'o' || doom_shape_names[1][2] != 'o' || doom_shape_names[2][4] != 'e' || doom_shape_names[2][5] != 0 ||
        doom_shape_names[0][3] != 0)
    {
        return 4;
    }
    if (doom_shape_helper(41) != 42)
    {
        return 5;
    }
    // Shape 6 (i_scale.c): a chained assignment whose left operands both
    // advance the pointer they write through.
    doom_shape_source[0] = 'x';
    doom_shape_source[1] = 'y';
    *doom_shape_write++ = *doom_shape_read++ = 'A';
    *doom_shape_write++ = *doom_shape_read++ = 'B';
    if (doom_shape_destination[0] != 'A' || doom_shape_destination[1] != 'B' || doom_shape_source[0] != 'A' || doom_shape_source[1] != 'B' ||
        doom_shape_read != doom_shape_source + 2 || doom_shape_write != doom_shape_destination + 2)
    {
        return 6;
    }
    if (doom_shape_nodes[1].link != &doom_shape_nodes[0] || doom_shape_nodes[2].link != &doom_shape_nodes[2] || doom_shape_nodes[0].link != 0 ||
        doom_shape_nodes[2].value != 3)
    {
        return 8;
    }
    for (index = 0; index < (int)(sizeof(doom_shape_imported_keys) / sizeof(*doom_shape_imported_keys)); index += 1)
    {
        if (doom_shape_imported_keys[index] == 0)
        {
            return 9;
        }
    }
    if (*doom_shape_imported_keys[0] != 11 || *doom_shape_imported_keys[1] != 22 || *doom_shape_imported_keys[2] != 33)
    {
        return 9;
    }
    return 0;
}
