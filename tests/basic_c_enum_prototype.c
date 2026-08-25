typedef signed long UNITY_INT64;
typedef UNITY_INT64 UNITY_INT;
typedef enum
{
    UNITY_DISPLAY_STYLE_INT = sizeof(int) + (0x10),
} UNITY_DISPLAY_STYLE_T;

static int failed;

void UnityPrintNumberByStyle(const UNITY_INT number, const UNITY_DISPLAY_STYLE_T style);

void UnityPrintNumberByStyle(const UNITY_INT number, const UNITY_DISPLAY_STYLE_T style)
{
    if (number != 7 || style != UNITY_DISPLAY_STYLE_INT)
    {
        failed = 1;
    }
}

int main(void)
{
    UnityPrintNumberByStyle(7, UNITY_DISPLAY_STYLE_INT);
    return failed;
}
