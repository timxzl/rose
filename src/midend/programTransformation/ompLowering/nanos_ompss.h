/*!
 * Support for OmpSs
 *
 * Sara Royuela, 12/4/2012
 */
#ifndef NANOS_OMPSS_H
#define NANOS_OMPSS_H 

int nanos_max_thread_num = 256;

//! The directionality of a Task Dependency Clause
enum ompss_dependency_direction_enum {
    e_dep_dir_in = 0x0001,
    e_dep_dir_out = 0x0002,
    e_dep_dir_inout = 0x0004/*,
    e_dep_dir_concurrent*/
};

#endif      // NANOS_OMPSS_H