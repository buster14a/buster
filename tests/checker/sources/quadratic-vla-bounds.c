int f(int n) { int a[n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n][n]; return sizeof a > 0; }
int g(int n) { int a0[n], a1[n], a2[n], a3[n], a4[n], a5[n], a6[n], a7[n], a8[n], a9[n], a10[n], a11[n], a12[n], a13[n], a14[n], a15[n], a16[n], a17[n], a18[n], a19[n], a20[n], a21[n], a22[n], a23[n], a24[n], a25[n], a26[n], a27[n], a28[n], a29[n], a30[n], a31[n], a32[n], a33[n], a34[n], a35[n], a36[n], a37[n], a38[n], a39[n], a40[n], a41[n], a42[n], a43[n], a44[n], a45[n], a46[n], a47[n], a48[n], a49[n], a50[n], a51[n], a52[n], a53[n], a54[n], a55[n], a56[n], a57[n], a58[n], a59[n], a60[n], a61[n], a62[n], a63[n], a64[n], a65[n], a66[n], a67[n], a68[n], a69[n], a70[n], a71[n], a72[n], a73[n], a74[n], a75[n], a76[n], a77[n], a78[n], a79[n], a80[n], a81[n], a82[n], a83[n], a84[n], a85[n], a86[n], a87[n], a88[n], a89[n], a90[n], a91[n], a92[n], a93[n], a94[n], a95[n], a96[n], a97[n], a98[n], a99[n]; return sizeof a0 + sizeof a99; }
int h(int n)
{
    int r = 0;
    {
        int b[n][n + 1][2];
        {
            int c[n][n][n][n], d[n], e[2][n];
            r += sizeof b + sizeof c + sizeof d + sizeof e;
        }
        for (int i = 1; i < n; i++) { int e[i][n]; r += sizeof e; if (i == 3) break; }
        r += ({ int g[n][n]; (int)sizeof g; });
    }
    return r;
}
int tested(int n) { return f(1) + g(2 + (n & 3)) + h(3 + (n & 1)); }
