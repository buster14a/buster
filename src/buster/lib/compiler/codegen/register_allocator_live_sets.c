// Included by register_allocator_fast.c. Persistent value-liveness sets for
// large block/value populations: binary nodes share unchanged subtrees, and
// zero/full subtrees need no storage. Small populations keep dense bitsets.
// All traversals use stacks bounded by the u32 index width; no CFG recursion.
#define MACHINE_FAST_LIVE_PAGE_BITS 10u
#define MACHINE_FAST_LIVE_PAGE_SIZE (1u << MACHINE_FAST_LIVE_PAGE_BITS)
#define MACHINE_FAST_LIVE_PAGE_MASK (MACHINE_FAST_LIVE_PAGE_SIZE - 1u)
#define MACHINE_FAST_LIVE_FULL UINT32_MAX
#define MACHINE_FAST_LIVE_DEPTH_LIMIT 32u
// At most 64 MiB of nodes; unrelated large sets must not grow without bound.
#define MACHINE_FAST_LIVE_NODE_LIMIT (1u << 23)
BUSTER_CT_CHECK(3u * MACHINE_FAST_LIVE_PAGE_BITS < 32u);

typedef union MachineFastLiveNode MachineFastLiveNode;
union MachineFastLiveNode
{
    u64 bits;
    u32 children[2];
};

struct MachineFastLiveSets
{
    Arena* arena;
    MachineFastLiveNode*** pages;
    u32* in;
    u32* out;
    u32 height;
    u32 node_count;
    u32 node_limit;
    bool exhausted;
    u64 visits;
};

BUSTER_GLOBAL_LOCAL MachineFastLiveSets* machine_fast_live_sets_create(Arena* arena, u32 block_count, u32 words)
{
    MachineFastLiveSets* sets = arena_allocate(arena, MachineFastLiveSets, 1);
    *sets = (MachineFastLiveSets){.arena = arena, .node_limit = MACHINE_FAST_LIVE_NODE_LIMIT};
    for (u32 width = 1; width < words; width *= 2u)
    {
        sets->height += 1;
    }
    BUSTER_RT_CHECK(sets->height < MACHINE_FAST_LIVE_DEPTH_LIMIT);
    sets->pages = arena_allocate(arena, MachineFastLiveNode**, MACHINE_FAST_LIVE_PAGE_SIZE);
    memset(sets->pages, 0, (u64)MACHINE_FAST_LIVE_PAGE_SIZE * sizeof(*sets->pages));
    sets->in = arena_allocate(arena, u32, (u64)block_count * 2u);
    sets->out = sets->in + block_count;
    memset(sets->in, 0, (u64)block_count * 2u * sizeof(*sets->in));
    return sets;
}

BUSTER_GLOBAL_LOCAL MachineFastLiveNode* machine_fast_live_node(MachineFastLiveSets const* sets, u32 id)
{
    u32 index = id - 1u;
    return sets->pages[index >> (2u * MACHINE_FAST_LIVE_PAGE_BITS)]
                      [(index >> MACHINE_FAST_LIVE_PAGE_BITS) & MACHINE_FAST_LIVE_PAGE_MASK] +
           (index & MACHINE_FAST_LIVE_PAGE_MASK);
}

