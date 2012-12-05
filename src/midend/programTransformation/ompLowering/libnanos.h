/*  The nanox's interface to the compiler 
*  */

#ifndef LIBNANOS_H 
#define LIBNANOS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "nanos.h"
#include "nanos_omp.h"

#include <stdarg.h>
#include <string.h>

// *** NANOS wrapper methods *** //

void NANOS_parallel( void (*) (void *), void *, unsigned, long, long (*) (void), void *, void (*) (void *, void *));

void NANOS_task(void (*) (void *), void (*), long, long (*) (void), bool, unsigned, void *, void (*) (void *, void *), 
                int, void *, int (*) (int [], int ), void* deps_data, void* (*get_dep_data) (void* [], int ));

void NANOS_loop(void*, void*, void*, int, int, void (*) (void *), void *, void *, long, long (*)(void), void *, void (*) (void *, void *));

void NANOS_sections(int, bool, va_list);

void NANOS_taskwait( void );
void NANOS_barrier( void );

void NANOS_critical_start( void );
void NANOS_critical_end( void );

void NANOS_atomic ( int, int, void *, void * );

bool NANOS_single ( void );

bool NANOS_master ( void );

void NANOS_flush( void );

#ifdef __cplusplus
 }
#endif

#endif  // LIBNANOS_H