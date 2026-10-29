# MafiaHub/Framework dependency pin.
#
# Mafia1Online links a prebuilt Framework SDK (cmake/FrameworkBootstrap.cmake). This file records the
# release whose archives are downloaded at configure time and the SHA-256 of each archive. A tag is a
# mutable ref, so the hashes are what fix the content; file(DOWNLOAD EXPECTED_HASH) fails the
# configure if an archive is ever repointed or tampered with.
#
# Plain set(), not CACHE entries: a cached value would survive in an existing build tree, so bumping
# the pin here and reconfiguring incrementally would silently keep the old release.
#
# Raising this pin is a release-affecting change: rebuild and re-test the client and the server
# together. Crossing a Framework MAJOR version changes the network build token, so every peer must
# update together. Every Framework release's notes carry this block ready to paste.
# -DFW_SDK_ROOT or -DFW_SOURCE_DIR (see builds/build.bat) override it.
#
# Before the SDK the pin was the Framework commit in FRAMEWORK_PIN and Mafia1Online built nested in
# the Framework tree under code/projects.
set(FRAMEWORK_PIN_VERSION "40.0.0")

# One archive per OS; the Windows one carries the x86 (client, launcher) and x64 (server) libraries.
set(FRAMEWORK_PIN_SHA256_windows "07c96c9f781a378bdedfb5ef33e72f205a4c6d33d7b915c2cfd00b107f88b90b")
set(FRAMEWORK_PIN_SHA256_linux "64cab92c8fe5e53f80185f4c25b1686b0e41c1a8beef29504bdd55ce4fab4154")
