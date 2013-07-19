#include "libnanos.h"
#include "nanos_ompss.h"

// for using asprintf
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

void NANOS_parallel_init( void )
{
    nanos_err_t err = nanos_omp_set_implicit( nanos_current_wd( ) );
    if( err != NANOS_OK )
        nanos_handle_error( err );
    err = nanos_enter_team( );
    if( err != NANOS_OK )
        nanos_handle_error( err );
}

void NANOS_parallel_end( void )
{
    nanos_err_t err = nanos_omp_barrier( );
    if( err != NANOS_OK )
        nanos_handle_error( err );
    err = nanos_leave_team( );
    if( err != NANOS_OK )
        nanos_handle_error( err );
}

void NANOS_parallel( void ( * func ) ( void * ), void * data, unsigned numThreads, long data_size, long ( *get_data_align )( void ), 
                     void * ( * get_empty_data )( void ), void ( * init_func ) ( void *, void * ) )
{
    nanos_err_t err;
    
    // Compute copy data (For SMP devices there are no copies. Just CUDA device requires copy data)
    int num_copies = 0;
    // TODO Compute dimensions
    int num_dimensions = 0;
    // Compute device descriptor (at the moment, only SMP is supported)
    int num_devices = 1;
    // TODO No dependencies for parallel construct in SMP devices
    int num_data_accesses = 0;
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
    dyn_props.flags.is_final = 0;

    // Create the working team
    if( numThreads == 0 )
        numThreads = nanos_omp_get_num_threads_next_parallel( 0 );
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

void NANOS_task( void ( * func ) ( void * ), void *data, 
                 long data_size, long ( * get_data_align ) ( void ), 
                 void * empty_data, void ( * init_func ) ( void *, void * ),
                 bool if_clause, unsigned untied,
                 int num_deps, int * deps_dir, void ** deps_data, 
                 int * deps_n_dims, nanos_region_dimension_t ** deps_dims, 
                 long int * deps_offset )
{
    nanos_err_t err;
    
    bool nanos_is_in_final;
    err = nanos_in_final( &nanos_is_in_final );
    if( nanos_is_in_final )
    {
        ( *func )( data );
    }
    else
    {
        // Compute copy data (For SMP devices there are no copies. Just CUDA device requires copy data)
        int num_copies = 0;
        // TODO Compute dimensions (for devices other than SMP)
        int num_dimensions = 0;
        // Compute device descriptor (at the moment, only SMP is supported)
        int num_devices = 1;
        // Compute dependencies
        const unsigned int num_data_accesses = num_deps;
        nanos_data_access_t dependences[num_data_accesses];
        int i;
        for( i = 0; i < num_data_accesses; ++i )
        {
            int in = ( deps_dir[i] & ( e_dep_dir_in | e_dep_dir_inout ) );
            int out = ( deps_dir[i] & ( e_dep_dir_out | e_dep_dir_inout ) );
            nanos_access_type_internal_t flags = {
                ( in != 0 ), // input
                ( out != 0 ), // output
                0 , // can rename
                0 , // concurrent
                0 , // commutative
            };
            nanos_data_access_t dep = { deps_data[i], flags, deps_n_dims[i], deps_dims[i], deps_offset[i] };
            dependences[i] = dep;
        }
        
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
            task_name                                 // description
            }, 
            { { &nanos_smp_factory,                     // device description
                &_smp_args }                            // outlined function
            }
        };
        
        // Compute properties of the WD: mandatory creation, priority, tiedness, real-time info and copy declarations
        nanos_wd_dyn_props_t dyn_props;
        dyn_props.tie_to = 0;
        dyn_props.priority = 0;
        dyn_props.flags.is_final = 0;
    
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
            ( *init_func )( empty_data, data );
    
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
}

// ************************ END Nanos Task structs and methods ************************ //
// ************************************************************************************ //



// ************************************************************************************ //
// ********************** Nanos Worksharings structs and methods ********************** //

static int loop_id = 0;
static int sections_id = 0;

static nanos_omp_sched_t nanos_get_scheduling( int policy )
{
    switch( policy )
    {
        case 1:     return omp_sched_static;
        case 2:     return omp_sched_dynamic;
        case 3:     return omp_sched_guided;
        default:    printf( "Unrecognized scheduling policy %d. Using static scheduling\n", policy );
                    return omp_sched_static;
    };
}

static void NANOS_worksharing( int lb, int ub, int step, int chunk, char * description,
                               void ( * func ) ( void * data, nanos_ws_desc_t * wsd ), void * data, long data_size, long ( * get_data_align )( void ), 
                               void * empty_data, void ( * init_func ) ( void *, void * ), void * ws_policy, bool wait )
{
    nanos_err_t err;

    // Create the Worksharing
    bool single_guard;
    nanos_ws_desc_t * wsd;
    nanos_ws_info_loop_t ws_info_loop;
    ws_info_loop.lower_bound = lb;
    ws_info_loop.upper_bound = ub;
    ws_info_loop.loop_step = step;
    ws_info_loop.chunk_size = chunk;
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
            props.flags.is_final = 0;
            
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
            struct nanos_const_wd_definition nanos_wd_const_data = { 
                { { 1,          // mandatory creation
                    1,          // tied
                    0, 0, 0, 0, 0, 0 },                         // properties 
                    ( *get_data_align )( ),                     // data alignment
                    num_copies, num_devices, num_dimensions,
                    description                                 // description
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
                                          data_size, nanos_wd_const_data.base.data_alignment, ( void ** ) &empty_data, 
                                          ( void ** ) 0, slicer, &nanos_wd_const_data.base.props, &props, 
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
            
            err = nanos_free( ( * wsd ).threads );
            if( err != NANOS_OK )
                nanos_handle_error( err );
        }
    }
    
    ( * func )( data, wsd );
    
    // Wait in case it is necessary
    if( wait )
    {
        err = nanos_omp_barrier( );
        if( err != NANOS_OK )
            nanos_handle_error( err );
    }
}

