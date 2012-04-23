#include "libnanox.h"

#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

__attribute__((weak)) nanos_lock_t _nx_default_critical_lock = {NANOS_LOCK_FREE};
// FIXME For c++ we just need "__attribute__((weak)) nanos_lock_t _nx_default_critical_lock;"

void nanos_omp_initialize_worksharings(void *dummy);

void NANOX_parallel(void (*func) (void *), void *data, unsigned numThreads, long data_size, long (*get_data_align)(void), 
                          void* empty_data, void (* init_func) (void *, void *))
{
  nanos_err_t err;

  if (numThreads == 0)
    numThreads = nanos_omp_get_max_threads();

  // Create the working team
  nanos_thread_t _nanos_threads[numThreads];
  nanos_team_t _nanos_team = (nanos_team_t) 0;
  err = nanos_create_team(&_nanos_team, (nanos_sched_t)0, &numThreads,
                          (nanos_constraint_t*)0, /*reuse current*/ 1, _nanos_threads);
  if (err != NANOS_OK)
    nanos_handle_error(err);

  // Compute copy data (For SMP devices there are no copies. Just CUDA device requires copy data)
  int num_copies = 0;
  // Compute dependencies (ROSE is not currently supporting dependencies among the tasks)
  int num_dependences = 0;
  // Compute the alignement of the struct with the arguments to the outlined function
  long data_align = (*get_data_align)();

  // Create the Device descriptor (at the moment, only SMP is supported)
  int num_devices = 1;
  nanos_smp_args_t _smp_args = { func };
  struct nanos_const_wd_definition_local_t
  {
    nanos_const_wd_definition_t base;
    nanos_device_t devices[1];
  };
  struct nanos_const_wd_definition_local_t _const_def = {
    {
      {
        1,                  /* mandatory creation */
        0,                  /* tied */
        0, 0, 0, 0, 0, 0,   /* reserved0..5 */
        0                   /* priority */
      },
      data_align,
      num_copies,
      num_devices
    },
    {{  // device description
        nanos_smp_factory,
        &_smp_args
    }}
  };

  // Compute properties of the WD: mandatory creation, priority, tiedness, real-time info and copy declarations
  nanos_wd_dyn_props_t dyn_props = { 0 };

  // Create a wd tied to each thread
  unsigned _i;
  for (_i = 1; _i < numThreads; _i++)
  {
    dyn_props.tie_to = _nanos_threads[_i];

    // Create the wd
    nanos_wd_t wd = 0;
    err = nanos_create_wd_compact(&wd, &_const_def.base, &dyn_props, data_size, (void**)&empty_data, 
                          nanos_current_wd(), (nanos_copy_data_t**) 0);
    if (err != NANOS_OK) 
      nanos_handle_error(err);
    (*init_func)(empty_data, data);

    err = nanos_submit(wd, num_dependences, (nanos_dependence_t*) 0, (nanos_team_t) 0);
    if (err != NANOS_OK) 
      nanos_handle_error(err);
  }

  // Create the wd for the master thread, which will run the team
  dyn_props.tie_to = _nanos_threads[0];
  nanos_create_wd_and_run_compact(&_const_def.base, &dyn_props, data_size, data, 
                          num_dependences, (nanos_dependence_t*) 0, (nanos_copy_data_t*) 0, (void *) 0);
  if (err != NANOS_OK)
    nanos_handle_error(err);

  // End the team
  err = nanos_end_team(_nanos_team);
  if (err != NANOS_OK)
    nanos_handle_error(err);
}

void NANOX_task(void (*func) (void *), void *data,
                long data_size, long (*get_data_align) (void), bool if_clause, unsigned untied, 
                void* empty_data, void (*init_func) (void *, void *))
{
  // Compute copy data (For SMP devices there are no copies. Just CUDA device requires copy data)
  int num_copies = 0;
  // Compute dependencies (ROSE is not currently supporting dependencies among the tasks)
  int num_dependences = 0;
  // Compute the alignement of the struct with the arguments to the outlined function
  long data_align = (*get_data_align)();

  // Create the Device descriptor (at the moment, only SMP is supported)
  int num_devices = 1;
  nanos_smp_args_t _smp_args = { func };
  struct nanos_const_wd_definition_local_t
  {
    nanos_const_wd_definition_t base;
    nanos_device_t devices[1];
  };
  struct nanos_const_wd_definition_local_t _const_def = {
    {
      {
        0,                  /* mandatory creation => only CUDA requieres mandatory creation */
        1,                  /* tied   FIXME this value should depend on the parameter 'untied' */
        0, 0, 0, 0, 0, 0,   /* reserved0..5 */
        0                   /* priority */
      },
      data_align,
      num_copies,
      num_devices
    },
    {{  // device description
        nanos_smp_factory,
        &_smp_args
    }}
  };

  // Compute properties of the WD: mandatory creation, priority, tiedness, real-time info and copy declarations
  nanos_wd_dyn_props_t dyn_props = {0};

  // Create the wd
  nanos_wd_t wd = (nanos_wd_t) 0;
  nanos_err_t err = nanos_create_wd_compact(&wd, &_const_def.base, &dyn_props, data_size, (void**) &empty_data,
                        nanos_current_wd(), (nanos_copy_data_t **) 0);
  if (err != NANOS_OK) 
    nanos_handle_error (err);

  if (wd != (nanos_wd_t)0)
  { // Submit the task to the existing actual working group
    (*init_func)(empty_data, data);

    err = nanos_submit(wd, num_dependences, (nanos_dependence_t *) 0, (nanos_team_t) 0);
    if (err != NANOS_OK) 
      nanos_handle_error (err);
  }
  else
  { // The task must be run immediately
    err = nanos_create_wd_and_run_compact(&_const_def.base, &dyn_props, data_size, data, num_dependences,
                                          (nanos_dependence_t *) 0, (nanos_copy_data_t *) 0, (void *) 0);
    if (err != NANOS_OK) 
      nanos_handle_error (err);
  }
}

