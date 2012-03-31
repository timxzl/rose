AC_DEFUN([ROSE_WITH_NANOX_OPENMP_LIBRARY],
[
# Check if Nanox OpenMP runtime library is available
# Begin macro ROSE_WITH_NANOX_OPENMP_LIBRARY.
# Inclusion of test for NANOX OpenMP Runtime system and its location.

AC_MSG_CHECKING(for OpenMP using nanox runtime library)
AC_ARG_WITH(nanox_omp_runtime_library,
[  --with-nanox_omp_runtime_library=PATH	Specify the prefix where NANOX Runtime System is installed],
,
if test ! "$with_nanox_omp_runtime_library" ; then
   with_nanox_omp_runtime_library=no
fi
)

echo "In ROSE SUPPORT MACRO: with_nanox_omp_runtime_library $with_nanox_omp_runtime_library"

if test "$with_nanox_omp_runtime_library" = no; then
   # If nanox_omp_runtime_library is not specified, then don't use it.
   echo "Skipping use of NANOX OpenMP Runtime Library!"
else
   nanox_omp_runtime_library_path=$with_nanox_omp_runtime_library
   echo "Setup NANOX OpenMP library in ROSE! path = $nanox_omp_runtime_library_path"
   AC_DEFINE([USE_ROSE_NANOX_OPENMP_LIBRARY],1,[Controls use of ROSE support for OpenMP Translator targeting NANOX OpenMP RTL.])
   AC_DEFINE_UNQUOTED([NANOX_OPENMP_LIB_PATH],"$nanox_omp_runtime_library_path",[Location (unquoted) of the NANOX OpenMP runtime library.])
fi

AC_DEFINE_UNQUOTED([ROSE_INSTALLATION_PATH],"$prefix",[Location (unquoted) of the top directory path to which ROSE is installed.])

AC_SUBST(nanox_omp_runtime_library_path)

# End macro ROSE_WITH_NANOX_OPENMP_LIBRARY.
AM_CONDITIONAL(WITH_NANOX_OPENMP_LIB,test ! "$with_nanox_omp_runtime_library" = no)

]
)
