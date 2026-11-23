#define N 4
#define PAR parallel
#pragma omp PAR num_threads(N)
_Pragma("omp for N")
#pragma ppf N
#pragma STDC FP_CONTRACT N
