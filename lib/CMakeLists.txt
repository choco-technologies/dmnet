# Library-type DMOD modules that live under dmnet/lib/.
#
# Each subdirectory is an independent DMOD module (its own CMakeLists.txt
# calling dmod_add_library()), added here rather than directly from the
# top-level CMakeLists.txt.
add_subdirectory(dmnetif)
