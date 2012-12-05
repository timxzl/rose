/*!
 * Support for OmpSs
 *
 * Sara Royuela, 12/4/2012
 */
#ifndef NANOS_OMPSS_H
#define NANOS_OMPSS_H 

//! The directionality of a Task Dependency Clause
enum ompss_dependency_direction_enum {
    e_dep_dir_input = 0,
    e_dep_dir_output = 1,
    e_dep_dir_inout = 2/*,
    e_dep_dir_concurrent*/
};

#endif      // NANOS_OMPSS_H