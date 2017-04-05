# ==============================================================================
# AMS CMake File
# ==============================================================================

# program objects --------------------------------------------------------------
SET(PROG_SRC
        AMSStatServerOptions.cpp
        AMSStatServer.cpp
        prefix.c
        )

# final build ------------------------------------------------------------------
ADD_EXECUTABLE(ams-isoftstat ${PROG_SRC})

TARGET_LINK_LIBRARIES(ams-isoftstat ${AMS_FB_LIBS})

INSTALL(TARGETS
            ams-isoftstat
        DESTINATION
            sbin
        )

