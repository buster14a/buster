#include "cJSON.h"
extern int printf(const char *, ...);
typedef unsigned long long U64;
static U64 parse(const char *s) { U64 x=0; while(*s>='0' && *s<='9') { x=x*10u+(U64)(*s-'0'); ++s; } return x; }
int main(int argc,char **argv) {
    const char *json="{\"items\":[{\"name\":\"Buster\",\"id\":14,\"enabled\":true},{\"name\":\"CCC\",\"id\":37,\"enabled\":false}],\"values\":[1,2,3,4,8,16,32,64,128,256],\"nested\":{\"null\":null,\"message\":\"compiler comparison\",\"binary_fraction\":1.25}}";
    U64 reps=argc>1?parse(argv[1]):1u,hash=0,r;
    int status = 0;
    for(r=0;r<reps && status==0;++r) {
        cJSON *root=cJSON_Parse(json);
        char *out; const unsigned char *p;
        if(!root) { status=2; }
        else {
            out=cJSON_PrintUnformatted(root);
            if(!out) { status=3; }
            else {
                p=(const unsigned char *)out;
                while(*p) { hash=(hash^*p)*1099511628211ull; ++p; }
                cJSON_free(out);
            }
            cJSON_Delete(root);
        }
    }
    if(status==0) { printf("%016llx\n",hash); }
    return status;
}
