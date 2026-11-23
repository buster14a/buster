int input;
float rejected = (int *){ input };

void compound_static_relaxed(void)
{
    float dependent = (int *){ input };
}
