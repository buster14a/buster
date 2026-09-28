// Compiled with -fno-pic, the read below is a rel32 to data another module
// defines, which a position-independent image cannot hold.
extern int non_pic_external;

int non_pic_read(void)
{
    return non_pic_external;
}
