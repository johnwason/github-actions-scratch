add_library(bar SHARED bar.cpp)
set_target_properties(bar PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
