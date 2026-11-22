#define FOUR(arm) arm arm arm arm
#define BRANCH if (x == i++) { if (x & 1) break; continue; } else

void deep(int x)
{
    int i = 0;
    for (;;)
        FOUR(FOUR(FOUR(BRANCH))) break;
}

void nested(int x)
{
    switch (x)
    {
        case 1:
            do { if (x--) continue; } while (x);
            break;
    }
    while (x)
    {
        switch (x) { default: if (x--) break; continue; }
        for (; ({ if (x) continue; x; }); ) break;
        while (({ if (x) break; x; })) continue;
        do { if (x--) break; } while (({ if (x) continue; x; }));
    }
}
