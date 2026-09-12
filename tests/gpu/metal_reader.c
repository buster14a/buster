// Independent Metal framework consumer. Load the library and create the named
// compute pipeline without submitting GPU work. C uses the public ObjC runtime
// with typed message signatures; no Objective-C frontend is required.
#include <objc/message.h>
#include <objc/runtime.h>
#include <stdio.h>
extern id MTLCreateSystemDefaultDevice(void);

static id send_no_args(id object, char const* selector)
{
    return ((id (*)(id, SEL))objc_msgSend)(object, sel_registerName(selector));
}

static void send_void(id object, char const* selector)
{
    ((void (*)(id, SEL))objc_msgSend)(object, sel_registerName(selector));
}

static id make_string(char const* text)
{
    return ((id (*)(id, SEL, char const*))objc_msgSend)((id)objc_getClass("NSString"),
        sel_registerName("stringWithUTF8String:"), text);
}

int main(int argc, char** argv)
{
    int result = 1;
    id pool = send_no_args(send_no_args((id)objc_getClass("NSAutoreleasePool"), "alloc"), "init");
    id device = MTLCreateSystemDefaultDevice();
    if (argc == 2 && device)
    {
        id error = nil;
        id library = ((id (*)(id, SEL, id, id*))objc_msgSend)(device,
            sel_registerName("newLibraryWithFile:error:"), make_string(argv[1]), &error);
        id function = library ? ((id (*)(id, SEL, id))objc_msgSend)(library,
            sel_registerName("newFunctionWithName:"), make_string("buster_gpu_smoke")) : nil;
        id pipeline = function ? ((id (*)(id, SEL, id, id*))objc_msgSend)(device,
            sel_registerName("newComputePipelineStateWithFunction:error:"), function, &error) : nil;
        if (pipeline)
        {
            puts("METAL_CONSUMER accepted buster_gpu_smoke");
            result = 0;
        }
        else if (error)
        {
            id description = send_no_args(error, "localizedDescription");
            char const* message = ((char const* (*)(id, SEL))objc_msgSend)(description, sel_registerName("UTF8String"));
            fprintf(stderr, "Metal consumer: %s\n", message ? message : "no diagnostic");
        }
        send_void(pipeline, "release");
        send_void(function, "release");
        send_void(library, "release");
    }
    else { fputs("Metal consumer requires one library path and a Metal device\n", stderr); }
    send_void(device, "release");
    send_void(pool, "drain");
    return result;
}
