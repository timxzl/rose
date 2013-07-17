/*  
 * A common layer for either gomp, omni and nanos runtime library
 *  Liao 1/20/2009
 *  */
#ifndef LIB_XOMP_H 
#define LIB_XOMP_H

// Fortran outlined function uses one parameter for each variable to be passed by reference
// We predefine a max number of parameters to be allowed here.
#define MAX_OUTLINED_FUNC_PARAMETER_COUNT 256
#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h> // for abort()
#include <assert.h>
#include <sys/time.h>

#include "nanos-int.h"

// return the current time stamp in a double floating point number
extern double xomp_time_stamp(void);
extern int env_region_instr_val; // save the environment variable value for instrumentation support
//e.g. export XOMP_REGION_INSTR=0|1

//enum omp_rtl_enum {
//  e_gomp,
//  e_omni,
//  e_last_rtl
//};
//
//extern omp_rtl_enum rtl_type;

//Runtime library initialization routine
extern void XOMP_init (int argc, char ** argv);
extern void xomp_init (void);

// Runtime library termination routine
extern void XOMP_terminate (int exitcode);

// func: pointer to a function which will be run in parallel
// data: pointer to a data segment which will be used as the arguments of func
// ifClauseValue: set to if-clause-expression if if-clause exists, or default is 1. 
// numThreadsSpecified: set to the expression of num_threads clause if the clause exists, or default is 0
// file_name:line_no the start source file info about this parallel region, used to pass source level info. to runtime
extern void XOMP_parallel_start (void (*func) (void *), void *data, unsigned ifClauseValue, unsigned numThreadsSpecified, char* file_name, int line_no);
extern void XOMP_parallel_end (char* file_name, int line_no);

// Method that initializes the environment to execute a parallel construct with Nanos
extern void XOMP_parallel_start_for_NANOS( void );

// Method for parallel when NANOS library.
// func: pointer to a function which will be run in parallel
// data: pointer to a data segment which will be used as the arguments of func
// ifClauseValue: set to if-clause-expression if if-clause exists, or default is 1. 
// numThreadsSpecified: set to the expression of num_threads clause if the clause exists, or default is 0
// data_size: size of the data segment used as argument of 'func'
// get_data_align: method that will compute the alignment of the data segment used as argument of 'func' at runtime
// get_empty_data: function that retrieves structs with the same type as 'data', but empty.
//                 They are used to initialize the team, and 'data' is used to fill the empty struct after the team initialization
// init_func: function that initializes 'empty_data' with the values of the members in 'data'
extern void XOMP_parallel_for_NANOS( void (*func)(void*), void* data, unsigned ifClauseValue, unsigned numThreadsSpecified,
                                     long data_size, long (*get_data_align)(void), void* (*get_empty_data)(void), void (*init_func)(void*, void*) );

// Method that finalizes the environment to execute a parallel construct with Nanos
extern void XOMP_parallel_end_for_NANOS( void );

/* Initialize sections and return the next section id (starting from 0) to be executed by the current thread */
extern int XOMP_sections_init_next(int section_count); 

/* Return the next section id (starting from 0) to be executed by the current thread. Return value <0 means no sections left */
extern int XOMP_sections_next(void); 

/* Called after the current thread is told that all sections are executed. It synchronizes all threads also. */
extern void XOMP_sections_end(void);

/* Called after the current thread is told that all sections are executed. It does not synchronizes all threads. */
extern void XOMP_sections_end_nowait(void);

// Method for sections when Nanos RTL configured
// func: pointer to a function which will be run in parallel
// data: pointer to a data segment which will be used as the arguments of func
// data_size: size of the data segment used as argument of 'func'
// get_data_align: method that will compute the alignment of the data segment used as argument of 'func' at runtime
// empty_data: pointer to a data segment with the same type as 'data'
// init_func: function that initializes 'empty_data' with the values of the members in 'data'
// n_sections: number of sections inside the construct
// wait: boolean indicating whether a clause 'nowait' appears in the sections construct
extern void XOMP_sections_for_NANOS( void (*func)( void *, nanos_ws_desc_t * ), void * data,
                                     long data_size, long ( *get_data_align )( void ), void * empty_data, void ( * init_func )( void *, void * ),
                                     int n_sections, bool wait );

extern void XOMP_task (void (*) (void *), void *, void (*) (void *, void *),
                       long, long, bool, unsigned);

