#
# Wrapper for CPackRPM.cmake
#

# load the original CPackRPM.cmake
set(orig_CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH})
unset(CMAKE_MODULE_PATH)
include(CPackRPM)
set(CMAKE_MODULE_PATH ${orig_CMAKE_MODULE_PATH})

# per-component cleanup
foreach(_RPM_SPEC_HEADER URL REQUIRES SUGGESTS PROVIDES OBSOLETES PREFIX CONFLICTS AUTOPROV AUTOREQ AUTOREQPROV)
  unset(TMP_RPM_${_RPM_SPEC_HEADER})
  unset(CPACK_RPM_PACKAGE_${_RPM_SPEC_HEADER}_TMP)
endforeach()

