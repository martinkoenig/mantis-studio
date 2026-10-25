set(expected 92197ce99da9b6b86a3a6139c4bdf2ab696ec3d8)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${FIRMWARE_DIR}" rev-parse HEAD
  RESULT_VARIABLE result OUTPUT_VARIABLE head OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${FIRMWARE_DIR}" status --porcelain
  RESULT_VARIABLE status OUTPUT_VARIABLE changes ERROR_QUIET)
if(NOT result EQUAL 0 OR NOT head STREQUAL expected OR NOT status EQUAL 0 OR NOT changes STREQUAL "")
  message(FATAL_ERROR "Live F2 interoperability requires a clean firmware checkout at ${expected}: ${FIRMWARE_DIR}")
endif()
