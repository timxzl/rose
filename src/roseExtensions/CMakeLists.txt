add_subdirectory(sqlite3x)
add_subdirectory(dataStructureTraversal)
add_subdirectory(highLevelGrammar)

if(enable-rosehpct)
  add_subdirectory(roseHPCToolkit)
endif()

if(with-roseQt)
  add_subdirectory(qtWidgets)
endif()