/* Variables used during the creation of the slicer to execute OpenMP loop constructs */
// FIXME  scheduling "auto" is only available from version "omp, 5" of Nanos++
//        we have to implement a mechanism version of the family
static nanos_ws_t ws_policy[3];
void nanos_omp_initialize_worksharings(void *dummy)
{
  ws_policy[0] = nanos_find_worksharing("static_for");
  ws_policy[1] = nanos_find_worksharing("dynamic_for");
  ws_policy[2] = nanos_find_worksharing("guided_for");
  // ws_policy[3] = nanos_omp_find_worksharing("omp_sched_auto");
}
__attribute__((weak, section("nanos_post_init")))
nanos_init_desc_t __section__nanos_init_worksharing = {(nanos_omp_initialize_worksharings), ((void *)0)};

void NANOX_loop(int start, int end, int incr, int chunk, int policy, 
                void (*func) (void *), void *data, void * data_wsd /*data_wsd == &(data->wsd)*/, long data_size, long (*get_data_align)(void),
                void* empty_data, void (*init_func) (void *, void *))
{
  nanos_err_t err;

  // Create the worksharing descriptor
  _Bool single_guard;

  // Policy of schedules that are not initialized in "nanos_omp_initialize_worksharings"
  // have a value greater or equal to 10 (using 10 because it is a round number 
  // bigger than the number of policies defined in OpenMP specifics)
  if (policy >= 10)
  {
    omp_sched_t _runtime_sched;
    err = nanos_omp_get_schedule(&_runtime_sched, &chunk);
    if (err != NANOS_OK)
        nanos_handle_error(err);
    policy = _runtime_sched - 1;
  }
  nanos_ws_t* current_ws_policy = &ws_policy[policy];
  nanos_ws_info_loop_t info_loop = { start, end, incr, chunk };

  err = nanos_worksharing_create(data_wsd, *current_ws_policy, (nanos_ws_info_t*) &info_loop, &single_guard);
  if (err != NANOS_OK)
    nanos_handle_error(err);

  if (single_guard)
  {
    int sup_threads;
    err = nanos_team_get_num_supporting_threads(&sup_threads);
    if (err != NANOS_OK)
      nanos_handle_error(err);
    
    if (sup_threads)
    {
      // Get the supporting threads of the current team
      (*((nanos_ws_desc_t**)data_wsd))->threads = (nanos_thread_t *) __builtin_alloca(sizeof(nanos_thread_t) * sup_threads);
      err = nanos_team_get_supporting_threads(&(*((nanos_ws_desc_t**)data_wsd))->nths, (*((nanos_ws_desc_t**)data_wsd))->threads);
      if (err != NANOS_OK)
        nanos_handle_error(err);
      nanos_wd_t wd = (nanos_wd_t) 0;
      nanos_wd_props_t props;
      __builtin_memset(&props, 0, sizeof (props));
      props.mandatory_creation = 1;
      props.tied = 1;
      nanos_wd_dyn_props_t dyn_props = {0};

      // Compute copy data (For SMP devices there are no copies. Just CUDA device requires copy data)
      int num_copies = 0;
      // Compute dependencies (ROSE is not currently supporting dependencies among the tasks)
      int num_dependences = 0;

      // Create the Device descriptor (at the moment, only SMP is supported)
      // FIXME This is still using the old interface
      int num_devices = 1;
      nanos_smp_args_t _smp_args = { func };
      nanos_device_t _current_devices[] = {{ nanos_smp_factory, &_smp_args }};

      // Create the slicer
      // Current version of Nanos only supports 'replicate' slicer
      static nanos_slicer_t replicate = 0;
      if (!replicate)
          replicate = nanos_find_slicer("replicate");
      if (replicate == 0)
          fprintf(stderr, "Cannot find replicate slicer plugin\n");
      long data_align = (*get_data_align)();
      err = nanos_create_sliced_wd(&wd, num_devices, _current_devices, data_size, data_align, (void **) &empty_data, nanos_current_wd(), replicate, &props, &dyn_props, num_copies, (nanos_copy_data_t **) 0);
      if (err != NANOS_OK)
          nanos_handle_error(err);
      (*init_func)(empty_data, data);

      // Submit the work to the runtime system
      err = nanos_submit(wd, num_dependences, (nanos_dependence_t *) 0, (nanos_team_t) 0);
      if (err != NANOS_OK)
          nanos_handle_error(err);
    }

    func(data);
    nanos_omp_barrier();
  }
}

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
  nanos_err_t err;

  _Bool single_guard;
  err = nanos_omp_single(&single_guard);
  if (single_guard)
  {
    // Create the workdescriptor for each section
    nanos_wd_t _wd_section_list[num_sections];
    int i;
    for (i = 0; i < num_sections; ++i)
    {
      // Get the parameters of the current section
      void (*func) (void*) = va_arg(sections_args, void (*) (void*));
      void* data = va_arg(sections_args, void*);
      long data_size = va_arg(sections_args, long);
      long (*get_data_align)(void) = va_arg(sections_args, long (*)(void));
      void* empty_data = va_arg(sections_args, void*);
      void (*init_func)(void*, void*) = va_arg(sections_args, void (*)(void*, void*));

      // Compute copy data (For SMP devices there are no copies. Just CUDA device requires copy data)
      int num_copies = 0;

      // Create the Device descriptor (at the moment, only SMP is supported)
      int num_devices = 1;
      nanos_smp_args_t _smp_args = { func };
      struct nanos_const_wd_definition_local_t
      {
        nanos_const_wd_definition_t base;
        nanos_device_t devices[1];
      };
      struct nanos_const_wd_definition_local_t _const_def = {
        {
          {
            1,                  /* mandatory creation => only CUDA requieres mandatory creation */
            0,                  /* tied */
            0, 0, 0, 0, 0, 0,   /* reserved0..5 */
            0                   /* priority */
          },
          (*get_data_align)(),
          num_copies,
          num_devices
        },
        {{  // device description
            nanos_smp_factory,
            &_smp_args
        }}
      };

      // Create the wd
      nanos_wd_t wd = (nanos_wd_t) 0;
      nanos_wd_dyn_props_t dyn_props = {0};
      err = nanos_create_wd_compact(&wd, &_const_def.base, &dyn_props, data_size, (void **) &empty_data, nanos_current_wd(), (nanos_copy_data_t **) 0);
      if (err != NANOS_OK)
          nanos_handle_error(err);
      (*init_func)(empty_data, data);
      _wd_section_list[i] = wd;
    }

    // Compute copy data (For SMP devices there are no copies. Just CUDA device requires copy data)
    int num_copies = 0;
    // Compute dependencies (ROSE is not currently supporting dependencies among the tasks)
    int num_dependences = 0;

    // Create master workdescriptor and submit the work
    nanos_wd_props_t props;
    __builtin_memset(&props, 0, sizeof (props));
    props.mandatory_creation = 1;
    nanos_wd_dyn_props_t dyn_props = {0};
    nanos_slicer_t compound_slicer = nanos_find_slicer("compound_wd");
    void *compound_dev = (void *) 0;
    nanos_slicer_get_specific_data(compound_slicer, &compound_dev);
    nanos_smp_args_t compound_devices_args = {(void (*)(void *)) compound_dev};
    nanos_device_t compound_device[1] = {{
        nanos_smp_factory,
        &compound_devices_args
    }};
    nanos_compound_wd_data_t *list_of_wds = (nanos_compound_wd_data_t *) 0;
    nanos_wd_t wd = (nanos_wd_t) 0;
    err = nanos_create_sliced_wd(&wd, 1, compound_device, sizeof(nanos_compound_wd_data_t) + num_sections * sizeof(nanos_wd_t), 
                                 __alignof__(nanos_compound_wd_data_t), (void **) &list_of_wds, nanos_current_wd(), 
                                 compound_slicer, &props, &dyn_props, num_copies, (nanos_copy_data_t **) 0);
    if (err != NANOS_OK)
        nanos_handle_error(err);
    list_of_wds->nsect = num_sections;
//     __builtin_memcpy(list_of_wds->lwd, _wd_section_list, sizeof (_wd_section_list));
    err = nanos_submit(wd, num_dependences, (nanos_dependence_t *) 0, (nanos_team_t) 0);
    if (err != NANOS_OK)
        nanos_handle_error(err);
  }

  if (must_wait)
    nanos_omp_barrier();
}

void NANOX_barrier( void )
{
  nanos_team_barrier();
}

void NANOX_taskwait( void )
{
  nanos_wg_t wg = nanos_current_wd();
  nanos_wg_wait_completion(wg, 0);
}

void NANOX_critical_start( void )
{
  nanos_set_lock(&_nx_default_critical_lock);
}

void NANOX_critical_end( void )
{
  nanos_unset_lock(&_nx_default_critical_lock);
}

void NANOX_atomic ( int op, int type, void * variable, void * operand )
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
      float tmp = *((float *) operand);
  
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

bool NANOX_single( void )
{
  bool single_guard;
  nanos_err_t err = nanos_single_guard(&single_guard);
  if (err != NANOS_OK)
    nanos_handle_error(err);

  return single_guard;
}

bool NANOX_master( void )
{
  return (omp_get_thread_num() == 0);
}

void NANOX_flush( void )
{
  __sync_synchronize();
}