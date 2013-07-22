/*  The nanos' interface to the compiler 
*  */

#ifndef LIBNANOS_H 
#define LIBNANOS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "nanos.h"
#include "nanos-int.h"
#include "nanos_omp.h"

#include <stdarg.h>
#include <string.h>

// *** NANOS wrapper methods *** //

void NANOS_parallel_init( void );
void NANOS_parallel_end( void );
void NANOS_parallel( void ( * ) ( void * ), void *, unsigned, long, long ( * )( void ), 
                     void* ( * )( void ), void ( * ) ( void *, void * ) );

void NANOS_task( void ( * ) ( void * ),                                                     // func 
                 void *, long, long ( * ) ( void ), void *, void ( * ) ( void *, void * ),  // data 
                 bool, unsigned,                                                            // clauses
                 int, int *, void **, int *, nanos_region_dimension_t **, long int * );     // dependencies

void NANOS_loop( void ( * ) ( void *, nanos_ws_desc_t * ), void *, long, long ( * )( void ),
                 void *, void ( * ) ( void *, void * ), int,
                 int, int, int, int, bool );

void NANOS_sections( void ( * ) ( void *, nanos_ws_desc_t * ), void *,
                     long, long ( * ) ( void ), void *, void ( * ) ( void *, void * ),
                     int, bool );

int NANOS_get_thread_num( void );
int NANOS_get_num_threads( void );
void NANOS_reduction( int, void ( ** )( void *, void *, int ),
                      void ( * )( void *, /*void**, */nanos_ws_desc_t * ), void *, void ( ** )( int, void *, void * ),
                      void ( ** )( void *, void **, int, int ),
                      void **, void **, long *, nanos_ws_desc_t *, const char *, int );

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
