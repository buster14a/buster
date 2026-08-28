// The headless DoomGeneric platform the test_doom compatibility harness runs.
//
// DoomGeneric leaves six functions to the platform -- DG_Init, DG_DrawFrame,
// DG_SleepMs, DG_GetTicksMs, DG_GetKey and DG_SetWindowTitle -- plus main().
// Every upstream backend fills them with a window system and a wall clock,
// which is exactly what a compiler test cannot use: the frame a window system
// produces depends on when it was asked for it. This backend replaces both
// with counters, so the whole program becomes a pure function of the IWAD and
// of the script below, and the Buster and Clang builds must print the same
// transcript byte for byte.
//
// The three things that make it deterministic:
//
//   * The clock is driven by the program instead of by the machine. Drawing a
//     frame advances it to the start of the next game tic, and a sleep
//     advances it by the milliseconds asked for -- Doom's inner loops wait for
//     the clock to move and would otherwise spin forever. Nothing else moves
//     it, so the run takes the same number of tics on any machine.
//   * Input is a table of (tic, pressed, key) rows rather than a device, and
//     the save/load/exit-level actions are called directly instead of being
//     steered through the menus, so a divergence lands on the tic that caused
//     it instead of on the first tic whose menu state drifted.
//   * DG_ScreenBuffer is cleared once before Doom writes into it. Upstream
//     mallocs it and never clears it, and I_FinishUpdate only writes the
//     letterboxed region, so hashing it unaltered would hash whatever the
//     allocator handed back.
//
// The transcript is one DOOM_TICK line per frame carrying a hash of the frame
// buffer and a hash of the simulation state, so a mismatch names the first
// divergent tic and says whether the pixels, the simulation, or both moved.
// Everything the harness compares is prefixed DOOM_; Doom's own startup output
// carries paths and is deliberately not part of the comparison.

#include "doomgeneric.h"

#include "d_event.h"
#include "d_main.h"
#include "doomdef.h"
#include "doomkeys.h"
#include "doomstat.h"
#include "g_game.h"
#include "i_system.h"
#include "i_video.h"
#include "info.h"
#include "m_argv.h"
#include "p_local.h"
#include "p_saveg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// m_random.c defines both random indices, but only the M_Random one reaches a
// header. prndindex is the play-simulation index -- the one a miscompiled
// monster or hitscan would move -- so it is declared here rather than left out
// of the state hash.
extern int prndindex;

#define HEADLESS_DEFAULT_TICS 480u
#define HEADLESS_SAVE_SLOT 0
#define HEADLESS_KEY_QUEUE 64u

