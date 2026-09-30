extern __thread int tls_identity_value;

int main(void)
{
    if (tls_identity_value != 37)
    {
        return 1;
    }
    tls_identity_value += 5;
    return tls_identity_value == 42 ? 0 : 2;
}
