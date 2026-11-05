// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Pinned-Clang source-time symbol pragma probes for issue 1447.
#if CASE == 1
#pragma weak alias = value
int value=7;
int use(void){return alias;}
#elif CASE == 2
int value=7;
#pragma weak alias = value
int use(void){return alias;}
#elif CASE == 3
#pragma weak alias = value
int use(void){return alias;}
int value=7;
#elif CASE == 4
#pragma weak alias = missing
#elif CASE == 5
extern int alias;
#pragma weak alias = value
int value=7;
int use(void){return alias;}
#elif CASE == 6
double alias;
#pragma weak alias = value
int value=7;
#elif CASE == 7
static int value=7;
#pragma weak alias = value
#elif CASE == 8
#pragma weak alias = value
static int value=7;
#elif CASE == 9
#pragma weak alias = function
int function(int x){return x+7;}
int use(void){return alias(1);}
#elif CASE == 10
#pragma weak alias = middle
#pragma weak middle = value
int value=7;
int use(void){return alias+middle;}
#elif CASE == 11
int value=7;
int alias=9;
#pragma weak alias = value
#elif CASE == 12
extern int value;
#pragma weak alias = value
#elif CASE == 13
int value=7;
#pragma weak alias = value
#pragma weak alias = other
int other=9;
#elif CASE == 14
int value=7;
#pragma weak alias = value
extern int alias;
#elif CASE == 15
extern int alias(void);
#pragma weak alias = function
int function(int x){return x;}
#elif CASE == 16
#pragma weak value = value
int value=7;
#elif CASE == 17
extern int value;
int use(void){return value;}
#pragma redefine_extname value renamed
int value=7;
#elif CASE == 18
extern int value;
int use(void){if(0)return value; return 1;}
#pragma redefine_extname value renamed
int value=7;
#elif CASE == 19
extern int value;
int use(void){return 1; return value;}
#pragma redefine_extname value renamed
int value=7;
#elif CASE == 20
extern int value;
int use(void){return sizeof(value);}
#pragma redefine_extname value renamed
int value=7;
#elif CASE == 21
extern int value;
static int use(void){return value;}
#pragma redefine_extname value renamed
int value=7;
#elif CASE == 22
extern int value;
inline int use(void){return value;}
#pragma redefine_extname value renamed
int value=7;
#elif CASE == 23
extern int value;
static int use(void){return value;}
#pragma redefine_extname value renamed
int value=7;
int caller(void){return use();}
#elif CASE == 24
extern int value;
static int use(void){return value;}
int caller(void){return use();}
#pragma redefine_extname value renamed
int value=7;
#elif CASE == 25
extern int value;
int *pointer=&value;
#pragma redefine_extname value renamed
int value=7;
#elif CASE == 26
extern int value;
int use(void){return _Generic(0,int:1,default:value);}
#pragma redefine_extname value renamed
int value=7;
#elif CASE == 27
extern int value;
int use(void){return 0 ? value : 1;}
#pragma redefine_extname value renamed
int value=7;
#elif CASE == 28
extern int value;
int use(int c){if(c)return value; return 1;}
#pragma redefine_extname value renamed
int value=7;
#elif CASE == 29
extern int value;
int use(void){return 0 && value;}
#pragma redefine_extname value renamed
int value=7;
#elif CASE == 30
extern int value;
int use(void){return 1 || value;}
#pragma redefine_extname value renamed
int value=7;
#elif CASE == 31
extern int value;
int use(int x){switch(1){case 2:return value;default:return x;}}
#pragma redefine_extname value renamed
int value=7;
#elif CASE == 32
int target(void);
int use(void){return target();}
#pragma redefine_extname target renamed
int target(void){return 7;}
#endif
