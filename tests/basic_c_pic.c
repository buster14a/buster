int buster_pic_global;

int buster_pic_address(void)
{
    return (int)(long)&buster_pic_global;
}

int buster_pic_read(void)
{
    return buster_pic_global;
}

int main(void)
{
    return buster_pic_address();
}
