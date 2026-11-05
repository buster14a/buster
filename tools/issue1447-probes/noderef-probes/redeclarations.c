#define ND __attribute__((noderef))
typedef int ND *NP;
extern NP earlier_nd;
extern int *earlier_nd;
extern int *later_nd;
extern NP later_nd;
int *return_later(void);
NP return_later(void);
NP return_earlier(void);
int *return_earlier(void);
void parameter_earlier(NP p);
void parameter_earlier(int *p) { (void)*p; }
void parameter_later(int *p);
void parameter_later(NP p) { (void)*p; }
void declarations(void)
{
    (void)*earlier_nd;
    (void)*later_nd;
    (void)*return_later();
    (void)*return_earlier();
}
