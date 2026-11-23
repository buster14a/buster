void after_loop(int x)
{
    switch (x)
    {
        default:
            for (;;) break;
            continue;
    }
}

void in_head(int x)
{
    while (({ if (x) break; x; })) break;
    for (; ({ if (x) continue; x; }); ) continue;
    switch (({ if (x) break; x; })) { default: break; }
    do { break; } while (({ if (x) continue; x; }));
}

void sibling(int x)
{
    if (x) while (x) break; else break;
}
