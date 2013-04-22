AC_DEFUN([ROSE_WITH_NANOS_OPENMP_LIBRARY],
[
# Check if Nanos OpenMP runtime library is available
# Begin macro ROSE_WITH_NANOS_OPENMP_LIBRARY.
# Inclusion of test for NANOS OpenMP Runtime system and its location.

AC_MSG_CHECKING(for OpenMP using Nanos++ runtime library)
AC_ARG_WITH(nanos_omp_runtime_library,
[  --with-nanos_omp_runtime_library=PATH	Specify the prefix where NANOS Runtime System is installed],
,
if test ! "$with_nanos_omp_runtime_library" ; then
   with_nanos_omp_runtime_library=no
fi
)

echo "In ROSE SUPPORT MACRO: with_nanos_omp_runtime_library $with_nanos_omp_runtime_library"

if test "$with_nanos_omp_runtime_library" = no; then
   # If nanos_omp_runtime_library is not specified, then don't use it.
   echo "Skipping use of NANOS OpenMP Runtime Library!"
else
   nanos_omp_runtime_library_path=$with_nanos_omp_runtime_library
   echo "Setup NANOS OpenMP library in ROSE! path = $nanos_omp_runtime_library_path"
   AC_DEFINE([USE_ROSE_NANOS_OPENMP_LIBRARY],1,[Controls use of ROSE support for OpenMP Translator targeting NANOS OpenMP RTL.])
   AC_DEFINE_UNQUOTED([NANOS_OPENMP_LIB_PATH],"$nanos_omp_runtime_library_path",[Location (unquoted) of the NANOS OpenMP runtime library.])
fi

AC_DEFINE_UNQUOTED([ROSE_INSTALLATION_PATH],"$prefix",[Location (unquoted) of the top directory path to which ROSE is installed.])

AC_SUBST(nanos_omp_runtime_library_path)

# End macro ROSE_WITH_NANOS_OPENMP_LIBRARY.
AM_CONDITIONAL(WITH_NANOS_OPENMP_LIB,test ! "$with_nanos_omp_runtime_library" = no)

]
)
