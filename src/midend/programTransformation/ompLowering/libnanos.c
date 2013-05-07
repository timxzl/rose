#include "libnanos.h"

#include <omp.h>
// needed for asprintf call, otherwise we get a compilation warning
#define _GNU_SOURCE         
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Struct containing information of the devce executing a task
// For more than one device, we will have to create different structs such as this one
struct nanos_const_wd_definition
{
    nanos_const_wd_definition_t base;
    nanos_device_t devices[1];
};

void nanos_omp_initialize_worksharings(void *dummy);


// ************************************************************************************ //
// ************************ Nanos Parallel structs and methods ************************ //

static int parallel_id = 0;

void NANOS_parallel_init( )
{
    nanos_err_t err = nanos_omp_set_implicit( nanos_current_wd( ) );
    if( err != NANOS_OK )
        nanos_handle_error( err );
    err = nanos_enter_team( );
    if( err != NANOS_OK )
        nanos_handle_error( err );
}

void NANOS_parallel_end( )
{
    nanos_err_t err = nanos_omp_barrier( );
    if( err != NANOS_OK )
        nanos_handle_error( err );
    err = nanos_leave_team( );
    if( err != NANOS_OK )
        nanos_handle_error( err );
}

void NANOS_parallel( void (*func) (void *), void *data, unsigned numThreads, long data_size, long (*get_data_align)(void), 
                     void* (*get_empty_data)(void), void (* init_func) (void *, void *) )
{
    nanos_err_t err;
    
    // Compute copy data (For SMP devices there are no copies. Just CUDA device requires copy data)
    int num_copies = 0;
    // Compute dependencies (ROSE is not currently supporting dependencies among the tasks)
    int num_data_accesses = 0;
    // TODO Compute dimensions
    int num_dimensions = 0;
    // Compute device descriptor (at the moment, only SMP is supported)
    int num_devices = 1;
    // TODO Compute dependencies
    nanos_data_access_t dependences[1];
  
    // Create the Device descriptor (at the moment, only SMP is supported)
    nanos_smp_args_t _smp_args = { func };
    char * parallel_name; 
    asprintf( &parallel_name, "parallel_%d", parallel_id++ );
    struct nanos_const_wd_definition nanos_wd_const_data = {
        { { 1,          // mandatory creation
            1,          // tied
            0, 0, 0, 0, 0, 0 },                     // properties 
            ( *get_data_align )( ),                 // data alignment
            num_copies, num_devices, num_dimensions,                            
            parallel_name                           // description
        }, 
        { { &nanos_smp_factory,                     // device description
            &_smp_args }                            // outlined function
        }
    };

    // Compute properties of the WD: mandatory creation, priority, tiedness, real-time info and copy declarations
    nanos_wd_dyn_props_t dyn_props;
    dyn_props.tie_to = ( void * ) 0;
    dyn_props.priority = 0;

    // Create the working team
    if( numThreads == 0 )
        numThreads = nanos_omp_get_max_threads( );
    void * nanos_team = ( void * ) 0;
    const unsigned int nthreads_vla = numThreads;
    void * team_threads[nthreads_vla];
    err = nanos_create_team( &nanos_team, ( void * ) 0, &numThreads,
                              (nanos_constraint_t *) 0, /*reuse current*/ 1, team_threads );
    if( err != NANOS_OK )
        nanos_handle_error( err );
    
    // Create a wd tied to each thread
    unsigned nth_i;
    for( nth_i = 1; nth_i < numThreads; nth_i++ )
    {
        // Set properties to the current wd of the team
        dyn_props.tie_to = team_threads[nth_i];
        
        // Create the current WD of the team
        void * empty_data = ( *get_empty_data )( );
        void * wd = ( void * ) 0;
        err = nanos_create_wd_compact( &wd, &nanos_wd_const_data.base, &dyn_props, 
                                       data_size, ( void** ) &empty_data, 
                                       nanos_current_wd( ), ( nanos_copy_data_t ** ) 0, 
                                       ( nanos_region_dimension_internal_t ** ) 0 );
        if (err != NANOS_OK) 
            nanos_handle_error(err);
        
        // Initialize outlined data
        ( *init_func )( empty_data, data );
    
        // Submit work to the WD
        err = nanos_submit( wd, num_data_accesses, ( nanos_data_access_t * ) 0, ( void * ) 0 );
        if( err != NANOS_OK ) 
            nanos_handle_error( err );
    }

    // Create the wd for the master thread, which will run the team
    dyn_props.tie_to = team_threads[0];
    err = nanos_create_wd_and_run_compact( &nanos_wd_const_data.base, &dyn_props, data_size, data, 
                                           num_data_accesses, dependences, ( nanos_copy_data_t * ) 0,
                                           ( nanos_region_dimension_internal_t * ) 0, 
                                           ( void ( * )( void *, void * ) ) 0 );
    if( err != NANOS_OK )
        nanos_handle_error( err );

    // End the team
    err = nanos_end_team( nanos_team );
    if( err != NANOS_OK )
        nanos_handle_error( err );
}

