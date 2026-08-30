# Registers the repository as an ESP-IDF component under the fixed name
# "SCD41". Resolving it through EXTRA_COMPONENT_DIRS instead would take the
# component name from the checkout directory, which breaks in any clone or
# release archive not named exactly "SCD41".
idf_component_register(
  SRCS "${CMAKE_CURRENT_LIST_DIR}/../../../../../src/SCD41.cpp"
  INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../../../../include"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
