#include <assert.h>
#include <omp.h>

void producer(int c[][100], int row)
{
    int i;
    for (i = 0; i < 100; ++i)
    {
        c[row][i] = row;
    }
}

void consumer(int c[][100], int row)
{
    int i;
    for (i = 0; i < 100; ++i)
    {
        assert(c[row][i] == row);
    }
}

int main()
{
    int c[100][100];

#pragma omp parallel
#pragma omp single
    {
        int i;
        for(i=0; i<100; i++)
        {
#pragma omp task firstprivate(i) depend(out : c[i][0:100]) 
            producer(c, i);
#pragma omp task firstprivate(i) depend(inout : c[i][0:100])
            consumer(c, i);
#pragma omp taskwait
        }
    }
}
