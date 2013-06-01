#include <stdlib.h>
#include <omp.h>


int main(int arg, char* argv[])
{
    int x[10][20];

    int i, j;
    for (i = 0; i < 10; i++)
    {
        for (j = 0; j < 20; j++)
        {
            x[i][j] = 1;
        }
    }
#pragma omp parallel
#pragma omp single
    {
#pragma omp task depend(inout : x[1][2])
        {
            x[1][2] += 41;
        }

#pragma omp task depend(in : x[1][2])
        {
            if (x[1][2] != 42) abort();
        }
    }

    return 0;
}
