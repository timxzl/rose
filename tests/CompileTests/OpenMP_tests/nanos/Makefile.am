include $(top_srcdir)/config/Makefile.for.ROSE.includes.and.libs

TESTCODES_REQUIRED_TO_PASS = \
    deps_array_access_01.c \
	deps_sections_01.c \
	deps_sections_02.c \
	deps_sections_03.c \
	deps_shaping_01.c \
	deps_shaping_02.c
	
TESTCODES_FAILING = \
	deps_sections_01.c
	
ALL_TESTCODES = \
  $(TESTCODES_REQUIRED_TO_PASS)
  
ROSE_FLAGS = -rose:C_only --edg:no_warnings -w -rose:verbose 0 --edg:restrict -rose:openmp:ast_only

TESTCODE_INCLUDES = -I$(top_srcdir)/src/frontend/SageIII

TESTCODES_REQUIRED_TO_PASS_Objects = $(TESTCODES_REQUIRED_TO_PASS:.c=.o)
$(TESTCODES_REQUIRED_TO_PASS_Objects): %.o:$(srcdir)/%.c ../parseOmp
	../parseOmp $(ROSE_FLAGS) $(TESTCODE_INCLUDES) -c $<

../parseOmp:
	$(MAKE) -C ../. parseOmp

ALL_TEST_Objects = $(TESTCODES_REQUIRED_TO_PASS_Objects)

check-local:
	@echo "Tests for OpenMP extensions with Nanos RTL."
	@$(MAKE) $(ALL_TEST_Objects)
	@echo "****************************************************************************************************"
	@echo "**** ROSE/tests/CompileTests/OpenMP_tests/nanos: make check rule complete (terminated normally) ****"
	@echo "****************************************************************************************************"

clean-local:
	rm -f *.o rose_*.c
