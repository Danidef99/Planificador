#include <stdio.h>
#include <stdlib.h>


int main(){
    int a = 5;
    int b = a+2;
    for (int c=0; c<4; c++){
        b++;
        a++;
    }
    printf("a: %i\n", a);
    printf("b: %i", b);
    return 0;
}
    