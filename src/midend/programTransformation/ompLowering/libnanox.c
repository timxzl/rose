#include "libnanox.h"

#include <omp.h>
#include <stdio.h>
#include <string.h>

__attribute__((weak)) nanos_lock_t _nx_default_critical_lock = {NANOS_LOCK_FREE};
// FIXME For c++ we just need "__attribute__((weak)) nanos_lock_t _nx_default_critical_lock;"

void NANOX_parallel_start(void (*func) (void *), void *data, unsigned numThreads, long arg_size, long (*get_arg_align)(void), 
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
  int num_dependencies = 0;
  // Compute the alignement of the struct with the arguments to the outlined function
  long arg_align = (*get_arg_align)();

  // Create the Device descriptor (at the moment, only SMP is supported)
  int num_devices = 1;
  nanos_smp_args_t _smp_args = {(void (*)(void *)) func};
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
      arg_align,
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
//     void* empty_data = get_empty_data();
    err = nanos_create_wd_compact(&wd, &_const_def.base, &dyn_props, arg_size, (void**)&empty_data, 
                          nanos_current_wd(), (nanos_copy_data_t**) 0);
    if (err != NANOS_OK) 
      nanos_handle_error(err);

    (*init_func)(empty_data, data);

    err = nanos_submit(wd, num_dependencies, (nanos_dependence_t*) 0, (nanos_team_t) 0);
    if (err != NANOS_OK) 
      nanos_handle_error(err);
  }

  // Create the wd for the master thread, which will run the team
  dyn_props.tie_to = _nanos_threads[0];
  nanos_create_wd_and_run_compact(&_const_def.base, &dyn_props, arg_size, data, 
                          num_dependencies, (nanos_dependence_t*) 0, (nanos_copy_data_t*) 0, (void *) 0);
  if (err != NANOS_OK)
    nanos_handle_error(err);

  // End the team
  err = nanos_end_team(_nanos_team);
  if (err != NANOS_OK)
    nanos_handle_error(err);
}

void NANOX_task(void (*func) (void *), void *data,
                long arg_size, long (*get_arg_align) (void), bool if_clause, unsigned untied, 
                void* empty_data, void (*init_func) (void *, void *))
{
  // Compute copy data (For SMP devices there are no copies. Just CUDA device requires copy data)
  int num_copies = 0;
  // Compute dependencies (ROSE is not currently supporting dependencies among the tasks)
  int num_dependences = 0;
  // Compute the alignement of the struct with the arguments to the outlined function
  long arg_align = (*get_arg_align)();

  // Create the Device descriptor (at the moment, only SMP is supported)
  int num_devices = 1;
  nanos_smp_args_t _smp_args = {(void (*)(void *)) func};
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
      arg_align,
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
  nanos_err_t err = nanos_create_wd_compact(&wd, &_const_def.base, &dyn_props, arg_size, (void**) &empty_data,
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
    err = nanos_create_wd_and_run_compact(&_const_def.base, &dyn_props, arg_size, data, num_copies,
                                          (nanos_dependence_t *) 0, (nanos_copy_data_t *) 0, (void *) 0);
    if (err != NANOS_OK) 
      nanos_handle_error (err);
  }
}

static nanos_ws_t ws_policy[3];
void nanos_omp_initialize_worksharings(void *dummy)
{
  ws_policy[0] = nanos_find_worksharing("static_for");
  ws_policy[1] = nanos_find_worksharing("dynamic_for");
  ws_policy[2] = nanos_find_worksharing("guided_for");
}
__attribute__((weak, section("nanos_post_init")))
nanos_init_desc_t __section__nanos_init_worksharing = {(nanos_omp_initialize_worksharings), ((void *)0)};

void NANOX_loop(int start, int end, int incr, int chunk, int policy, 
                void (*func) (void *), void *data, void * data_wsd /*data_wsd == &(data->wsd)*/, long arg_size, long (*get_arg_align)(void),
                void* empty_data, void (*init_func) (void *, void *)/*, void* ws_policy*/)
{
  nanos_err_t err;

  // Create the worksharing descriptor
  _Bool single_guard;
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
    
    printf("The number of supporting threads is %d\n", sup_threads);
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

      // Create the Device descriptor (at the moment, only SMP is supported)
      // FIXME This is still using the old interface
      int num_devices = 1;
      nanos_smp_args_t _smp_args = {(void (*)(void *)) func};
      nanos_device_t _current_devices[] = {{ nanos_smp_factory, &_smp_args }};

      // Create the slicer
      // Current version of Nanos only supports 'replicate' slicer
      static nanos_slicer_t replicate = 0;
      if (!replicate)
          replicate = nanos_find_slicer("replicate");
      if (replicate == 0)
          fprintf(stderr, "Cannot find replicate slicer plugin\n");
      long arg_align = (*get_arg_align)();
      err = nanos_create_sliced_wd(&wd, num_devices, _current_devices, arg_size, arg_align, (void **) &empty_data, nanos_current_wd(), replicate, &props, &dyn_props, 0, (nanos_copy_data_t **) 0);
      if (err != NANOS_OK)
          nanos_handle_error(err);
      (*init_func)(empty_data, data);

      // Submit the work to the runtime system
      err = nanos_submit(wd, 0, (nanos_dependence_t *) 0, (nanos_team_t) 0);
      if (err != NANOS_OK)
          nanos_handle_error(err);
    }

    func(data);
    nanos_omp_barrier();
  }
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

void NANOX_todo(char * func)
{
  fprintf(stderr, "Method '%s' in NANOX not yet implemented\n", func);
}