BUSTER_GLOBAL_LOCAL u32 machine_fast_live_append(MachineFastLiveSets* sets, MachineFastLiveNode node)
{
    u32 result = 0;
    if (sets->node_count >= sets->node_limit)
    {
        sets->exhausted = true;
    }
    else
    {
        u32 index = sets->node_count++;
        u32 outer = index >> (2u * MACHINE_FAST_LIVE_PAGE_BITS);
        u32 page = (index >> MACHINE_FAST_LIVE_PAGE_BITS) & MACHINE_FAST_LIVE_PAGE_MASK;
        BUSTER_RT_CHECK(outer < MACHINE_FAST_LIVE_PAGE_SIZE);
        if (!sets->pages[outer])
        {
            sets->pages[outer] = arena_allocate(sets->arena, MachineFastLiveNode*, MACHINE_FAST_LIVE_PAGE_SIZE);
            memset(sets->pages[outer], 0, (u64)MACHINE_FAST_LIVE_PAGE_SIZE * sizeof(*sets->pages[outer]));
        }
        if (!sets->pages[outer][page])
        {
            sets->pages[outer][page] = arena_allocate(sets->arena, MachineFastLiveNode, MACHINE_FAST_LIVE_PAGE_SIZE);
        }
        sets->pages[outer][page][index & MACHINE_FAST_LIVE_PAGE_MASK] = node;
        result = index + 1u;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u32 machine_fast_live_child(MachineFastLiveSets const* sets, u32 root, u32 side)
{
    return root == 0 || root == MACHINE_FAST_LIVE_FULL ? root : machine_fast_live_node(sets, root)->children[side];
}

BUSTER_GLOBAL_LOCAL u64 machine_fast_live_bits(MachineFastLiveSets const* sets, u32 root)
{
    return root == 0 ? 0 : root == MACHINE_FAST_LIVE_FULL ? UINT64_MAX : machine_fast_live_node(sets, root)->bits;
}

BUSTER_GLOBAL_LOCAL u32 machine_fast_live_leaf(MachineFastLiveSets* sets, u64 bits, u32 first, u32 second)
{
    u32 result;
    if (bits == 0 || bits == UINT64_MAX)
    {
        result = bits ? MACHINE_FAST_LIVE_FULL : 0;
    }
    else if (bits == machine_fast_live_bits(sets, first))
    {
        result = first;
    }
    else if (bits == machine_fast_live_bits(sets, second))
    {
        result = second;
    }
    else
    {
        result = machine_fast_live_append(sets, (MachineFastLiveNode){.bits = bits});
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u32 machine_fast_live_branch(MachineFastLiveSets* sets, u32 left, u32 right, u32 first, u32 second)
{
    u32 result;
    if (left == right && (left == 0 || left == MACHINE_FAST_LIVE_FULL))
    {
        result = left;
    }
    else if (left == machine_fast_live_child(sets, first, 0) && right == machine_fast_live_child(sets, first, 1))
    {
        result = first;
    }
    else if (left == machine_fast_live_child(sets, second, 0) && right == machine_fast_live_child(sets, second, 1))
    {
        result = second;
    }
    else
    {
        result = machine_fast_live_append(sets, (MachineFastLiveNode){.children = {left, right}});
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u32 machine_fast_live_update(MachineFastLiveSets* sets, u32 root, u32 value, bool add)
{
    u32 path[MACHINE_FAST_LIVE_DEPTH_LIMIT];
    u32 word = value / 64u;
    u32 cursor = root;
    for (u32 height = sets->height; height; height -= 1u)
    {
        path[height] = cursor;
        cursor = machine_fast_live_child(sets, cursor, (word >> (height - 1u)) & 1u);
    }
    u64 bits = machine_fast_live_bits(sets, cursor);
    u64 bit = UINT64_C(1) << (value % 64u);
    u32 result = machine_fast_live_leaf(sets, add ? bits | bit : bits & ~bit, cursor, 0);
    for (u32 height = 1; height <= sets->height; height += 1u)
    {
        u32 side = (word >> (height - 1u)) & 1u;
        u32 other = machine_fast_live_child(sets, path[height], side ^ 1u);
        result = machine_fast_live_branch(sets, side ? other : result, side ? result : other, path[height], 0);
    }
#if BUSTER_WORK_LEDGER
    sets->visits += sets->height + 1u;
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL bool machine_fast_live_contains(MachineFastLiveSets const* sets, u32 root, u32 value)
{
    u32 word = value / 64u;
    for (u32 height = sets->height; height && root && root != MACHINE_FAST_LIVE_FULL; height -= 1u)
    {
        root = machine_fast_live_child(sets, root, (word >> (height - 1u)) & 1u);
    }
    return ((machine_fast_live_bits(sets, root) >> (value % 64u)) & 1u) != 0;
}

typedef struct MachineFastLiveApply MachineFastLiveApply;
struct MachineFastLiveApply
{
    u32 first;
    u32 second;
    u32 left;
    u32 height;
    u32 stage;
};

// Union, or first minus second. A subtree that is unchanged reuses its old
// ID, so root identity is an exact no-change test for the monotone worklist.
BUSTER_GLOBAL_LOCAL u32 machine_fast_live_apply(MachineFastLiveSets* sets, u32 first, u32 second, bool subtract)
{
    MachineFastLiveApply stack[MACHINE_FAST_LIVE_DEPTH_LIMIT];
    stack[0] = (MachineFastLiveApply){.first = first, .second = second, .height = sets->height};
    u32 depth = 1;
    u32 result = 0;
    while (depth && !sets->exhausted)
    {
        MachineFastLiveApply* frame = stack + depth - 1u;
        u32 a = frame->first;
        u32 b = frame->second;
        if (frame->stage == 0)
        {
#if BUSTER_WORK_LEDGER
            sets->visits += 1;
#endif
            bool done = true;
            if (subtract && (a == 0 || b == MACHINE_FAST_LIVE_FULL || a == b))
            {
                result = 0;
            }
            else if (b == 0 || (!subtract && a == b))
            {
                result = a;
            }
            else if (!subtract && a == 0)
            {
                result = b;
            }
            else if (!subtract && (a == MACHINE_FAST_LIVE_FULL || b == MACHINE_FAST_LIVE_FULL))
            {
                result = MACHINE_FAST_LIVE_FULL;
            }
            else if (!frame->height)
            {
                u64 a_bits = machine_fast_live_bits(sets, a);
                u64 b_bits = machine_fast_live_bits(sets, b);
                result = machine_fast_live_leaf(sets, subtract ? a_bits & ~b_bits : a_bits | b_bits, a, b);
            }
            else
            {
                done = false;
                frame->stage = 1;
                stack[depth++] = (MachineFastLiveApply){
                    .first = machine_fast_live_child(sets, a, 0), .second = machine_fast_live_child(sets, b, 0), .height = frame->height - 1u};
            }
            if (done)
            {
                depth -= 1u;
            }
        }
        else if (frame->stage == 1)
        {
            frame->left = result;
            frame->stage = 2;
            stack[depth++] = (MachineFastLiveApply){
                .first = machine_fast_live_child(sets, a, 1), .second = machine_fast_live_child(sets, b, 1), .height = frame->height - 1u};
        }
        else
        {
            result = machine_fast_live_branch(sets, frame->left, result, a, b);
            depth -= 1u;
        }
    }
    return result;
}