// ********************** END Nanos Parallel structs and methods ********************** //
// ************************************************************************************ //



// ************************************************************************************ //
// ************************** Nanos Task structs and methods ************************** //

static int task_id = 0;

void NANOS_task( void (*func) (void *), void *data,
                 long data_size, long (*get_data_align) (void), bool if_clause, unsigned untied, 
                 void* empty_data, void (*init_func) (void *, void *) )
{
    nanos_err_t err;
    
    // Compute copy data (For SMP devices there are no copies. Just CUDA device requires copy data)
    int num_copies = 0;
    // Compute dependencies (ROSE is not currently supporting dependencies among the tasks)
    int num_data_accesses = 0;
    // TODO Compute dimensions
    int num_dimensions = 0;
    // Compute device descriptor (at the moment, only SMP is supported)
    int num_devices = 1;
    // TODO Compute dependencies
    nanos_data_access_t dependences[1];
  
    // Create the Device descriptor (at the moment, only SMP is supported)
    nanos_smp_args_t _smp_args = { func };
    char * task_name; 
    asprintf( &task_name, "task_%d", task_id++ );
    struct nanos_const_wd_definition nanos_wd_const_data = {
        { { 0,          // mandatory creation
            !untied,    // tied 
            0, 0, 0, 0, 0, 0 },                     // properties 
        ( *get_data_align )( ),                     // data alignment
          num_copies, num_devices, num_dimensions,                            
          "traverse"//task_name                                 // description
        }, 
        { { &nanos_smp_factory,                     // device description
            &_smp_args }                            // outlined function
        }
    };

    // Compute properties of the WD: mandatory creation, priority, tiedness, real-time info and copy declarations
    nanos_wd_dyn_props_t dyn_props;
    dyn_props.tie_to = 0;
    dyn_props.priority = 0;
    
    // Create the WD
    nanos_wd_t wd = (nanos_wd_t) 0;
    err = nanos_create_wd_compact( &wd, &nanos_wd_const_data.base, &dyn_props, 
                                   data_size, ( void ** ) &empty_data,
                                   nanos_current_wd( ), ( nanos_copy_data_t ** ) 0, 
                                   ( nanos_region_dimension_internal_t ** ) 0 );
    if( err != NANOS_OK ) 
        nanos_handle_error( err );
    
    if( wd != ( void * ) 0 )
    {   // Submit the task to the existing actual working group
        // Initialize outlined data
        (*init_func)(empty_data, data);

        err = nanos_submit( wd, num_data_accesses, dependences, ( void * ) 0 );
        if( err != NANOS_OK ) 
            nanos_handle_error( err );
    }
    else
    { // The task must be run immediately
        err = nanos_create_wd_and_run_compact( &nanos_wd_const_data.base, &dyn_props, 
                                               data_size, data, num_data_accesses,
                                               dependences, ( nanos_copy_data_t * ) 0, 
                                               ( nanos_region_dimension_internal_t * ) 0, 
                                               ( void ( * )( void *, void * ) ) 0 );
        if( err != NANOS_OK ) 
            nanos_handle_error( err );
    }
}

