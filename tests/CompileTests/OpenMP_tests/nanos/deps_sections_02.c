#include <stdlib.h>
#include <stdio.h>
#include <omp.h>


void foo (unsigned sizex, unsigned sizey, int u[100][100])
{
    int (*check)[sizey] = calloc(sizex * sizey, sizeof(check[0][0]));

    int i, j;
    for (i = 0; i < sizex; i++)
    {
        for (j = 0; j < sizey; j++)
        {
            u[i][j] = j - i;
            check[i][j] = j - i;
        }
    }

    for (i = sizex-1; i >= 1; i--)
    {
        for (j = sizey-1; j >= 1; j--)
        {
#pragma omp task depend(in : u[i-1:1][j-1:1]) depend(inout : u[i:1][j:1])
            {
                u[i][j] += u[i-1][j-1];
            }
        }
    }
#pragma omp taskwait

    for (i = 1; j < sizex; i++)
    {
        for (j = 1; j < sizey; j++)
        {
            if (u[i][j] != (check[i][j] + check[i-1][j-1]))
            {
                fprintf(stderr, "%d != %d\n", u[i][j], (check[i][j] + check[i-1][j-1]));
                abort();
            }
        }
    }

    free(check);
}

int main(int argc, char *argv[])
{
    int (*k)[100] = calloc(100 * 100, sizeof(*k));

    foo(100, 100, k);

    free(k);

    return 0;
}
