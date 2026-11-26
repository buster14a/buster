int disabled_initializer = 1;
int disabled_tentative;
#pragma clang diagnostic push
#pragma clang diagnostic warning "-Wmissing-variable-declarations"
int enabled_initializer = 2;
int enabled_tentative;
int ignored_last_tentative;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-variable-declarations"
int ignored_initializer = 3;
int ignored_tentative;
int ignored_last_tentative;
#pragma clang diagnostic pop
#pragma clang diagnostic error "-Wmissing-variable-declarations"
int promoted_initializer = 4;
int promoted_tentative;
#pragma clang diagnostic warning "-Wmissing-variable-declarations"
int demoted_initializer = 5;
#pragma clang diagnostic pop
int restored_initializer = 6;
int restored_tentative;