// ************************ END Nanos Task structs and methods ************************ //
// ************************************************************************************ //



// ************************************************************************************ //
// ************************** Nanos Loop structs and methods ************************** //

static const char * nanos_get_slicer_name( int policy )
{
    switch( policy )
    {
        case 0:     return "static_for";
        case 1:     return "dynamic_for";
        case 2:     return "guided_for";
        default:    printf( "Unrecognized scheduling policy %d. Using static scheduling\n", policy );
                    return "static_for";
    };
}

static int loop_id = 0;
void NANOS_loop( void ( * func ) ( void * ), void * data, long data_size, long ( * get_data_align )( void ),
                 void* empty_data, void ( * init_func ) ( void *, void * ), int policy )
{    
    nanos_err_t err;

    // Create the WD and its properties
    void * wd = ( void * ) 0;
    nanos_wd_dyn_props_t props;
    props.tie_to = ( void * ) 0;
    props.priority = 0;
      
    // Compute copy data (For SMP devices there are no copies. Just CUDA device requires copy data)
    int num_copies = 0;
    // Compute dependencies (ROSE is not currently supporting dependencies among the tasks)
    int num_data_accesses = 0;
    // TODO Compute dimensions
    int num_dimensions = 0;
    // Compute device descriptor (at the moment, only SMP is supported)
    int num_devices = 1;
    
    // Create the slicer
    nanos_smp_args_t _smp_args = { func };
    char * loop_name; 
    asprintf( &loop_name, "loop_%d", loop_id++ );
    struct nanos_const_wd_definition nanos_wd_const_data = { 
        { { 1,          // mandatory creation
            1,          // tied
            0, 0, 0, 0, 0, 0 },                     // properties 
        ( *get_data_align )( ),                   // data alignment
          num_copies, num_devices, num_dimensions,                            
          loop_name                               // description
        }, 
        { { &nanos_smp_factory,                   // device description
            &_smp_args }                          // outlined function
        } 
    };
    void * slicer = nanos_find_slicer( nanos_get_slicer_name( policy ) );
    if( slicer == 0 )
        nanos_handle_error( NANOS_UNIMPLEMENTED );
    err = nanos_create_sliced_wd( &wd, nanos_wd_const_data.base.num_devices, nanos_wd_const_data.devices, 
                                   data_size, nanos_wd_const_data.base.data_alignment, ( void ** ) &empty_data, 
                                  nanos_current_wd( ), slicer, &nanos_wd_const_data.base.props, &props, 
                                  num_copies, ( nanos_copy_data_t ** ) 0, 
                                  num_dimensions, ( nanos_region_dimension_internal_t ** ) 0 );
    if( err != NANOS_OK )
        nanos_handle_error( err );
    
    // Initialize outlined data
    ( *init_func )( empty_data, data );
    
    // Submit the work to the runtime system
    err = nanos_submit( wd, num_data_accesses, ( nanos_data_access_t * ) 0, ( nanos_team_t ) 0 );
    if( err != NANOS_OK )
        nanos_handle_error( err );
    
    // Wait for work completion
    err = nanos_wg_wait_completion( nanos_current_wd( ), 0 );
    if( err != NANOS_OK )
        nanos_handle_error( err );
}

// ************************ END Nanos Loop structs and methods ************************ //
// ************************************************************************************ //


// ************************************************************************************ //
// ************************ Nanos Sections structs and methods ************************ //

static int sections_id = 0;

struct sections_data_t
{
    void ( * func ) ( void *, int );
    nanos_ws_desc_t * wsd;
    void * section_data;
    bool wait;
};