// FNV-1a over single bytes. A word-wise digest could cancel a byte-order bug
// against itself; a byte-at-a-time one is defined on the byte sequence and
// cannot, which is the point of hashing a frame buffer at all.
static unsigned long long headless_digest_bytes(unsigned long long hash, const void* data, unsigned long long size)
{
    const unsigned char* bytes = (const unsigned char*)data;
    unsigned long long index;
    for (index = 0; index < size; index += 1)
    {
        hash ^= (unsigned long long)bytes[index];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static unsigned long long headless_digest_value(unsigned long long hash, long long value)
{
    unsigned char encoded[8];
    unsigned long long bits = (unsigned long long)value;
    unsigned index;
    for (index = 0; index < 8; index += 1)
    {
        encoded[index] = (unsigned char)((bits >> (index * 8)) & 0xffu);
    }
    return headless_digest_bytes(hash, encoded, sizeof(encoded));
}

#define HEADLESS_DIGEST_START 14695981039346656037ULL

// One scripted key. The queue is drained by DG_GetKey exactly the way a real
// backend drains a device queue, so i_input.c sees the same shape of input it
// would see from a window system.
typedef struct HeadlessKey HeadlessKey;
struct HeadlessKey
{
    unsigned tic;
    int pressed;
    unsigned char key;
};

// Movement, turning, firing, the use key, the menu and the automap, in that
// order: the point is to reach the code that a title-screen-only run never
// reaches -- P_Move, the hitscan attack path, the door specials, m_menu.c and
// am_map.c -- with a fixed sequence.
static const HeadlessKey headless_script[] = {
    {12, 1, KEY_UPARROW},    {45, 0, KEY_UPARROW},

    {50, 1, KEY_RIGHTARROW}, {62, 0, KEY_RIGHTARROW},

    {70, 1, KEY_FIRE},       {74, 0, KEY_FIRE},

    {80, 1, KEY_UPARROW},    {120, 0, KEY_UPARROW},

    {125, 1, KEY_USE},       {128, 0, KEY_USE},

    {140, 1, KEY_ESCAPE},    {142, 0, KEY_ESCAPE},
    {148, 1, KEY_DOWNARROW}, {150, 0, KEY_DOWNARROW},
    {152, 1, KEY_DOWNARROW}, {154, 0, KEY_DOWNARROW},
    {158, 1, KEY_ESCAPE},    {160, 0, KEY_ESCAPE},

    {165, 1, KEY_TAB},       {167, 0, KEY_TAB},
    {180, 1, KEY_TAB},       {182, 0, KEY_TAB},

    {200, 1, KEY_UPARROW},   {240, 0, KEY_UPARROW},
    {235, 1, KEY_FIRE},      {245, 0, KEY_FIRE},

    {275, 1, KEY_UPARROW},   {295, 0, KEY_UPARROW},

    // The intermission advances on the use/attack buttons in the player's
    // ticcmd, not on a keyboard scan, so these are what walk it through the
    // kill/item/secret counters and into the next level.
    {315, 1, KEY_USE},       {318, 0, KEY_USE},
    {325, 1, KEY_USE},       {328, 0, KEY_USE},
    {335, 1, KEY_USE},       {338, 0, KEY_USE},
    {345, 1, KEY_USE},       {348, 0, KEY_USE},
    {355, 1, KEY_USE},       {358, 0, KEY_USE},
    {365, 1, KEY_USE},       {368, 0, KEY_USE},

    {420, 1, KEY_UPARROW},   {450, 0, KEY_UPARROW},
    {455, 1, KEY_FIRE},      {460, 0, KEY_FIRE},
};

typedef enum HeadlessActionKind
{
    HEADLESS_ACTION_SAVE,
    HEADLESS_ACTION_MEASURE_SAVE_FILE,
    HEADLESS_ACTION_LOAD,
    HEADLESS_ACTION_EXIT_LEVEL,
} HeadlessActionKind;

typedef struct HeadlessAction HeadlessAction;
struct HeadlessAction
{
    unsigned tic;
    HeadlessActionKind kind;
};

// G_SaveGame and G_LoadGame only set gameaction; the work happens at the top
// of the next tic. Both sit outside the key script above, so that no held key
// is folded into the tic that carries out the save or the load.
static const HeadlessAction headless_actions[] = {
    {190, HEADLESS_ACTION_SAVE},
    {195, HEADLESS_ACTION_MEASURE_SAVE_FILE},
    {250, HEADLESS_ACTION_LOAD},
    {300, HEADLESS_ACTION_EXIT_LEVEL},
};

static unsigned headless_tic_limit = HEADLESS_DEFAULT_TICS;
static unsigned headless_frames = 0;
static unsigned long long headless_clock_ms = 0;
static int headless_video_cleared = 0;
static unsigned headless_sleeps = 0;
static unsigned headless_titles = 0;
static unsigned headless_keys_delivered = 0;
static unsigned long long headless_frame_chain = HEADLESS_DIGEST_START;
static unsigned long long headless_state_chain = HEADLESS_DIGEST_START;
static unsigned long long headless_saved_world = 0;
static int headless_saved_world_valid = 0;
// The save/load check, and the two pieces of Doom's plumbing it has to work
// around.
//
// G_SaveGame does not save: it raises `sendsave`, which travels through the
// next ticcmd and only then becomes gameaction = ga_savegame, which the tic
// after that carries out. The world that reaches the file is therefore the one
// standing while gameaction is still ga_savegame, which is where it is hashed
// below -- not the one standing when the save was asked for.
//
// G_LoadGame does set gameaction directly, but the tic that carries the load
// out also runs the world forward afterwards, which would hide what p_saveg.c
// restored behind one tic of simulation. P_Ticker returns early while the menu
// is up, so the platform raises `menuactive` across the load and drops it
// again once the comparison is made.
static int headless_save_requested = 0;
static int headless_saved_leveltime = 0;
static int headless_load_pending = 0;

static HeadlessKey headless_queue[HEADLESS_KEY_QUEUE];
static unsigned headless_queue_read = 0;
static unsigned headless_queue_write = 0;

// The simulation state, without anything that a second run could legitimately
// disagree about: no pointers, no addresses, no allocation order. Thinkers are
// walked in list order, which the save/load path preserves, and the state
// pointer is reduced to its index in the states[] table so that a wrong
// pointer subtraction still shows up.
static unsigned long long headless_world_hash(void)
{
    unsigned long long hash = HEADLESS_DIGEST_START;
    const player_t* player = &players[consoleplayer];
    thinker_t* thinker;
    unsigned long long mobj_count = 0;
    int index;

    hash = headless_digest_value(hash, leveltime);
    hash = headless_digest_value(hash, gameepisode);
    hash = headless_digest_value(hash, gamemap);
    hash = headless_digest_value(hash, (long long)gameskill);
    hash = headless_digest_value(hash, totalkills);
    hash = headless_digest_value(hash, totalitems);
    hash = headless_digest_value(hash, totalsecret);

    hash = headless_digest_value(hash, player->health);
    hash = headless_digest_value(hash, player->armorpoints);
    hash = headless_digest_value(hash, player->armortype);
    hash = headless_digest_value(hash, (long long)player->readyweapon);
    hash = headless_digest_value(hash, (long long)player->pendingweapon);
    hash = headless_digest_value(hash, player->killcount);
    hash = headless_digest_value(hash, player->itemcount);
    hash = headless_digest_value(hash, player->secretcount);
    hash = headless_digest_value(hash, player->cheats);
    hash = headless_digest_value(hash, player->refire);
    hash = headless_digest_value(hash, player->damagecount);
    hash = headless_digest_value(hash, player->bonuscount);
    hash = headless_digest_value(hash, player->extralight);
    hash = headless_digest_value(hash, player->viewz);
    hash = headless_digest_value(hash, player->viewheight);
    hash = headless_digest_value(hash, player->deltaviewheight);
    hash = headless_digest_value(hash, player->bob);
    for (index = 0; index < NUMAMMO; index += 1)
    {
        hash = headless_digest_value(hash, player->ammo[index]);
        hash = headless_digest_value(hash, player->maxammo[index]);
    }
    for (index = 0; index < NUMWEAPONS; index += 1)
    {
        hash = headless_digest_value(hash, player->weaponowned[index]);
    }
    for (index = 0; index < NUMCARDS; index += 1)
    {
        hash = headless_digest_value(hash, player->cards[index]);
    }

    for (thinker = thinkercap.next; thinker != NULL && thinker != &thinkercap; thinker = thinker->next)
    {
        const mobj_t* mobj;
        if (thinker->function.acp1 != (actionf_p1)P_MobjThinker)
        {
            continue;
        }
        mobj = (const mobj_t*)thinker;
        mobj_count += 1;
        hash = headless_digest_value(hash, mobj->x);
        hash = headless_digest_value(hash, mobj->y);
        hash = headless_digest_value(hash, mobj->z);
        hash = headless_digest_value(hash, (long long)mobj->angle);
        hash = headless_digest_value(hash, mobj->momx);
        hash = headless_digest_value(hash, mobj->momy);
        hash = headless_digest_value(hash, mobj->momz);
        hash = headless_digest_value(hash, (long long)mobj->type);
        hash = headless_digest_value(hash, mobj->health);
        hash = headless_digest_value(hash, mobj->tics);
        hash = headless_digest_value(hash, (long long)mobj->flags);
        hash = headless_digest_value(hash, mobj->movedir);
        hash = headless_digest_value(hash, mobj->movecount);
        hash = headless_digest_value(hash, mobj->reactiontime);
        hash = headless_digest_value(hash, mobj->threshold);
        hash = headless_digest_value(hash, mobj->state ? (long long)(mobj->state - states) : -1);
    }
    hash = headless_digest_value(hash, (long long)mobj_count);
    return hash;
}

// Everything above plus the parts that a load is not expected to restore:
// gametic keeps counting across a load, and the two random indices are not in
// a vanilla savegame at all.
static unsigned long long headless_state_hash(void)
{
    unsigned long long hash = headless_world_hash();
    hash = headless_digest_value(hash, gametic);
    hash = headless_digest_value(hash, (long long)gamestate);
    hash = headless_digest_value(hash, (long long)gameaction);
    hash = headless_digest_value(hash, menuactive);
    hash = headless_digest_value(hash, automapactive);
    hash = headless_digest_value(hash, paused);
    hash = headless_digest_value(hash, rndindex);
    hash = headless_digest_value(hash, prndindex);
    return hash;
}

// I_InitGraphics allocates I_VideoBuffer out of the zone and hands it to Doom
// without clearing it, and the first frame is a screen wipe whose source is
// whatever that buffer already held -- recycled zone blocks, which still carry
// the heap addresses of the free-list headers that used to live there. Those
// addresses move between runs of one binary, so the first frames would hash
// differently every time.
//
// DoomGeneric offers no hook between that allocation and the first frame, so
// the clearing rides on the first clock read that happens after the buffer
// exists: TryRunTics reads the clock before anything draws, which makes it the
// earliest point the platform is given.
static void headless_clear_video_buffer(void)
{
    if (headless_video_cleared || I_VideoBuffer == NULL)
    {
        return;
    }
    memset(I_VideoBuffer, 0, (size_t)SCREENWIDTH * (size_t)SCREENHEIGHT);
    headless_video_cleared = 1;
}

// The first millisecond that belongs to the tic after the one `ms` is in.
static unsigned long long headless_next_tic_ms(unsigned long long ms)
{
    unsigned long long tic = (ms * (unsigned long long)TICRATE) / 1000ULL + 1ULL;
    return (tic * 1000ULL + (unsigned long long)TICRATE - 1ULL) / (unsigned long long)TICRATE;
}

static void headless_queue_key(int pressed, unsigned char key)
{
    headless_queue[headless_queue_write % HEADLESS_KEY_QUEUE].pressed = pressed;
    headless_queue[headless_queue_write % HEADLESS_KEY_QUEUE].key = key;
    headless_queue_write += 1;
}

// The savegame is read back rather than assumed to exist: the size is a
// property of p_saveg.c's field-by-field serialization, so a wrong field width
// or a missing field moves it.
//
// The contents are deliberately not hashed. A vanilla savegame stores
// mobj_t.target and mobj_t.tracer as the raw pointer values they had when the
// game was saved, which differ between two runs of the same binary; the bytes
// are therefore not a compiler property at all. What the savegame actually
// restored is checked by the leveltime-keyed comparison below, which reads the
// world back out of the game rather than out of the file.
static void headless_measure_save_file(void)
{
    char* path = P_SaveGameFile(HEADLESS_SAVE_SLOT);
    unsigned long long size = 0;
    unsigned char buffer[4096];
    FILE* stream = fopen(path, "rb");
    if (stream == NULL)
    {
        printf("DOOM_SAVEFILE status=missing\n");
        return;
    }
    for (;;)
    {
        size_t read = fread(buffer, 1, sizeof(buffer), stream);
        if (read == 0)
        {
            break;
        }
        size += (unsigned long long)read;
    }
    fclose(stream);
    printf("DOOM_SAVEFILE bytes=%llu\n", size);
}

// Hashes the world that is about to be written, and later the world that was
// read back. The second frame is the first one drawn after the load has been
// carried out; the simulation is still frozen there, so what it observes is
// exactly what p_saveg.c rebuilt, and it must equal the first hash.
static void headless_check_save_load(unsigned tic)
{
    unsigned long long world;
    if (gamestate != GS_LEVEL)
    {
        return;
    }
    if (headless_save_requested && !headless_saved_world_valid && gameaction == ga_savegame)
    {
        headless_saved_world = headless_world_hash();
        headless_saved_leveltime = leveltime;
        headless_saved_world_valid = 1;
        printf("DOOM_SAVEPOINT tic=%u leveltime=%d world=%016llx\n", tic, leveltime, headless_saved_world);
        return;
    }
    if (headless_load_pending && leveltime == headless_saved_leveltime)
    {
        world = headless_world_hash();
        printf("DOOM_SAVELOAD tic=%u leveltime=%d saved=%016llx loaded=%016llx match=%d\n", tic, leveltime, headless_saved_world, world,
               world == headless_saved_world);
        headless_load_pending = 0;
        menuactive = false;
    }
}

static void headless_run_actions(unsigned tic)
{
    unsigned index;
    for (index = 0; index < sizeof(headless_actions) / sizeof(headless_actions[0]); index += 1)
    {
        if (headless_actions[index].tic != tic)
        {
            continue;
        }
        switch (headless_actions[index].kind)
        {
            case HEADLESS_ACTION_SAVE:
            {
                G_SaveGame(HEADLESS_SAVE_SLOT, "buster headless");
                headless_save_requested = 1;
                printf("DOOM_EVENT tic=%u kind=save slot=%d leveltime=%d\n", tic, HEADLESS_SAVE_SLOT, leveltime);
            }
            break;
            case HEADLESS_ACTION_MEASURE_SAVE_FILE:
            {
                headless_measure_save_file();
            }
            break;
            case HEADLESS_ACTION_LOAD:
            {
                // Freeze the simulation across the load, so the next frame
                // sees the restored world and not the restored world plus a
                // tic. See headless_check_save_load.
                menuactive = true;
                G_LoadGame(P_SaveGameFile(HEADLESS_SAVE_SLOT));
                headless_load_pending = headless_saved_world_valid;
                printf("DOOM_EVENT tic=%u kind=load slot=%d leveltime=%d\n", tic, HEADLESS_SAVE_SLOT, leveltime);
            }
            break;
            case HEADLESS_ACTION_EXIT_LEVEL:
            {
                G_ExitLevel();
                printf("DOOM_EVENT tic=%u kind=exit-level\n", tic);
            }
            break;
        }
    }
}

void DG_Init(void)
{
    int parameter;

    // Upstream mallocs DG_ScreenBuffer immediately before calling this and
    // never clears it, and I_FinishUpdate writes only the scaled Doom region.
    // Clearing it here is what makes the frame hash a function of what Doom
    // drew rather than of what the allocator happened to return.
    memset(DG_ScreenBuffer, 0, (size_t)DOOMGENERIC_RESX * (size_t)DOOMGENERIC_RESY * sizeof(pixel_t));

    parameter = M_CheckParmWithArgs("-tics", 1);
    if (parameter > 0)
    {
        int requested = atoi(myargv[parameter + 1]);
        if (requested > 0)
        {
            headless_tic_limit = (unsigned)requested;
        }
    }
    printf("DOOM_PLATFORM backend=headless resx=%d resy=%d pixel_bytes=%u tics=%u script_keys=%u script_actions=%u\n", DOOMGENERIC_RESX, DOOMGENERIC_RESY,
           (unsigned)sizeof(pixel_t), headless_tic_limit, (unsigned)(sizeof(headless_script) / sizeof(headless_script[0])),
           (unsigned)(sizeof(headless_actions) / sizeof(headless_actions[0])));
}

void DG_DrawFrame(void)
{
    unsigned tic = headless_frames;
    unsigned long long frame = headless_digest_bytes(HEADLESS_DIGEST_START, DG_ScreenBuffer,
                                                     (unsigned long long)DOOMGENERIC_RESX * (unsigned long long)DOOMGENERIC_RESY * sizeof(pixel_t));
    unsigned long long state = headless_state_hash();
    unsigned index;

    headless_frame_chain = headless_digest_value(headless_frame_chain, (long long)frame);
    headless_state_chain = headless_digest_value(headless_state_chain, (long long)state);
    printf("DOOM_TICK tic=%u leveltime=%d frame=%016llx state=%016llx\n", tic, leveltime, frame, state);

    headless_check_save_load(tic);
    headless_run_actions(tic);
    for (index = 0; index < sizeof(headless_script) / sizeof(headless_script[0]); index += 1)
    {
        if (headless_script[index].tic == tic)
        {
            headless_queue_key(headless_script[index].pressed, headless_script[index].key);
        }
    }

    // Advance to the first millisecond of the next game tic. I_GetTime()
    // computes (ms * 35) / 1000, so the smallest ms belonging to tic n is
    // ceil(n * 1000 / 35): a drawn frame therefore moves Doom's clock forward
    // by exactly one tic and never lands mid-tic.
    headless_frames += 1;
    headless_clock_ms = headless_next_tic_ms(headless_clock_ms);
}

void DG_SleepMs(uint32_t ms)
{
    // Doom sleeps while it waits for the next tic to become available and
    // while a screen wipe runs, and both loops exit on the clock having moved.
    // A sleep therefore has to move it, or TryRunTics never returns; it moves
    // it by exactly what was asked for, so the wait is bounded and the number
    // of sleeps is itself a deterministic property of the run.
    headless_sleeps += 1;
    headless_clock_ms += ms ? (unsigned long long)ms : 1ULL;
}

uint32_t DG_GetTicksMs(void)
{
    headless_clear_video_buffer();
    return (uint32_t)headless_clock_ms;
}

int DG_GetKey(int* pressed, unsigned char* key)
{
    if (headless_queue_read == headless_queue_write)
    {
        return 0;
    }
    *pressed = headless_queue[headless_queue_read % HEADLESS_KEY_QUEUE].pressed;
    *key = headless_queue[headless_queue_read % HEADLESS_KEY_QUEUE].key;
    headless_queue_read += 1;
    headless_keys_delivered += 1;
    return 1;
}

void DG_SetWindowTitle(const char* title)
{
    (void)title;
    headless_titles += 1;
}

int main(int argc, char** argv)
{
    doomgeneric_Create(argc, argv);

    while (headless_frames < headless_tic_limit)
    {
        doomgeneric_Tick();
    }

    printf("DOOM_SUMMARY frames=%u gametic=%d sleeps=%u titles=%u keys=%u clock_ms=%llu frame_chain=%016llx state_chain=%016llx\n", headless_frames, gametic,
           headless_sleeps, headless_titles, headless_keys_delivered, headless_clock_ms, headless_frame_chain, headless_state_chain);
    printf("DOOM_FINAL episode=%d map=%d gamestate=%d leveltime=%d health=%d kills=%d/%d items=%d/%d secrets=%d/%d\n", gameepisode, gamemap, (int)gamestate,
           leveltime, players[consoleplayer].health, players[consoleplayer].killcount, totalkills, players[consoleplayer].itemcount, totalitems,
           players[consoleplayer].secretcount, totalsecret);
    fflush(stdout);

    // I_Quit runs the atexit list, which is what writes default.cfg back out;
    // the harness compares that file between the two builds, so the run has to
    // go through the ordinary shutdown path rather than just returning.
    I_Quit();
    return 0;
}
