/*
* Copyright (c) 2013, BSC (Barcelona Supercomputing Center)
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright
*       notice, this list of conditions and the following disclaimer in the
*       documentation and/or other materials provided with the distribution.
*     * Neither the name of the <organization> nor the
*       names of its contributors may be used to endorse or promote products
*       derived from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY BSC ''AS IS'' AND ANY
* EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL <copyright holder> BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <sys/time.h>
#include <time.h>

#include <omp.h>

#define TRUE 1
#define FALSE 0

#define NB 16
const int BSIZE = 32;

/* Sparse LU factorization
 */

void genmat (double* M[NB][NB])
{
    int init_val, i, j, ii, jj;
    int null_entry;
    int bcount = 0;
    double *p;

    for (ii=0; ii<NB; ii++) {
        for (jj=0; jj<NB; jj++) {
            null_entry=FALSE;
            if ((ii<jj) && (ii%3 !=0)) null_entry =TRUE;
            if ((ii>jj) && (jj%3 !=0)) null_entry =TRUE;
            if (ii%2==1) null_entry=TRUE;
            if (jj%2==1) null_entry=TRUE;
            if (ii==jj) null_entry=FALSE;
            if (null_entry==FALSE){
                bcount++;
                M[ii][jj] = (double *)malloc(BSIZE*BSIZE*sizeof(double));
                if (M[ii][jj]==NULL) {
                    printf("Out of memory\n");
                    exit(1);
                }
            } else M[ii][jj] = NULL;
        }
    }

    init_val = 1325;
    for (ii = 0; ii < NB; ii++) {
        for (jj = 0; jj < NB; jj++) {
        p = M[ii][jj];
        if (p!=NULL)
            for (i = 0; i < BSIZE; i++) {
                for (j = 0; j < BSIZE; j++) {
                    init_val = (3125 * init_val) % 65536;
                    (*p) = (double)((init_val - 32768.0) / 16384.0);
                    p++;
                }
            }
        }
    }
}

double *allocate_clean_block()
{
    int i,j;
    double *p, *q;

    p=(double*)malloc(BSIZE*BSIZE*sizeof(double));
    q=p;
    if (p!=NULL){
        for (i = 0; i < BSIZE; i++)
            for (j = 0; j < BSIZE; j++){(*p)=(double)0.0; p++;}

    }
    else printf ("OUT OF MEMORY!!!!!!!!!!!!!!!\n");
    return (q);
}

bool check_matrices( double* M[NB][NB], double* N[NB][NB] )
{
    bool equal = true;
    int i, j, k;

    for(i=0; i<NB && equal; i++) {
        for(j=0; j<NB; j++) {
            if(M[i][j]==NULL && N[i][j]==NULL )
                continue;
            else if(M[i][j]!=NULL && N[i][j]!=NULL) {
                for(k=0; k<BSIZE*BSIZE && equal; k++) {
                    if( fabs(M[i][j][k] - N[i][j][k]) > 0.00001 ) {
                        printf("Non equal values: M[%d][%d][%d]=%11.4f, N[%d][%d][%d]=%11.4f\n", i, j, k, M[i][j][k], i, j, k, N[i][j][k]);
                        equal = false;
                        break;
                    }
                }
            } else {
                equal = false;
                break;
            }
        }
    }

    return equal;
}

void print_matrices( double* M[NB][NB], double* N[NB][NB] )
{
    int i, j, k;
    for(i=0; i<NB; i++) {    
        for(j=0; j<NB; j++) {
            printf(" | ");
            if( M[i][j]==NULL )
                printf(" NULL ");
            else
                for(k=0; k<BSIZE*BSIZE; k++)
                {
                    printf( "%11.2f  ", M[i][j][k] );
                }
            printf("| ");
        }
        printf("\n");
    }
    printf("\n");
    for(i=0; i<NB; i++) {    
        for(j=0; j<NB; j++) {
            printf(" | ");
            if( M[i][j]==NULL )
                printf(" NULL ");
            else
                for(k=0; k<BSIZE*BSIZE; k++)
                {
                    printf( "%11.2f  ", N[i][j][k] );
                }
            printf("| ");
        }
        printf("\n");
    }
}
    
float mysecond()
{
    clock_t t = clock();
    return ( (float) t ) / CLOCKS_PER_SEC;
}

void lu0(double *diag)
{
    int i, j, k;
    for (k=0; k<BSIZE; k++) {
        for (i=k+1; i<BSIZE; i++) {
            diag[i*BSIZE+k] = diag[i*BSIZE+k] / diag[k*BSIZE+k];
            for (j=k+1; j<BSIZE; j++)
                diag[i*BSIZE+j] = diag[i*BSIZE+j] - diag[i*BSIZE+k] * diag[k*BSIZE+j];
        }
    }
}

