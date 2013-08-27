#include <assert.h>
#include <stdio.h>
#include <omp.h>


void f(int * var, int cnst)
{
    (*var) = cnst;
}

void g(int * var, int cnst)
{
    assert(*var == cnst);
}

int main()
{
    int i;
    int result = 0;
    int *ptrResult = &result;
    
#pragma omp parallel
#pragma omp single
    {
        for (i = 0; i < 100; i++)
#pragma omp task depend(inout : [1] ptrResult) firstprivate(i)
            f(ptrResult, i);

#pragma omp task depend(in : [1] ptrResult) firstprivate(i)
        g(ptrResult, i-1);
    }
}
