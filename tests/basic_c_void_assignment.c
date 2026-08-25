typedef struct Node Node;
struct Node
{
    Node *next;
};

static int advance(Node **cursor)
{
    (void)(*cursor = (*cursor)->next);
    return *cursor != 0;
}

int main(void)
{
    Node first = {0};
    Node second = {0};
    first.next = &second;
    Node *cursor = &first;
    return advance(&cursor) && cursor == &second ? 0 : 1;
}
