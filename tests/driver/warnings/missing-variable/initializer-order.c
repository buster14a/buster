int called(void);
int tentative;
extern int external_initializer = 1;
int nonconstant = called();
extern int external_nonconstant = called();
int conversion = 1.5;
int last_initialized = 2;
