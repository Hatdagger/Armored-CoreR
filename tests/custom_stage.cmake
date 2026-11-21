# The custom_stage test: builds tools/maps/make_sample.py's Tinkercad-style
# zip, converts it with tools/maps/tinkercad_stage.py, then loads the stage
# with the engine's parsers (test_custom_stage).
set(OUT "${BIN}/custom_stage")
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")
execute_process(COMMAND "${PY}" -I "${SRC}/tools/maps/make_sample.py" "${OUT}/sample.zip" RESULT_VARIABLE r)
if(r)
  message(FATAL_ERROR "make_sample.py failed")
endif()
execute_process(COMMAND "${PY}" -I "${SRC}/tools/maps/tinkercad_stage.py" "${OUT}/sample.zip" "${OUT}" --id 50 --name "Sample Hall" RESULT_VARIABLE r)
if(r)
  message(FATAL_ERROR "tinkercad_stage.py failed")
endif()
execute_process(COMMAND "${TEST}" "${OUT}/stages/50" RESULT_VARIABLE r)
if(r)
  message(FATAL_ERROR "test_custom_stage failed")
endif()