// Method for tasks when Nanos RTL configured
// func: pointer to a function which will be run in parallel
// data: pointer to a data segment which will be used as the arguments of func
// data_size: size of the data segment used as argument of 'func'
// get_data_align: method that will compute the alignment of the data segment used as argument of 'func' at runtime
// if_clause: boolean containing the expression of the if clause, if exists, or 1 otherwise
// untied: boolean indicating whether a clause 'untied' appears in the task construct
// empty_data: pointer to a data segment with the same type as 'data' 
// init_func: function that initializes 'empty_data' with the values of the members in 'data'
// num_deps: number od depend clauses associated with the task construct
// deps_direction: array containing the direction (in, out, inout) of each dependency
// deps_data: array containing the data (expression) of each dependency
// deps_n_dims: array containing the number of dimensions of each dependency
// deps_dims: array containing a nanos variable with information about the dimensions of each dependency
// deps_offset: array containing the offset of each dependency
extern void XOMP_task_for_NANOS( void (*func) (void *), void * data, long data_size, long (*get_data_align) (void), 
                                 bool if_clause, unsigned untied, void* empty_data, void (*init_func) (void*, void*),
                                 int num_deps, int * deps_direction, void ** deps_data, 
                                 int * deps_n_dims, nanos_region_dimension_t ** deps_dims, long int * deps_offset );

extern void XOMP_taskwait (void);

// scheduler functions, union of runtime library functions
// empty body if not used by one
// scheduler initialization, only meaningful used for OMNI

// Default loop scheduling, worksharing without any schedule clause, upper bounds are inclusive
// Kick in before all runtime libraries. We use the default loop scheduling from XOMP regardless the runtime chosen.
extern void XOMP_loop_default(int lower, int upper, int stride, long* n_lower, long* n_upper);

//! Optional init functions, mostly used for working with omni RTL
// Non-op for gomp
extern void XOMP_loop_static_init(int lower, int upper, int stride, int chunk_size);
extern void XOMP_loop_dynamic_init(int lower, int upper, int stride, int chunk_size);
extern void XOMP_loop_guided_init(int lower, int upper, int stride, int chunk_size);
extern void XOMP_loop_runtime_init(int lower, int upper, int stride);

//  ordered case
extern void XOMP_loop_ordered_static_init(int lower, int upper, int stride, int chunk_size);
extern void XOMP_loop_ordered_dynamic_init(int lower, int upper, int stride, int chunk_size);
extern void XOMP_loop_ordered_guided_init(int lower, int upper, int stride, int chunk_size);
extern void XOMP_loop_ordered_runtime_init(int lower, int upper, int stride);

// Specific method for Nanos++ executing OpenMP loops
// func: pointer to a function which will be run in parallel
// data: pointer to a data segment which will be used as the arguments of func
// data_size: size of the data segment used as argument of 'func'
// get_data_align: method that will compute the alignment of the data segment used as argument of 'func' at runtime
// empty_data: pointer to a data segment with the same type as 'data'
// init_func: function that initializes 'empty_data' with the values of the members in 'data'
// policy: integer defining the scheduling policy
// lower_bound: original loop lower bound
// upper_bound: original loop upper bound
// step: original loop step
// wait: boolean indicating whether the loop construct has a nowait clause
extern void XOMP_loop_for_NANOS ( void (*func)(void * loop_data, nanos_ws_desc_t * wsd), void *data, long data_size, long (*get_data_align)(void), 
                                  void * empty_data, void (*init_func)(void *, void *), int policy,
                                  int lower_bound, int upper_bound, int step, int chunk, bool wait );

// if (start), 
// mostly used because of gomp, omni will just call  XOMP_loop_xxx_next();
// (long start, long end, long incr, long chunk_size,long *istart, long *iend)
//  upper bounds are non-inclusive, 
//  bounds for inclusive loop control will need +/-1 , depending on incremental/decremental cases
extern bool XOMP_loop_static_start (long, long, long, long, long *, long *);
extern bool XOMP_loop_dynamic_start (long, long, long, long, long *, long *);
extern bool XOMP_loop_guided_start (long, long, long, long, long *, long *);
extern bool XOMP_loop_runtime_start (long, long, long, long *, long *);

extern bool XOMP_loop_ordered_static_start (long, long, long, long, long *, long *);
extern bool XOMP_loop_ordered_dynamic_start (long, long, long, long, long *, long *);
extern bool XOMP_loop_ordered_guided_start (long, long, long, long, long *, long *);
extern bool XOMP_loop_ordered_runtime_start (long, long, long, long *, long *);

