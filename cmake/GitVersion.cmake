# SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
# SPDX-License-Identifier: MIT

# Keep the numeric CMake/MSI version separate from the prerelease label.
# Parse the distance from the RIGHT so bluetooth.1 is never mistaken for it.
function(deskconnect_parse_git_version revision)
  if(NOT "${revision}" MATCHES "^(v[0-9]+\\.[0-9]+\\.[0-9]+(\\.[0-9]+)?(-[A-Za-z0-9.-]+)?)-([0-9]+)-g[0-9a-f]+$")
    return()
  endif()
  set(version_tag "${CMAKE_MATCH_1}")
  set(version_distance "${CMAKE_MATCH_4}")
  if("${version_tag}" MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)(\\.[0-9]+)?(-[A-Za-z0-9.-]+)?$")
    set(DESKFLOW_VERSION_MAJOR "${CMAKE_MATCH_1}" PARENT_SCOPE)
    set(DESKFLOW_VERSION_MINOR "${CMAKE_MATCH_2}" PARENT_SCOPE)
    set(DESKFLOW_VERSION_PATCH "${CMAKE_MATCH_3}" PARENT_SCOPE)
    set(DESKFLOW_VERSION_TWEAK "${version_distance}" PARENT_SCOPE)
    set(DESKCONNECT_VERSION_SUFFIX "${CMAKE_MATCH_5}" PARENT_SCOPE)
  endif()
endfunction()
