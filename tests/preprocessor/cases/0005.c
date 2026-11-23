#if 0 && (-1 > 0u) || (-2 > 0u)
case_0
#endif
#if (0 && (-1 > 0u)) + (-3 > 0u)
case_1
#endif
#if 1 ? (-1 > 0u) : (-2 > 0u)
case_2
#endif
#if 0 ? (-1 > 0u) : (-2 > 0u)
case_3
#endif
#if 0 ? (-1 > 0u) : 1 ? (0u < -2) : (-3 > 0u)
case_4
#endif
#if 1 ? (0 ? (-1 > 0u) : (-2 > 0u)) : (-3 > 0u)
case_5
#endif
#if (1 || (-1 > 0u)) && (0u < -2)
case_6
#endif
#if ((0u < -1) ? -2 : 3) > 0u
case_7
#endif
#if (-1 > 0u) && (-2 > 0u)
case_8
#endif
#if -'A' > 0u
case_9
#endif
