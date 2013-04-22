#include "libnanos.h"
#include "nanos_ompss.h"

// for using asprintf
#define _GNU_SOURCE

#include <omp.h>
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
        
        // Initialize empty data structure
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
                 int * deps_n_dims, nanos_region_dimension_t ** deps_dims )
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
    
    // Initialized outlined data
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



// The ellipsed arguments depend on the number of sections. The arguments per section are:
// - void (*func) (void*) : Outlined function containing the code of the section
// - void * data : data segment containing the parameters to be passed to the outlined function
// - long arg_size : size of the data segment
// - long (*get_arg_align)(void) : function that computes the proper alignment of the data segment
// - void * empty_data : empty data segment of the same type as 'data'
// - void (*init_func) (void *, void *): pointer to the function thar initializes 
//         the members of the empty data secgment with the members of the filled data segment 
void NANOS_sections(int num_sections, bool must_wait, va_list sections_args)
{
//   nanos_err_t err;
// 
//   _Bool single_guard;
//   err = nanos_omp_single(&single_guard);
//   if (single_guard)
//   {
//     // Create the workdescriptor for each section
//     nanos_wd_t _wd_section_list[num_sections];
//     int i;
//     for (i = 0; i < num_sections; ++i)
//     {
//       // Get the parameters of the current section
//       void (*func) (void*) = va_arg(sections_args, void (*) (void*));
//       void* data = va_arg(sections_args, void*);
//       long data_size = va_arg(sections_args, long);
//       long (*get_data_align)(void) = va_arg(sections_args, long (*)(void));
//       void* empty_data = va_arg(sections_args, void*);
//       void (*init_func)(void*, void*) = va_arg(sections_args, void (*)(void*, void*));
// 
//       // Compute copy data (For SMP devices there are no copies. Just CUDA device requires copy data)
//       int num_copies = 0;
// 
//       // Create the Device descriptor (at the moment, only SMP is supported)
//       int num_devices = 1;
//       nanos_smp_args_t _smp_args = { func };
//       struct nanos_const_wd_definition_local_t
//       {
//         nanos_const_wd_definition_t base;
//         nanos_device_t devices[1];
//       };
//       struct nanos_const_wd_definition_local_t _const_def = {
//         {
//           {
//             1,                  /* mandatory creation => only CUDA requieres mandatory creation */
//             0,                  /* tied */
//             0, 0, 0, 0, 0, 0,   /* reserved0..5 */
//             0                   /* priority */
//           },
//           (*get_data_align)(),
//           num_copies,
//           num_devices
//         },
//         {{  // device description
//             nanos_smp_factory,
//             &_smp_args
//         }}
//       };
// 
//       // Create the wd
//       nanos_wd_t wd = (nanos_wd_t) 0;
//       nanos_wd_dyn_props_t dyn_props = {0};
//       err = nanos_create_wd_compact(&wd, &_const_def.base, &dyn_props, data_size, (void **) &empty_data, nanos_current_wd(), (nanos_copy_data_t **) 0);
//       if (err != NANOS_OK)
//           nanos_handle_error(err);
//       (*init_func)(empty_data, data);
//       _wd_section_list[i] = wd;
//     }
// 
//     // Compute copy data (For SMP devices there are no copies. Just CUDA device requires copy data)
//     int num_copies = 0;
//     // Compute dependencies (ROSE is not currently supporting dependencies among the tasks)
//     int num_data_accesses = 0;
// 
//     // Create master workdescriptor and submit the work
//     nanos_wd_props_t props;
//     __builtin_memset(&props, 0, sizeof (props));
//     props.mandatory_creation = 1;
//     nanos_wd_dyn_props_t dyn_props = {0};
//     nanos_slicer_t compound_slicer = nanos_find_slicer("compound_wd");
//     void *compound_dev = (void *) 0;
//     nanos_slicer_get_specific_data(compound_slicer, &compound_dev);
//     nanos_smp_args_t compound_devices_args = {(void (*)(void *)) compound_dev};
//     nanos_device_t compound_device[1] = {{
//         nanos_smp_factory,
//         &compound_devices_args
//     }};
//     nanos_compound_wd_data_t *list_of_wds = (nanos_compound_wd_data_t *) 0;
//     nanos_wd_t wd = (nanos_wd_t) 0;
//     err = nanos_create_sliced_wd(&wd, 1, compound_device, sizeof(nanos_compound_wd_data_t) + num_sections * sizeof(nanos_wd_t), 
//                                  __alignof__(nanos_compound_wd_data_t), (void **) &list_of_wds, nanos_current_wd(), 
//                                  compound_slicer, &props, &dyn_props, num_copies, (nanos_copy_data_t **) 0);
//     if (err != NANOS_OK)
//         nanos_handle_error(err);
//     list_of_wds->nsect = num_sections;
// //     __builtin_memcpy(list_of_wds->lwd, _wd_section_list, sizeof (_wd_section_list));
//     err = nanos_submit(wd, num_data_accesses, (nanos_data_access_t *) 0, (nanos_team_t) 0);
//     if (err != NANOS_OK)
//         nanos_handle_error(err);
//   }
// 
//   if (must_wait)
//     nanos_omp_barrier();
}

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

// ********************** END Nanos Critical defines and methods ********************** //
// ************************************************************************************ //



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
      abort();
    }
  }
}

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
    return ( omp_get_thread_num( ) == 0 );
}

void NANOS_flush( void )
{
    __sync_synchronize();
}
