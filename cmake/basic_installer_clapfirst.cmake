# add_clapfirst_installer() -- one target that packages what the four plugin
# formats built.
#
# Taken from two-filters' cmake/basic_installer_clapfirst.cmake, which is the
# shape every clap-first plugin in this family uses, and kept close enough to it
# that a diff is still readable. Three platforms, three different answers:
#
#   macOS    a signed, notarised .pkg inside a .dmg, assembled by
#            sst-plugininfra's scripts/installer_mac/make_installer.sh
#   Windows  a .zip of the raw bundles, plus an Inno Setup .exe if the compiler
#            is on PATH -- the .iss itself is sst-cmake's
#   Linux    a .zip, and a .deb beside it wherever dpkg-deb is found
#
# **Signing is by environment variable, and its absence is not an error.** The
# mac script signs when MAC_SIGNING_CERT is set and quietly does not when it is
# not, so a developer build and a pull request produce an unsigned installer
# through the same code path CI signs with. That is deliberate: the path that
# ships is the path that gets exercised.
#
# SPDX-License-Identifier: GPL-3.0-or-later

function(add_clapfirst_installer)
    set(oneValueArgs
            INSTALLER_TARGET
            ASSET_OUTPUT_DIRECTORY
            PRODUCT_NAME
            INSTALLER_PREFIX
            EXTRA_ZIP_NAME # appended after the system name, e.g. "-aarch64"
    )
    set(multiValueArgs
            TARGETS # the per-format targets, e.g. sw_clap sw_vst3 sw_auv2
    )
    cmake_parse_arguments(CIN "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if ("${CIN_INSTALLER_PREFIX}" STREQUAL "")
        message(FATAL_ERROR "You must set INSTALLER_PREFIX in add_clapfirst_installer")
    endif ()

    if ("${CIN_PRODUCT_NAME}" STREQUAL "")
        message(FATAL_ERROR "You must set PRODUCT_NAME in add_clapfirst_installer")
    endif ()

    if ("${CIN_ASSET_OUTPUT_DIRECTORY}" STREQUAL "")
        message(FATAL_ERROR "You must set ASSET_OUTPUT_DIRECTORY in add_clapfirst_installer")
    endif ()

    set(TGT ${CIN_INSTALLER_TARGET})
    add_custom_target(${TGT})
    foreach (INST ${CIN_TARGETS})
        if (TARGET ${INST})
            message(STATUS "Adding ${INST} to ${TGT} target deps")
            add_dependencies(${TGT} ${INST})
        endif ()
    endforeach ()

    # Date and commit rather than a version number, because these are nightlies
    # far more often than they are releases, and a nightly whose file name does
    # not say which commit it came from cannot be reported against.
    string(TIMESTAMP INST_DATE "%Y-%m-%d")
    set(INST_VERSION "${INST_DATE}-${GIT_COMMIT_HASH}")
    set(INST_ZIP ${CIN_INSTALLER_PREFIX}-${CMAKE_SYSTEM_NAME}${CIN_EXTRA_ZIP_NAME}-${INST_VERSION}.zip)

    set(SW_PACKAGING "${CMAKE_SOURCE_DIR}/assets/installer")
    set(SW_LICENSE "${SW_PACKAGING}/License.txt")

    if (APPLE)
        # \note Not INST_ZIP: macOS makes no zip, and make_installer.sh names
        # the dmg itself, lowercasing the product and spelling the platform
        # "macOS" where CMAKE_SYSTEM_NAME says "Darwin".
        message(STATUS "Installer: macOS pkg/dmg, version ${INST_VERSION}")

        # productbuild takes a whole directory as its resources and reads the
        # licence out of it as `License.txt` -- distribution.xml in
        # make_installer.sh names it -- so the directory is staged in the build
        # tree, with exactly the three files it should contain. Handing it
        # assets/installer/ directly would put the Windows icon and the wizard
        # banner inside the .pkg.
        set(SW_MAC_RESOURCES "${CMAKE_BINARY_DIR}/installer_mac_resources")

        add_custom_command(
                TARGET ${TGT}
                POST_BUILD
                USES_TERMINAL
                VERBATIM
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                COMMAND ${CMAKE_COMMAND} -E make_directory installer
                COMMAND ${CMAKE_COMMAND} -E make_directory "${SW_MAC_RESOURCES}"
                COMMAND ${CMAKE_COMMAND} -E copy
                    "${SW_LICENSE}" "${SW_MAC_RESOURCES}/License.txt"
                COMMAND ${CMAKE_COMMAND} -E copy
                    "${SW_PACKAGING}/entitlements.plist" "${SW_PACKAGING}/icns.rsrc"
                    "${SW_MAC_RESOURCES}"
                COMMAND ${CMAKE_SOURCE_DIR}/libs/sst/sst-plugininfra/scripts/installer_mac/make_installer.sh
                    "${CIN_PRODUCT_NAME}"
                    "${CIN_ASSET_OUTPUT_DIRECTORY}"
                    "${SW_MAC_RESOURCES}"
                    "${CMAKE_BINARY_DIR}/installer"
                    "${INST_VERSION}"
        )
    elseif (WIN32)
        message(STATUS "Installer: installer/${INST_ZIP}, and an exe beside it")
        cmake_path(REMOVE_EXTENSION INST_ZIP OUTPUT_VARIABLE WIN_INSTALLER)

        # The bundles land in per-format subdirectories; Inno and the zip both
        # want them flat, in one place.
        set(WINCOL ${CIN_ASSET_OUTPUT_DIRECTORY}/installer_copy)
        file(MAKE_DIRECTORY ${WINCOL})

        add_custom_target(${TGT}_wincollect)
        foreach (INST ${CIN_TARGETS})
            if (TARGET ${INST})
                message(STATUS "Installer: staging ${INST}")
                add_dependencies(${TGT}_wincollect ${INST})
                add_custom_command(TARGET ${TGT}_wincollect
                        POST_BUILD
                        USES_TERMINAL
                        COMMAND ${CMAKE_COMMAND} -E echo "Staging " $<TARGET_FILE:${INST}> " to " ${WINCOL}
                        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${INST}>" "${WINCOL}"
                )
            endif ()
        endforeach ()

        add_dependencies(${TGT} ${TGT}_wincollect)

        add_custom_command(
                TARGET ${TGT}
                POST_BUILD
                USES_TERMINAL
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                COMMAND ${CMAKE_COMMAND} -E echo "Building the Windows zip"
                COMMAND ${CMAKE_COMMAND} -E make_directory installer
                COMMAND 7z a -r installer/${INST_ZIP} ${WINCOL}
        )

        # sst-cmake defines this when it finds iscc; the CI leg installs Inno
        # Setup before configuring so that it does. A developer without it gets
        # the zip and no complaint.
        if (TARGET innosetup::compiler)
            message(STATUS "Installer: adding the Inno Setup exe")

            add_dependencies(${TGT} innosetup::compiler)
            add_custom_command(
                    TARGET ${TGT}
                    POST_BUILD
                    USES_TERMINAL
                    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                    COMMAND ${CMAKE_COMMAND} -E echo "Building the Windows installer with $<TARGET_PROPERTY:innosetup::compiler,IMPORTED_LOCATION>"
                    COMMAND ${CMAKE_COMMAND} -E make_directory installer
                    COMMAND innosetup::compiler
                        /O"${CMAKE_BINARY_DIR}/installer"
                        /F"${WIN_INSTALLER}"
                        /DName="${CIN_PRODUCT_NAME}"
                        /DNameCondensed="${CIN_INSTALLER_PREFIX}"
                        /DVersion="${INST_VERSION}"
                        # Inno's AppId, which is what an upgrade recognises a
                        # previous install by. It must never change again.
                        /DID="D6EF2470-69A9-486B-A3AC-63DB2A4D3618"
                        /DPublisher="${SW_VENDOR}"
                        /DURL="${SW_VENDOR_URL}"
                        /DCLAP /DVST3 /DVST3_IS_SINGLE_FILE /DSA
                        /DIcon="${SW_PACKAGING}/SpectrumWorxIcon.ico"
                        /DBanner="${SW_PACKAGING}/SpectrumWorxBanner.png"
                        # Asked of the compiler rather than hardcoded, so that
                        # an arm64 runner produces an arm64 installer.
                        /DArch="$<TARGET_PROPERTY:innosetup::compiler,ARCH_ID>"
                        /DLicense="${SW_LICENSE}"
                        /DStagedAssets="${WINCOL}"
                        "$<TARGET_PROPERTY:innosetup::compiler,INSTALL_SCRIPT>"
            )
        else ()
            message(STATUS "Installer: no Inno Setup compiler, so no exe -- zip only")
        endif ()
    else ()
        message(STATUS "Installer: installer/${INST_ZIP}")
        add_custom_command(
                TARGET ${TGT}
                POST_BUILD
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                COMMAND ${CMAKE_COMMAND} -E make_directory installer
                COMMAND ${CMAKE_COMMAND} -E copy "${SW_LICENSE}" ${CIN_ASSET_OUTPUT_DIRECTORY}
                COMMAND ${CMAKE_COMMAND} -E tar cvf installer/${INST_ZIP} --format=zip ${CIN_ASSET_OUTPUT_DIRECTORY}/
                COMMAND ${CMAKE_COMMAND} -E echo "Installer in: installer/${INST_ZIP}")

        # non-debian hosts still get the zip
        find_program(INST_DPKG_DEB dpkg-deb)
        if (INST_DPKG_DEB)
            set(INST_DEB ${CIN_INSTALLER_PREFIX}-${CMAKE_SYSTEM_NAME}${CIN_EXTRA_ZIP_NAME}-${INST_VERSION}.deb)
            if ("${GIT_IMPLIED_DISPLAY_VERSION}" MATCHES "^v([0-9].*)$")
                set(INST_DEB_VERSION "${CMAKE_MATCH_1}")
            else ()
                set(INST_DEB_VERSION "NIGHTLY-${INST_VERSION}")
            endif ()
            message(STATUS "Installer: installer/${INST_DEB}, version ${INST_DEB_VERSION}")

            add_custom_command(
                    TARGET ${TGT}
                    POST_BUILD
                    USES_TERMINAL
                    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                    COMMAND ${CMAKE_COMMAND} -E env DEB_BASE_VERSION=${CMAKE_PROJECT_VERSION_MAJOR}.${CMAKE_PROJECT_VERSION_MINOR}.${CMAKE_PROJECT_VERSION_PATCH}
                        ${CMAKE_SOURCE_DIR}/scripts/installer_linux/make_deb.sh
                        ${CIN_ASSET_OUTPUT_DIRECTORY} ${CMAKE_SOURCE_DIR}
                        ${CMAKE_BINARY_DIR}/installer/${INST_DEB} "${INST_DEB_VERSION}"
                    COMMAND ${CMAKE_COMMAND} -E echo "Deb in: installer/${INST_DEB}"
            )
        endif ()
    endif ()
endfunction()
