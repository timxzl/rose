add_subdirectory(SageIII)
add_subdirectory(BinaryDisassembly)
add_subdirectory(BinaryLoader)
add_subdirectory(BinaryFormats)
add_subdirectory(Disassemblers)
add_subdirectory(PHPFrontend)

# These subdirectories contains logic to handle the "language disabled" case.
add_subdirectory(OpenFortranParser_SAGE_Connection)
add_subdirectory(ECJ_ROSE_Connection)

if(enable-c)
  add_subdirectory(CxxFrontend)
endif()

if(enable-python)
  add_subdirectory(PythonFrontend)
endif()