static void NANOS_outlined_section( struct sections_data_t * data )
{
    nanos_err_t err;
    
    // Assign the WD that will execute the section
    err = nanos_omp_set_implicit( nanos_current_wd( ) );
    if( err != NANOS_OK )
        nanos_handle_error( err );

    // Iterate over the sections
    nanos_ws_item_loop_t nanos_item_loop;
    err = nanos_worksharing_next_item( data->wsd, ( void ** ) &nanos_item_loop );
    if( err != NANOS_OK )
        nanos_handle_error( err );
    while( nanos_item_loop.execute )
    {
        int i;
        for( i = nanos_item_loop.lower; i <= nanos_item_loop.upper; ++i )
        {
            ( * data->func )( data->section_data, i );
        }
        err = nanos_worksharing_next_item( data->wsd, ( void ** ) &nanos_item_loop );
    }
        
    // Wait in case it is necessary
    if( data->wait )
    {
        err = nanos_omp_barrier( );
        if( err != NANOS_OK )
            nanos_handle_error( err );
    }
}

void NANOS_sections( void ( * func ) ( void * section_data, int i ), void * data, int n_sections, bool wait )
{
    nanos_err_t err;
    
    // Get scheduling policy
    void * ws_policy = nanos_omp_find_worksharing( omp_sched_static );
    if( ws_policy == 0 )
        nanos_handle_error( NANOS_UNIMPLEMENTED );
    
    // Create the Worksharing
    bool single_guard;
    nanos_ws_desc_t * wsd;
    nanos_ws_info_loop_t ws_info_loop;
    ws_info_loop.lower_bound = 0;
    ws_info_loop.upper_bound = n_sections - 1;
    ws_info_loop.loop_step = 1;
    ws_info_loop.chunk_size = 1;
    err = nanos_worksharing_create( &wsd, ws_policy, ( void ** ) &ws_info_loop, &single_guard );
    if( err != NANOS_OK )
        nanos_handle_error( err );

    if( single_guard )
    {
        int sup_threads;
        err = nanos_team_get_num_supporting_threads( &sup_threads );
        if( err != NANOS_OK )
            nanos_handle_error( err );
        if( sup_threads > 0 )
        {
            // Configure the Worksahring
            err = nanos_malloc( ( void ** ) &( *wsd ).threads, sizeof( void * ) * sup_threads, /*filename*/"", /*fileline*/0 );
            if( err != NANOS_OK )
                nanos_handle_error( err );
            err = nanos_team_get_supporting_threads( &( *wsd ).nths, ( *wsd ).threads );
            if( err != NANOS_OK )
                nanos_handle_error( err );
            
            // Create the WD and its properties
            void * wd = ( void * ) 0;
            nanos_wd_dyn_props_t props;
            props.tie_to = ( void * ) 0;
            props.priority = 0;
      
            // Compute copy data (For SMP devices there are no copies. Just CUDA device requires copy data)
            int num_copies = 0;
            // Compute dependencies (ROSE is not currently supporting dependencies among the tasks)
            int num_data_accesses = 0;
            // TODO Compute dimensions
            int num_dimensions = 0;
            // Compute device descriptor (at the moment, only SMP is supported)
            int num_devices = 1;
    
            // Create the slicer
            nanos_smp_args_t _smp_args = { (void(*)(void *)) &NANOS_outlined_section };
            char * sections_name; 
            asprintf( &sections_name, "sections_%d", sections_id++ );
            struct nanos_const_wd_definition nanos_wd_const_data = { 
                { { 1,          // mandatory creation
                    1,          // tied
                    0, 0, 0, 0, 0, 0 },                         // properties 
                    __alignof__(struct sections_data_t),        // data alignment
                    num_copies, num_devices, num_dimensions,                            
                    sections_name                               // description
                }, 
                { { &nanos_smp_factory,                         // device description
                    &_smp_args }                                // outlined function
                } 
            };
            void * slicer = nanos_find_slicer( "replicate" );
            if( slicer == (void *)0 )
                nanos_handle_error( NANOS_UNIMPLEMENTED );
            
            struct sections_data_t* empty_data = ( struct sections_data_t * ) 0;
            err = nanos_create_sliced_wd( &wd, nanos_wd_const_data.base.num_devices, nanos_wd_const_data.devices, 
                                           sizeof( struct  sections_data_t ), nanos_wd_const_data.base.data_alignment, ( void ** ) &empty_data, 
                                           ( void ** ) 0, slicer, &nanos_wd_const_data.base.props, &props, 
                                           num_copies, ( nanos_copy_data_t ** ) 0, 
                                           num_dimensions, ( nanos_region_dimension_internal_t ** ) 0 );
            if( err != NANOS_OK )
                nanos_handle_error( err );
            
            // Initialize outlined data
            ( * empty_data ).func = func;
            ( * empty_data ).wsd = wsd;
            ( * empty_data ).section_data = data;
            ( * empty_data ).wait = wait;
            
            // Submit the work to the runtime system
            err = nanos_submit( wd, num_data_accesses, ( nanos_data_access_t * ) 0, ( nanos_team_t ) 0 );
            if( err != NANOS_OK )
                nanos_handle_error( err );
            
            err = nanos_free( ( * wsd ).threads );
            if( err != NANOS_OK )
                nanos_handle_error( err );
        }
    }
    
    struct sections_data_t empty_data = { func, wsd, data, wait };
    ( * NANOS_outlined_section )( &empty_data );
}

