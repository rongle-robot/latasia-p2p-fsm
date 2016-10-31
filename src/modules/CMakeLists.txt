ADD_SUBDIRECTORY(apps)

AUX_SOURCE_DIRECTORY(. src_modules)
ADD_LIBRARY(lts_modules STATIC ${src_modules})
TARGET_LINK_LIBRARIES(lts_modules lts_app_p2p_fsm)
