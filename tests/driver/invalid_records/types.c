struct Forward;
enum ForwardEnum;
typedef void Nothing;
typedef int Function(void);
struct Types
{
    Nothing nothing;
    struct Forward forward;
    enum ForwardEnum enumeration;
    Function function;
    int direct(void);
    int after;
};
int tail = missing_after_types;
int use_invalid(struct Types *p)
{
    return p->nothing + p->forward.anything + p->enumeration + p->function() + missing_in_use;
}
struct FirstIncomplete { struct Later field; int tail; };
struct Later { int value; };
int use_earlier(struct FirstIncomplete *p)
{
    return p->field.value + missing_after_completion;
}