// ************************************************************************************ //
// ***************************** Nanos Reduction methods ****************************** //

int NANOS_get_thread_num( void )
{
    return nanos_omp_get_thread_num( );
}

int NANOS_get_num_threads( void )
{
    return nanos_omp_get_num_threads( );
}

void NANOS_reduction( int n_reductions,
                      void ( ** all_threads_reduction )( void * out, void * in, int num_scalars ),
                      void ( ** init_thread_reduction_array )( void **, void ** ),
                      void ( * single_thread_reduction )( void *, int ), void * single_thread_data,
                      void *** global_th_data, void ** global_data, long * global_data_size, int num_scalars,
                      const char * filename, int fileline )
{
    nanos_err_t err;
    
    if( NANOS_single( ) )
    {
        int nanos_n_threads = nanos_omp_get_num_threads( );
        
        int i;
        for( i = 0; i < n_reductions; i++ )
        {
            nanos_reduction_t * result;
            
            err = nanos_malloc( ( void ** ) &result, sizeof( nanos_reduction_t ), filename, fileline );
            if( err != NANOS_OK )
                nanos_handle_error( err );
            ( *result ).original = global_data[i];
            err = nanos_malloc( &( * result ).privates, global_data_size[i] * nanos_n_threads, filename, fileline );
            if( err != NANOS_OK )
                nanos_handle_error( err );
            ( *result ).descriptor = ( *result ).privates;
            ( * ( init_thread_reduction_array[i] ) )( global_th_data[i], &( *result ).privates );
            ( *result).vop = 0;
            ( *result ).bop = ( void ( * )( void *, void *, int ) ) all_threads_reduction[i];
            ( *result ).element_size = global_data_size[i];
            ( *result ).num_scalars = num_scalars;
            ( *result ).cleanup = nanos_free0;
            err = nanos_register_reduction( result );
            if( err != NANOS_OK )
                nanos_handle_error( err );
        }
        
        err = nanos_release_sync_init( );
        if( err != NANOS_OK )
            nanos_handle_error( err );
    }
    else
    {
        err = nanos_wait_sync_init( );
        if( err != NANOS_OK )
            nanos_handle_error( err );
        
        int i;
        for( i = 0; i < n_reductions; i++ )
        {
            nanos_reduction_t * result;
            
            err = nanos_reduction_get( &result, global_data[i] );
            if( err != NANOS_OK )
                nanos_handle_error( err );
            ( * ( init_thread_reduction_array[0] ) )( global_th_data[i], &( *result ).privates );
        }
    }
    
    ( * single_thread_reduction )( single_thread_data, 0 );
}

// *************************** END Nanos Reduction methods **************************** //
// ************************************************************************************ //


void NANOS_barrier( void )
{
    nanos_team_barrier( );
}

void NANOS_taskwait( void )
{
    void * wg = nanos_current_wd( );
    nanos_err_t err = nanos_wg_wait_completion( wg, 0 );
    if( err != NANOS_OK )
        nanos_handle_error( err );
}

