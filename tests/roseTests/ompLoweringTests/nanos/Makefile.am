include $(top_srcdir)/config/Makefile.for.ROSE.includes.and.libs

NANOS_PATH 	= @nanos_omp_runtime_library_path@

# Put test codes without main() entry here
# They will be compiled only, not executed.
C_TESTCODES_TO_COMPILE =

# Put test codes which can run (with main() ) here
# They will be compiled and executed when a RTL library path is set
C_TESTCODES_TO_RUN = \
    deps_array_access_01.c \
	deps_sections_02.c \
	deps_sections_03.c \
	deps_shaping_01.c \
	deps_shaping_02.c
# deps_sections_01.c
	
# we reuse the OpenMP sources files from the OpenMP compile test nanos directory
TEST_DIR = $(top_srcdir)/tests/CompileTests/OpenMP_tests/nanos

# the place to find omp.h, libnanos.h, etc.
TEST_INCLUDES = -I$(top_srcdir)/src/frontend/SageIII -I$(top_srcdir)/src/midend/programTransformation/ompLowering

# required flags
TEST_FLAGS = $(TEST_INCLUDES) -rose:openmp:lowering -g --edg:no_warnings

../roseomp:
	make -C ../. roseomp

# generate the binaries
C_TESTCODES_TO_COMPILE_Objects = ${C_TESTCODES_TO_COMPILE:.c=.o}
C_TESTCODES_TO_RUN_Objects = ${C_TESTCODES_TO_RUN:.c=.o}

$(C_TESTCODES_TO_RUN_Objects): %.o: $(TEST_DIR)/%.c ../roseomp
	../roseomp$(EXEEXT) ${TEST_FLAGS} -c $<
	
# compile and run using Nanos
C_TESTCODES_TO_RUN_Executables = ${C_TESTCODES_TO_RUN_Objects:.o=.out}

MY_FINAL_LINK = -L$(top_builddir)/src/midend -L$(NANOS_PATH)/debug -lxomp -lnanox-c -lnanox-omp -lnanox-ss -lpthread -lm
$(C_TESTCODES_TO_RUN_Executables): %.out: %.o
	$(LIBTOOL) --mode=link $(CC) $< -o $@ $(MY_FINAL_LINK)
check_PROGRAM = $(C_TESTCODES_TO_RUN_Executables)
TESTS =  $(check_PROGRAM)

check-local: ../roseomp
	@echo "Test for ROSE OpenMP lowering with NANOS RTL library."
	@echo "***************** Testing C input *******************"
	$(MAKE) $(C_TESTCODES_TO_RUN_Objects)
	@echo "***********************************************************************************************************"
	@echo "******* ROSE/tests/roseTests/ompLoweringTests/nanos: make check rule complete (terminated normally) *******"
	@echo "***********************************************************************************************************"
	
clean-local:
	rm -f *.o rose_*.c *.out
	