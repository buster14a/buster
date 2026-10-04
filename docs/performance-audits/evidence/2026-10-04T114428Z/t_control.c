int f(int* p, int n, int mode)
{
    int sum = 0;
    for (int i = 0; i < n; i += 1)
    {
        if (p[i] < 0)
        {
            continue;
        }
        switch (mode)
        {
        case 0:
            sum += p[i];
            break;
        case 1:
            sum -= p[i];
        case 2:
            sum ^= p[i];
            break;
        default:
            goto done;
        }
        while (sum > 1000)
        {
            sum /= 2;
            if (sum == 7)
            {
                break;
            }
        }
    }
done:
    return sum;
}
