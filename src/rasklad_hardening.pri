# Linux compiler/linker mitigations for local builds too, rather than relying
# only on future packaging flags. These do not provide process isolation.
linux {
    QMAKE_CXXFLAGS += -fstack-protector-strong
    QMAKE_CXXFLAGS_RELEASE += -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3
    QMAKE_LFLAGS += -Wl,-z,relro,-z,now,-z,noexecstack
}
