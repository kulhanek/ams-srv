# ==============================================================================
# InfiCore CMake File
# ==============================================================================

INSTALL(FILES
            isoftrepo.xml
        DESTINATION
            etc/servers
        PERMISSIONS
            OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
        )

