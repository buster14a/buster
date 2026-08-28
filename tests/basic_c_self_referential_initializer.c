// `quickjs.c` heads its atomics waiter list with
// `static struct list_head js_atomics_waiter_list =
//  LIST_HEAD_INIT(js_atomics_waiter_list);`, which expands to an initializer
// naming the object it initializes.  The declaration's name scan keeps the
// last matching identifier so that `struct x x;` names the object and not the
// tag; without stopping at the initializer it took the self-reference for the
// declarator, parsed the type from inside the braces and bound no entity at
// all -- the object was silently missing from the output.
struct list_head
{
    struct list_head *previous;
    struct list_head *next;
};

#define LIST_HEAD_INIT(element) {&(element), &(element)}

static struct list_head waiter_list = LIST_HEAD_INIT(waiter_list);
struct list_head external_list = LIST_HEAD_INIT(external_list);

struct node
{
    struct node *self;
    int value;
};

static struct node self_node = {&self_node, 5};

// The tag and the object may still share a name.
struct shared
{
    int value;
};
struct shared shared = {11};

static int list_is_empty(const struct list_head *head)
{
    return head->next == head;
}

int main(void)
{
    if (!list_is_empty(&waiter_list)) return 1;
    if (waiter_list.previous != &waiter_list) return 2;
    if (!list_is_empty(&external_list)) return 3;
    if (self_node.self != &self_node || self_node.value != 5) return 4;
    if (self_node.self->self->value != 5) return 5;
    if (shared.value != 11) return 6;
    return 0;
}
