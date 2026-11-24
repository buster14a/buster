enum { A=sizeof("ab" "cd"), B=sizeof(L"hi"), C=sizeof(u"hi") }; _Static_assert(A==5 && B==12 && C==6, "strings");
