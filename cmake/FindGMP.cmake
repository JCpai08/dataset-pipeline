find_path(GMP_INCLUDE_DIRS NAMES gmp.h)
find_library(GMP_LIBRARIES NAMES gmp libgmp)
find_library(GMPXX_LIBRARIES NAMES gmpxx libgmpxx)
if(GMPXX_LIBRARIES)
  list(APPEND GMP_LIBRARIES ${GMPXX_LIBRARIES})
endif()
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GMP DEFAULT_MSG
GMP_INCLUDE_DIRS
GMP_LIBRARIES)
mark_as_advanced(GMP_INCLUDE_DIRS GMP_LIBRARIES GMPXX_LIBRARIES)