void NANOS_loop( void ( * func ) ( void * loop_data, nanos_ws_desc_t * wsd ), void * data, long data_size, long ( * get_data_align )( void ),
                 void* empty_data, void ( * init_func ) ( void *, void * ), int policy,
                 int lower_bound, int upper_bound, int step, int chunk, bool wait )
{
    // Get scheduling policy
    void * ws_policy = nanos_omp_find_worksharing( nanos_get_scheduling( policy ) );
    if( ws_policy == 0 )
        nanos_handle_error( NANOS_UNIMPLEMENTED );

    char * loop_name; 
    asprintf( &loop_name, "loop_%d", loop_id++ );
    NANOS_worksharing( lower_bound, upper_bound, step , chunk, loop_name,
                       func, data, data_size, get_data_align, empty_data, init_func, 
                       ws_policy, wait );
}

void NANOS_sections( void ( * func ) ( void * section_data, nanos_ws_desc_t * wsd ), void * data,
                     long data_size, long ( * get_data_align )( void ), void * empty_data, void ( * init_func ) ( void *, void * ),
                     int n_sections, bool wait )
{
    // Get scheduling policy
    void * ws_policy = nanos_omp_find_worksharing( omp_sched_static );
    if( ws_policy == 0 )
        nanos_handle_error( NANOS_UNIMPLEMENTED );
    
    char * sections_name; 
    asprintf( &sections_name, "sections_%d", sections_id++ );
    NANOS_worksharing( /*lb*/ 0, /*ub*/ n_sections - 1, /*step*/ 1 , /*chunk*/ 1, sections_name,
                       func, data, data_size, get_data_align, empty_data, init_func, 
                       ws_policy, wait );
}

bool NANOS_single( void )
{
    bool single_guard;
  
    nanos_err_t err = nanos_single_guard( &single_guard );
    if( err != NANOS_OK )
        nanos_handle_error(err);

    return single_guard;
}

// ******************** END Nanos Worksharings structs and methods ******************** //
// ************************************************************************************ //



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
                      void ( * func )( void * data, /*void** globals, */nanos_ws_desc_t * wsd ), void * data,
                      void ( ** copy_back )( int team_size, void * original, void * privates ),
                      void ( ** set_privates )( void * nanos_private, void ** global_data, int reduction_id, int thread ),
                      void ** global_th_data, void ** global_data, long * global_data_size,
                      nanos_ws_desc_t * wsd, const char * filename, int fileline )
{
    nanos_err_t err;
    
    err = nanos_omp_set_implicit( nanos_current_wd( ) );
    if( err != NANOS_OK )
        nanos_handle_error( err );
    
    bool red_single_guard;
    err = nanos_enter_sync_init( &red_single_guard );
    if( err != NANOS_OK )
        nanos_handle_error( err );
    
    nanos_reduction_t* result[n_reductions];

    int nanos_n_threads = nanos_omp_get_num_threads( );
//     void * _global_[ nanos_n_threads ];

    if( red_single_guard )
    {
        int i;
        for( i = 0; i < n_reductions; i++ )
        {
            err = nanos_malloc( ( void ** ) &result, sizeof( nanos_reduction_t ), filename, fileline );
            if( err != NANOS_OK )
                nanos_handle_error( err );
            ( * ( result[i] ) ).original = global_data[i];
            err = nanos_malloc( &( * ( result[i] ) ).privates, global_data_size[i] * nanos_n_threads, filename, fileline );
            if( err != NANOS_OK )
                nanos_handle_error( err );
            ( * ( result[i] ) ).descriptor = ( * ( result[i] ) ).privates;       // Fortran only
//             _global_[i] = (int *)( * ( result[i] ) ).privates;
            ( * ( result[i] ) ).vop = copy_back[i];
            ( * ( result[i] ) ).bop = all_threads_reduction[i];
            ( * ( result[i] ) ).element_size = global_data_size[i];
            ( * ( result[i] ) ).num_scalars = 1;
            ( * ( result[i] ) ).cleanup = nanos_free0;
            err = nanos_register_reduction( result[i] );
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
            err = nanos_reduction_get( & ( result[i] ), global_data[i] );
            if( err != NANOS_OK )
                nanos_handle_error( err );
//             _global_[i] = (int *)( * ( result[i] ) ).privates;
        }
    }
    
    // Execute the function containing the reduction
    ( * func )( data, /*_global_, */wsd );

    // Point the 'privates' member to the actual private value computed in the reduction code
    // FIXME copy back data cannot be made at the end because 
    // the privates member is used before this copy back is performed
    int i;
    for( i = 0; i < n_reductions; i++ )
    {
        ( * ( set_privates[i] ) )( ( * ( result[i] ) ).privates, global_th_data, i, omp_get_thread_num( ) );
    }
}

// *************************** END Nanos Reduction methods **************************** //
// ************************************************************************************ //


// ************************************************************************************ //
// ******************** Nanos synchronization defines and methods ********************* //

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

__attribute__((weak)) nanos_lock_t nanos_default_critical_lock = { NANOS_LOCK_FREE };

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
//       float tmp = *((float *) operand);
      printf("Nanos support for Atomic access to floats is not yet implemented\n");
      abort();
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
      abort( );
    }
  }
}

bool NANOS_master( void )
{
    return ( omp_get_thread_num( ) == 0 );
}

void NANOS_flush( void )
{
    __sync_synchronize( );
}

// ****************** END Nanos synchronization defines and methods ******************* //
// ************************************************************************************ //