// next
extern bool XOMP_loop_static_next (long *, long *);
extern bool XOMP_loop_dynamic_next (long *, long *);
extern bool XOMP_loop_guided_next (long *, long *);
extern bool XOMP_loop_runtime_next (long *, long *);

extern bool XOMP_loop_ordered_static_next (long *, long *);
extern bool XOMP_loop_ordered_dynamic_next (long *, long *);
extern bool XOMP_loop_ordered_guided_next (long *, long *);
extern bool XOMP_loop_ordered_runtime_next (long *, long *);

extern void XOMP_loop_end (void);
extern void XOMP_loop_end_nowait (void);

//--------------end of  loop functions 

extern void XOMP_barrier (void);
extern void XOMP_critical_start (void** data); 
extern void XOMP_critical_end (void** data);
extern bool XOMP_single(void);
extern bool XOMP_master(void);

extern void XOMP_atomic_start (void);
extern void XOMP_atomic_end (void);

// Specific method for Nanos++ executing OpenMP atomic
// op: integer defining the operation performed inside the atomic
// type: type of the variables used in the atomic operation
// variable: data used in the atomic operation
// operand: data used to modify 'variable'
extern void XOMP_atomic_for_NANOS (int op, int type, void * variable, void * operand);

// Specific method for Nanos++ executing OpenMP reduction
// n_reductions: Number of reductions performed in the current OpenMP directive
// all_threads_reduction: Array of n_reductions elements with the functions performing the reduction of each thread values into a unique reduction value
// func: pointer to the function that will perform the reductions
// data: pointer to a data segment which will be used as the arguments of func
// copy_back: Array of n_reduction elements with the functions copying the nanos internal values into the original variables
// set_privates: Array of n_reduction elements with the functions copying the value computed for each thread into a nanos internal variable
// global_th_data: Array of n_reduction elements with the global arrays storing each thread partial result of each reduction
// global_data: Array of n_reduction elements with the original reductin variables
// global_data_size: Array of n_reduction elements with the size of the original reduction variables
// wsd: workdescriptor needed for Nanos
// filename: name of the file where the reduction is defined
// fileline: line in the file where the reduction is defined
extern void XOMP_reduction_for_NANOS( int n_reductions, void ( **all_threads_reduction )( void * out, void * in, int num_scalars ),
                                      void ( * func )( void * data, /*void** globals, */nanos_ws_desc_t * wsd ), void * data,
                                      void ( ** copy_back )( int team_size, void * original, void * privates ),
                                      void ( ** set_privates )( void * nanos_private, void ** global_data, int reduction_id, int thread ),
                                      void ** global_th_data, void ** global_data, long * global_data_size,
                                      nanos_ws_desc_t * wsd, const char * filename, int fileline );
extern int XOMP_get_nanos_thread_num( void );
extern int XOMP_get_nanos_num_threads( void );

//--------------end of  sync functions 
   
// flush without variable list
extern void XOMP_flush_all (void);
// omp flush with variable list, flush one by one, given each's start address and size
extern void XOMP_flush_one (char * startAddress, int nbyte);


// omp ordered directive
extern void XOMP_ordered_start (void);
extern void XOMP_ordered_end (void);

//--------------------- extensions to support OpenMP accelerator model experimental implementation------
// We only include 
//--------------------- kernel launch ------------------

// the max number of threads per thread block of the first available device
size_t xomp_get_maxThreadsPerBlock(void);

//get the max number of 1D blocks for a given input length
size_t xomp_get_max1DBlock(size_t ss);

// Get the max number threads for one dimension (x or y) of a 2D block
// Two factors are considered: the total number of threads within the 2D block must<= total threads per block
//  x * y <= maxThreadsPerBlock 512 or 1024
// each dimension: the number of threads must <= maximum x/y-dimension
//    x <= maxThreadsDim[0],  1024
//    y <= maxThreadsDim[1], 1024 
//  maxThreadsDim[0] happens to be equal to  maxThreadsDim[1] so we use a single function to calculate max segments for both dimensions
size_t xomp_get_max_threads_per_dimesion_2D (void);

// return the max number of segments for a dimension (either x or y) of a 2D block
size_t xomp_get_maxSegmentsPerDimensionOf2DBlock(size_t dimension_size);

//------------------memory allocation/copy/free----------------------------------
//Allocate device memory and return the pointer
// This should be a better interface than cudaMalloc()
// since it mimics malloc() closely
/*
return a pointer to the allocated space 
   * upon successful completion with size not equal to 0
return a null pointer if
  * size is 0 
  * failure due to any reason
*/
void* xomp_deviceMalloc(size_t size);

