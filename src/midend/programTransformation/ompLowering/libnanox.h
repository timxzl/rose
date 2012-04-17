/*  The nanox's interface to the compiler 
*  */

#ifndef LIBNANOS_H 
#define LIBNANOS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "nanos.h"
#include "nanos_omp.h"

#include <string.h>

// *** NANOX wrapper methods *** //

void NANOX_parallel_start( void (*) (void *), void *, unsigned, long, long (*) (void), void *, void (*) (void *, void *));

void NANOX_task(void (*) (void *), void (*), long, long (*) (void), bool, unsigned, void *, void (*) (void *, void *));

void NANOX_loop(int, int, int, int, int, void (*) (void *), void *, void *, long, long (*)(void), void *, void (*) (void *, void *)/*, void **/);

void NANOX_taskwait( void );
void NANOX_barrier( void );

void NANOX_critical_start( void );
void NANOX_critical_end( void );

bool NANOX_atomic_start ( void );
bool NANOX_atomic_end ( void );

bool NANOX_single ( void );

bool NANOX_master ( void );

void NANOX_flush( void );

void NANOX_todo( char * func );

#ifdef __cplusplus
 }
#endif

#endif  // LIBNANOS_H