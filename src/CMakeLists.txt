# sw-impl: the plugin, as a static library exporting clap_init / clap_deinit /
# clap_get_factory. clap-wrapper links it behind a per-format entry stub, so
# this is where essentially all development happens.
#
# SPDX-License-Identifier: GPL-3.0-or-later

# gui.cmake first: sw-gui-widgets links sw-io, which dsp.cmake defines. CMake
# resolves target names at generate time either way, but reading it in dependency
# order is kinder.
#
# \note This used to read "sw-dsp links sw-gui-resources for the skin its module
# class now reaches". It does not: a module's strip belongs to the editor now and
# sw-dsp links no JUCE at all -- see the note over sw::assets in dsp.cmake, and
# checkNoJuceInDSP.cmake, which fails the build if it comes back.
include(${CMAKE_CURRENT_SOURCE_DIR}/buildStamp.cmake) # sw-gui links it
include(${CMAKE_CURRENT_SOURCE_DIR}/gui.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/dsp.cmake)

add_library(sw-impl STATIC
        spectrumWorxCLAP.cpp
        swClapEntryImpl.cpp
        clapHelpersInstantiation.cpp

        # The host-facing half of the core. Deliberately not in sw-dsp: these
        # are what a plugin format talks to, and the golden tests must not need
        # them. plugin2Host.cpp holds the real parameter enumeration -- the
        # statics over Program const* that clap_plugin_params is made of.
        core/host_interop/host2Plugin.cpp
        core/host_interop/plugin2Host.cpp
)

sw_force_include_odr_header(sw-impl)

target_include_directories(sw-impl PUBLIC .)

# PUBLIC: the per-format entry stubs compile against sw-impl's interface only,
# and they include <clap/clap.h>.
target_link_libraries(sw-impl PUBLIC clap)

# The engine, by way of the module layer that its factory's vtables point at.
#
# PRIVATE, and it is the link line's usage requirements that decide it rather
# than ours: sw-gui-resources links juce::juce_gui_basics PUBLIC, a JUCE module
# target carries its own .cpp files as INTERFACE_SOURCES, and a PUBLIC chain
# hands those sources to every target downstream to compile again. That chain
# ended at the bundles, so each of them compiled the whole of JUCE a second time
# on top of the copy already inside sw-impl, and then linked both. It is not only
# the bundles that pay: one Windows build log counts fifteen compilations of
# juce_core.cpp, and this change removes the ones nothing could use.
#
#   Three linkers had been quietly covering for it. ld and link.exe take an
# archive member only when it resolves something still missing, so the second
# copy was never pulled and nobody paid for it twice at run time; lld-link takes
# it and reports every symbol in it as a duplicate, which is what a clang-cl
# build failed on. PRIVATE still puts sw-gui on the bundles' link line -- CMake
# records it as $<LINK_ONLY:sw-gui> -- while keeping the sources on this side of
# it. The bundles link one JUCE and compile none.
#
#   What this costs: a consumer that wants sw-gui's headers has to say so.
# tests/CMakeLists.txt does, and so does tools/show-ui.
#
# \note It used to carry the force-include across this link line as well, which
# is how clap-wrapper's sources came to be compiled with a header of ours. They
# are not, now: sw_force_include_odr_header() above names sw-impl's own sources.
# The entry stubs in clap-first/ include swClapEntryImpl.hpp, which is written to
# need nothing from it.
target_link_libraries(sw-impl PRIVATE sw-gui)

target_link_libraries(sw-impl PRIVATE
        clap-helpers
        clap-wrapper-extensions
        fmt-header-only
        sst-plugininfra
        sst-plugininfra::version_information
        sst::clap_juce_shim
        sst::clap_juce_shim_headers

        # spectrumWorxCLAP.cpp opens the editor. It reached JUCE through
        # juce::juce_gui_basics and so compiled the module here as well; sw-juce
        # is the same headers with the sources already built.
        sw-juce
)

# clap_juce_shim.h is included from spectrumWorxCLAP.hpp, and the ADD_SHIM_*
# macros need SHIM_MAC/SHIM_LINUX/SHIM_WINDOWS from this interface target.
target_link_libraries(sw-impl PUBLIC sst::clap_juce_shim_headers)

target_compile_definitions(sw-impl PRIVATE
        PRODUCT_NAME="${PRODUCT_NAME}"
        SW_CLAP_ID="${SW_CLAP_ID}"
        SW_VENDOR="${SW_VENDOR}"
        SW_VENDOR_URL="${SW_VENDOR_URL}"
        JUCE_STANDALONE_APPLICATION=0
)