void fwd(double *diag, double *col)
{
    int i, j, k;
    for (k=0; k<BSIZE; k++)
        for (i=k+1; i<BSIZE; i++)
            for (j=0; j<BSIZE; j++)
                col[i*BSIZE+j] = col[i*BSIZE+j] - diag[i*BSIZE+k]*col[k*BSIZE+j];
}

void bdiv(double *row, double *diag)
{
    int i, j, k;
    for (i=0; i<BSIZE; i++) {
        for (k=0; k<BSIZE; k++) {
            row[i*BSIZE+k] = row[i*BSIZE+k] / diag[k*BSIZE+k];
            for (j=k+1; j<BSIZE; j++)
                row[i*BSIZE+j] = row[i*BSIZE+j] - row[i*BSIZE+k]*diag[k*BSIZE+j];
        }
    }
}

void bmod(double *row, double *col, double *inner)
{
    int i, j, k;
    for (i=0; i<BSIZE; i++) {
        for (k=0; k<BSIZE; k++) {
            for (j=0; j<BSIZE; j++) {
                inner[i*BSIZE+j] = inner[i*BSIZE+j] - row[i*BSIZE+k]*col[k*BSIZE+j];
            }
        }
    }
}

void lu_serial( double* M[NB][NB] )
{
    float t_start,t_end;
    float time;
    t_start= mysecond();

    int ii, jj, kk;
    for (kk=0; kk<NB; kk++) {
        {
            double *diag = M[kk][kk];
            lu0(diag);
        }

        for (jj=kk+1; jj<NB; jj++)
            if (M[kk][jj] != NULL)
            {
                double *diag = M[kk][kk];
                double *col = M[kk][jj];
                fwd(diag, col);
            }

        for (ii=kk+1; ii<NB; ii++) {
            if (M[ii][kk] != NULL) {
                {
                    double *row = M[kk][kk];
                    double *diag = M[ii][kk];
                    bdiv (diag, row);
                }

                for (jj=kk+1; jj<NB; jj++) {
                    if (M[kk][jj] != NULL) {
                        if (M[ii][jj]==NULL)
                            M[ii][jj]=allocate_clean_block();
                        {
                            double *row = M[ii][kk];
                            double *col = M[kk][jj];
                            double *inner = M[ii][jj];
                            bmod(row, col, inner);
                        }
                    }
                }
            }
        }
    }

    t_end=mysecond();

    time = t_end-t_start;
    printf("Serial time to compute = %f usec\n", time);
}


void lu_dependencies( double* M[NB][NB] )
{
    float t_start,t_end;
    float time;
    t_start=mysecond();

    int ii, jj, kk;
    for (kk=0; kk<NB; kk++) {
        {
            double *diag = M[kk][kk];
#pragma omp task depend(inout: [BSIZE][BSIZE]diag)
            lu0(diag);
        }
        for (jj=kk+1; jj<NB; jj++)
            if (M[kk][jj] != NULL) {
                double *diag = M[kk][kk];
                double *col = M[kk][jj];
#pragma omp task depend(in: [BSIZE][BSIZE]diag) depend(inout: [BSIZE][BSIZE]col)
                fwd(diag, col);
            }
            
        for (ii=kk+1; ii<NB; ii++) {
            if (M[ii][kk] != NULL) {
                {
                    double *row = M[kk][kk];
                    double *diag = M[ii][kk];
#pragma omp task depend(in: [BSIZE][BSIZE]diag) depend(inout: [BSIZE][BSIZE]row)
                    bdiv (diag, row);
                }

                for (jj=kk+1; jj<NB; jj++) {
                    if (M[kk][jj] != NULL) {
                        if (M[ii][jj]==NULL)
                            M[ii][jj]=allocate_clean_block();
                        {
                            double *row = M[ii][kk];
                            double *col = M[kk][jj];
                            double *inner = M[ii][jj];
#pragma omp task depend(in: [BSIZE][BSIZE]row, [BSIZE][BSIZE]col) depend(inout: [BSIZE][BSIZE]inner)
                            bmod(row, col, inner);
                        }    
                    }
                }
            }
        }
    }

#pragma omp taskwait

    t_end=mysecond();
    time = t_end-t_start;
    printf("Dependencies time to compute = %f usec\n", time);
}

int main(int argc, char* argv[])
{
    double *A[NB][NB];
    double *B[NB][NB];

    // Matrices initializations
    //   printf("Generating matrices... \n");
    genmat(A);
    genmat(B);

    // Different versions' execution
    lu_serial( A );
#pragma omp parallel
#pragma omp single
    lu_dependencies( B );
    
    
//     print_matrices( A, B );
            
    // Check results
    if( !check_matrices( A, B ) )
        printf("Execution of dependencies version has failed\n");

    return 0;
}
