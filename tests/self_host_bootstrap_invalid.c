// Must produce a structured diagnostic, not a crash, timeout or executable.
int main(void)
{
    return bootstrap_undeclared_identifier;
}
