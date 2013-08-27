#include <stdio.h>
#include <math.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <omp.h>

#define SIZE 256
#define BLOCK_SIZE 16

typedef struct
{
  struct timeval start;
  struct timeval end;
  double elapsed_time;
} timing_t;

void timing_start(timing_t* t)
{
    memset(t, 0, sizeof(*t));
    
    gettimeofday(&(t->start), NULL);
}

void timing_end(timing_t* t)
{
    gettimeofday(&(t->end), NULL);

    double start_value = t->start.tv_sec*1e6 + t->start.tv_usec;
    double end_value = t->end.tv_sec*1e6 + t->end.tv_usec;

    double diff_value = end_value - start_value;

    t->elapsed_time = diff_value / 1e6;
}

double timing_elapsed(const timing_t* t)
{
    return (t->elapsed_time);
}

void matmul_block(int N, int BS, float *a, float *b, float *c)
{
    // Assumption: BS evenly divides N
    int i, j, k;
    for (i = 0; i < BS; i++)
    {
        for (j = 0; j < BS; j++)
        {
            for (k = 0; k < BS; k++)
            {
                c[N*i + j] += a[N*i + k] * b[N*k + j];
            }
        }
    }
}

void matmul(int BS, float a[SIZE][SIZE], float b[SIZE][SIZE])
{
   int i, j, k;
   float (*c1)[SIZE] = calloc(sizeof(float), SIZE * SIZE);

   timing_t parallel_time;
   timing_start(&parallel_time);
   for (i = 0; i < SIZE; i+=BS) {
      for (j = 0; j < SIZE; j+=BS) {
         for (k = 0; k < SIZE; k+=BS) {
#pragma omp task depend ( in: a[i:BS][k:BS], b[k:BS][j:BS] ) \
                 depend ( inout: c1[i:BS][j:BS] )
             {
                 matmul_block(SIZE, BS, &a[i][k], &b[k][j], &c1[i][j]);
             }
         }
      }
   }
#pragma omp taskwait
   timing_end(&parallel_time);
   fprintf(stderr, "Parallel ( %d threads ) ended. %.2f sec\n", omp_get_max_threads(), timing_elapsed(&parallel_time));

   float (*c2)[SIZE] = calloc(sizeof(float), SIZE * SIZE);
   // Serial
   timing_t serial_time;
   timing_start(&serial_time);
//    fprintf(stderr, "Serial\n");
   for (i = 0; i < SIZE; i++) {
      for (j = 0; j < SIZE; j++) {
         for (k = 0; k < SIZE; k++) {
                c2[i][j] += a[i][k] * b[k][j];
         }
      }
   }
   timing_end(&serial_time);
   fprintf(stderr, "Serial ended. %.2f sec\n", timing_elapsed(&serial_time));

   // Check
//    fprintf(stderr, "Check\n");
   for (i = 0; i < SIZE; i++) {
      for (j = 0; j < SIZE; j++) {
          if (fabsf(c1[i][j] - c2[i][j]) > 1e-5)
          {
              fprintf(stderr, "Check failure. Error at [%d][%d] %.5f != %.5f\n", i, j, c1[i][j], c2[i][j]);
              abort();
          }
      }
   }
   fprintf(stderr, "Check is OK. Speedup is %.2f\n", timing_elapsed(&serial_time) / timing_elapsed(&parallel_time));

   free(c1);
   free(c2);
}

float a[SIZE][SIZE];
float b[SIZE][SIZE];

float rand_FloatRange(float a, float b)
{
    return ((b-a)*((float)random()/RAND_MAX))+a;
}

int main(int argc, char* argv[])
{
    srand(clock());

    // Random fill the matrices
    int i, j;
    for (i = 0; i < SIZE; i++)
    {
        for (j = 0; j < SIZE; j++)
        {
            a[i][j] = rand_FloatRange(-1e9, 1e9);
            b[i][j] = rand_FloatRange(-1e9, 1e9);
        }
    }

#pragma omp parallel default(shared)
#pragma omp single
    matmul(BLOCK_SIZE, a, b);

    return 0;
}