// A host version
void* xomp_hostMalloc(size_t size);

//get the time stamp for now, up to microsecond resolution: 1e-6 , but maybe 1e-4 in practice
double xomp_time_stamp(void);


// memory copy from src to dest, return the pointer to dest. NULL pointer if anything is wrong 
void * xomp_memcpyHostToDevice (void *dest, const void * src, size_t n_n);
void * xomp_memcpyDeviceToHost (void *dest, const void * src, size_t n_n);
// copy a dynamically allocated host source array to linear dest address on a GPU device. the dimension information of the source array
// is given by: int dimensions[dimension_size], with known element size. 
// bytes_copied reports the total bytes copied by this function.  
// Note: It cannot be used copy static arrays declared like type array[N][M] !!
void * xomp_memcpyDynamicHostToDevice (void *dest, const void * src, int * dimensions, size_t dimension_size, size_t element_size, size_t *bytes_copied);

// copy linear src memory to dynamically allocated destination, with dimension information given by
// int dimensions[dimension_size]
// the source memory has total n continuous memory, with known size for each element
// the total bytes copied by this function is reported by bytes_copied
void * xomp_memcpyDynamicDeviceToHost (void *dest, int * dimensions, size_t dimension_size, const void * src, size_t element_size, size_t *bytes_copied);

void * xomp_memcpyDeviceToDevice (void *dest, const void * src, size_t n_n);
void * xomp_memcpyHostToHost (void *dest, const void * src, size_t n_n); // same as memcpy??


// free the device memory pointed by a pointer, return false in case of failure, otherwise return true
bool xomp_freeDevice(void* devPtr);
// free the host memory pointed by a pointer, return false in case of failure, otherwise return true
bool xomp_freeHost(void* hostPtr);

/* Allocation/Free functions for Host */
/* Allocate a multi-dimensional array
 *
 * Input parameters:
 *  int *dimensions:  an integer array storing the size of each dimension
 *  size_t dimension_num: the number of dimensions
 *  size_t esize: the size of an array element
 *
 * return:
 *  the pointer to the allocated array
 * */
void * xomp_mallocArray(int * dimensions, size_t dimension_num, size_t esize);

void xomp_freeArrayPointer (void* array, int * dimensions, size_t dimension_num);


/* CUDA reduction support */
//------------ types for CUDA reduction support---------
// Reduction for regular OpenMP is supported by compiler translation. No runtime support is needed.
// For the accelerator model experimental implementation, we use a two-level reduction method:
// thread-block level within GPU + beyond-block level on CPU

/* 
   We don't really want to expose this to the compiler to simplify the compiler translation.
*/
// We try to limit the numbers of runtime data types exposed to a compiler.
// A set of integers to represent reduction operations
#define XOMP_REDUCTION_PLUS 6
#define XOMP_REDUCTION_MINUS 7
#define XOMP_REDUCTION_MUL 8
#define XOMP_REDUCTION_BITAND 9 // &
#define XOMP_REDUCTION_BITOR 10 // |
#define XOMP_REDUCTION_BITXOR  11 // ^
#define XOMP_REDUCTION_LOGAND 12 // &&
#define XOMP_REDUCTION_LOGOR 13  // ||

#if 0
// No linker support for device code. We have to put implementation of these device functions into the header
// TODO: wait until nvcc supports linker for device code.
//#define XOMP_INNER_BLOCK_REDUCTION_DECL(dtype) \
//__device__ void xomp_inner_block_reduction_##dtype(dtype local_value, dtype * grid_level_results, int reduction_op);
//
///*TODO declare more prototypes */
//XOMP_INNER_BLOCK_REDUCTION_DECL(int)
//XOMP_INNER_BLOCK_REDUCTION_DECL(float)
//XOMP_INNER_BLOCK_REDUCTION_DECL(double)
//
//#undef XOMP_INNER_BLOCK_REDUCTION_DECL

#endif

#define XOMP_BEYOND_BLOCK_REDUCTION_DECL(dtype) \
  dtype xomp_beyond_block_reduction_##dtype(dtype * per_block_results, int numBlocks, int reduction_op);

XOMP_BEYOND_BLOCK_REDUCTION_DECL(int)
XOMP_BEYOND_BLOCK_REDUCTION_DECL(float)
XOMP_BEYOND_BLOCK_REDUCTION_DECL(double)

#undef XOMP_BEYOND_BLOCK_REDUCTION_DECL

#ifdef __cplusplus
 }
#endif
 
#endif /* LIB_XOMP_H */



 
