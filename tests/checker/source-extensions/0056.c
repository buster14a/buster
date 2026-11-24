#pragma STDC FENV_ACCESS ON
float x=1.0f/3.0f;float f(float a){
#pragma STDC FENV_ACCESS OFF
{
#pragma STDC FENV_ACCESS ON
a+=1.0f/3.0f;}return a+1.0f/3.0f;}
#pragma STDC FENV_ACCESS DEFAULT
float g(void){return 1.0f/3.0f;}