// ************************************************************************************ //
// ************************ Nanos Critical defines and methods ************************ //

__attribute__((weak)) nanos_lock_t nanos_default_critical_lock/* = { NANOS_LOCK_FREE }*/;

void NANOS_critical_start( void )
{
    nanos_err_t err = nanos_set_lock( &nanos_default_critical_lock );
    if( err != NANOS_OK )
        nanos_handle_error( err );
}

void NANOS_critical_end( void )
{
    nanos_err_t err = nanos_unset_lock( &nanos_default_critical_lock );
    if( err != NANOS_OK )
        nanos_handle_error( err );
}

// ********************** END Nanos Critical defines and methods ********************** //
// ************************************************************************************ //



// ************************************************************************************ //
// ************************* Nanos Atomic defines and methods ************************* //

void NANOS_atomic ( int op, int type, void * variable, void * operand )
{
  if ( ( type == 0 ) && ( op == 1 || op == 2 || op == 5 || op == 6 || op == 7) )
  { // variable has integer type and the operation is some kind of the following compound assignments:
    // plus, minus, and, ior, xor
    printf("info: 'atomic' construct implemented using atomic builtins.\n");

    int tmp = *((int *) operand);
    switch (op)
    {
      case 1: __sync_add_and_fetch((int *) variable, tmp);
              break; 
      case 2: __sync_sub_and_fetch((int *) variable, tmp);
              break;
      case 5: __sync_and_and_fetch((int *) variable, tmp);
              break;
      case 6: __sync_or_and_fetch((int *) variable, tmp);
              break;
      case 7: __sync_xor_and_fetch((int *) variable, tmp);
              break;
    };
  }
  else if ( ( type == 0 ) && ( op == 10 || op == 11) )
  { // variable has integer type and the operation is a pre-/post- incr-/decr- ement
    printf("info: 'atomic' construct implemented using atomic builtins.\n");
    if (op == 10)
      __sync_add_and_fetch((int *) variable, 1);
    else
      __sync_sub_and_fetch((int *) variable, 1);
  } 
  else
  { // any other case
    printf("info: 'atomic' construct implemented using compare and exchange.\n");
    
    if (type == 1)
    { // Float type
      printf( "Nanos atomic operations with float elements are not yet supported.\n" );
      exit(1);
    }
    else if (type == 2)
    { // Double type
      double tmp = *((double *) operand);

      double oldval, newval;
      unsigned int sizeof_var = sizeof(variable);
      do {
          oldval = *((double *) variable);
          switch (op)
          {
            case 1:  newval = oldval + tmp;
                     break;
            case 2:  newval = oldval - tmp;
                     break;
            case 3:  newval = oldval * tmp;
                     break;
            case 4:  newval = oldval / tmp;
                     break;
            case 10: newval = oldval + 1;
                     break;
            case 11: newval = oldval - 1;
                     break;
            default: printf("Unhandled operation type while generating Nanos code for OpenMP atomic contruct.");
                     abort();
          }
          __sync_synchronize();
      } while ( (sizeof_var == 4) ? !__sync_bool_compare_and_swap_4((double *) variable, 
                                                                    *(unsigned int *) &oldval, 
                                                                    *(unsigned int *) &newval) :
                                    (sizeof_var == 8) ? !__sync_bool_compare_and_swap_8((double *) variable, 
                                                                                        *(unsigned long *) &oldval, 
                                                                                        *(unsigned long *) &newval) :
                                                        0 );
    }
    else
    {
      printf("Unhandled variable type while generating Nanos code for OpenMP atomic contruct.");
      abort();
    }
  }
}

// *********************** END Nanos Atomic defines and methods *********************** //
// ************************************************************************************ //



bool NANOS_single( void )
{
    bool single_guard;
  
    nanos_err_t err = nanos_single_guard( &single_guard );
    if( err != NANOS_OK )
        nanos_handle_error(err);

    return single_guard;
}

bool NANOS_master( void )
{
    return ( omp_get_thread_num() == 0 );
}

void NANOS_flush( void )
{
    __sync_synchronize();
}