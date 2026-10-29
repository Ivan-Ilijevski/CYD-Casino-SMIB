# Reads the project's .env into compile definitions.
#
# Included by both main/CMakeLists.txt (firmware) and sim/CMakeLists.txt, so the
# board and the desktop simulator agree on the admin passcode and the factory
# network defaults. The values are FACTORY DEFAULTS only: once staff save from
# the admin panel, NVS wins (drivers/settings.h).
#
# .env is gitignored. .env.example is committed and documents every key.
#
#   smib_env_read(<out_var>)   -> list of KEY=VALUE compile definitions
#
# A missing .env is a warning, not an error: a fresh clone must still build.

function(smib_env_read out_var)
    set(_env_file "${SMIB_ENV_DIR}/.env")

    # Defaults for every key, so the firmware always compiles and a board with
    # no configuration comes up parked rather than pointed somewhere random.
    set(_passcode "0000")
    set(_ssid "")
    set(_pass "")
    set(_cms_host "")
    set(_cms_port "9020")

    if(EXISTS "${_env_file}")
        file(STRINGS "${_env_file}" _lines)
        foreach(_line IN LISTS _lines)
            # KEY=VALUE, skipping blanks and # comments. Values are taken
            # verbatim apart from surrounding quotes, so a Wi-Fi password may
            # contain '#', spaces and '='.
            if(_line MATCHES "^[ \t]*#" OR NOT _line MATCHES "=")
                continue()
            endif()
            string(REGEX REPLACE "^[ \t]*([A-Za-z_][A-Za-z0-9_]*)[ \t]*=(.*)$" "\\1" _key "${_line}")
            string(REGEX REPLACE "^[ \t]*([A-Za-z_][A-Za-z0-9_]*)[ \t]*=(.*)$" "\\2" _val "${_line}")
            if(_key STREQUAL _line)
                continue() # not a KEY=VALUE line after all
            endif()
            string(STRIP "${_val}" _val)
            string(REGEX REPLACE "^\"(.*)\"$" "\\1" _val "${_val}")
            string(REGEX REPLACE "^'(.*)'$" "\\1" _val "${_val}")

            if(_key STREQUAL "SMIB_ADMIN_PASSCODE")
                set(_passcode "${_val}")
            elseif(_key STREQUAL "SMIB_WIFI_SSID")
                set(_ssid "${_val}")
            elseif(_key STREQUAL "SMIB_WIFI_PASS")
                set(_pass "${_val}")
            elseif(_key STREQUAL "SMIB_CMS_HOST")
                set(_cms_host "${_val}")
            elseif(_key STREQUAL "SMIB_CMS_PORT")
                set(_cms_port "${_val}")
            endif()
        endforeach()

        if(_passcode STREQUAL "0000")
            message(WARNING
                "SMIB: .env has no SMIB_ADMIN_PASSCODE; the admin panel will "
                "accept the default passcode 0000. Set one before deploying.")
        endif()
    else()
        message(WARNING
            "SMIB: no .env at ${_env_file} — building with the default admin "
            "passcode 0000 and no network configuration. Copy .env.example to "
            ".env and fill it in (see README.md).")
    endif()

    if(NOT _passcode MATCHES "^[0-9][0-9][0-9][0-9]+$")
        message(FATAL_ERROR
            "SMIB: SMIB_ADMIN_PASSCODE must be at least 4 digits, 0-9 only "
            "(the admin keypad on the terminal is numeric). Got '${_passcode}'.")
    endif()
    if(NOT _cms_port MATCHES "^[0-9]+$" OR _cms_port LESS 1 OR _cms_port GREATER 65535)
        message(FATAL_ERROR "SMIB: SMIB_CMS_PORT must be 1-65535. Got '${_cms_port}'.")
    endif()

    # Rebuild when .env changes, so editing the passcode takes effect.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_env_file}")

    set(${out_var}
        "SMIB_ADMIN_PASSCODE=\"${_passcode}\""
        "SMIB_WIFI_SSID_DEFAULT=\"${_ssid}\""
        "SMIB_WIFI_PASS_DEFAULT=\"${_pass}\""
        "SMIB_CMS_HOST_DEFAULT=\"${_cms_host}\""
        "SMIB_CMS_PORT_DEFAULT=${_cms_port}"
        PARENT_SCOPE)
endfunction()
