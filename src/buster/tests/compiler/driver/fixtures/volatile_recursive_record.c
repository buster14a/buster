struct Node { volatile struct Node *next, *prev; };
struct Right;
struct Left { const volatile struct Right *next; int value; };
struct Right { volatile struct Left *prev; int value; };
union Link { volatile union Link *next; unsigned long bits; };

static void unlink_nodes(volatile struct Node *prev, volatile struct Node *next)
{
    next->prev = prev;
    prev->next = next;
}

static void connect(volatile struct Left *left, volatile struct Right *right)
{
    left->next = right;
    right->prev = left;
    left->value = right->value;
}

static void connect_union(volatile union Link *left, volatile union Link *right)
{
    left->next = right;
}

int main(void)
{
    struct Node a = {0}, b = {0};
    struct Left left = {0};
    struct Right right = {0};
    union Link first = {0}, second = {0};
    right.value = 7;
    unlink_nodes(&a, &b);
    connect(&left, &right);
    connect_union(&first, &second);
    return a.next != &b || b.prev != &a || left.next != &right ||
           right.prev != &left || left.value != 7 || first.next != &second;
}
