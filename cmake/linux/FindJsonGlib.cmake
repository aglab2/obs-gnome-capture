# - Find json-glib
# This module looks for json-glib.
# This module defines the following variables:
#    JSON_GLIB_FOUND
#    JSON_GLIB_INCLUDE_DIRS
#    JSON_GLIB_LIBRARIES

#=============================================================================
# Copyright (C) 2024
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#=============================================================================

include(FindPackageHandleStandardArgs)

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_search_module(PC_JsonGlib QUIET json-glib-1.0)
endif()

find_path(JSON_GLIB_INCLUDE_DIR
  NAMES json-glib/json-glib.h
  HINTS ${PC_JsonGlib_INCLUDE_DIRS}
  PATHS /usr/include /usr/local/include
  PATH_SUFFIXES json-glib-1.0)

find_path(JSON_GLIB_GOBJECT_INCLUDE_DIR
  NAMES glib-object.h
  HINTS ${PC_JsonGlib_INCLUDE_DIRS}
  PATHS /usr/include /usr/local/include
  PATH_SUFFIXES glib-2.0)

find_path(JSON_GLIB_GLIBCONFIG_INCLUDE_DIR
  NAMES glibconfig.h
  HINTS ${PC_JsonGlib_INCLUDE_DIRS}
  PATHS /usr/lib /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu /usr/local/lib
  PATH_SUFFIXES glib-2.0/include)

find_library(JSON_GLIB_LIBRARIES
  NAMES json-glib-1.0
  HINTS ${PC_JsonGlib_LIBDIR} ${PC_JsonGlib_LIBRARY_DIRS}
  PATHS /usr/lib /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu /usr/local/lib)

set(JSON_GLIB_INCLUDE_DIRS
  ${JSON_GLIB_INCLUDE_DIR}
  ${JSON_GLIB_GOBJECT_INCLUDE_DIR}
  ${JSON_GLIB_GLIBCONFIG_INCLUDE_DIR})

find_package_handle_standard_args(JsonGlib
  DEFAULT_MSG
  JSON_GLIB_LIBRARIES
  JSON_GLIB_INCLUDE_DIR
  JSON_GLIB_GOBJECT_INCLUDE_DIR
  JSON_GLIB_GLIBCONFIG_INCLUDE_DIR)

mark_as_advanced(JSON_GLIB_LIBRARIES
  JSON_GLIB_INCLUDE_DIR
  JSON_GLIB_GOBJECT_INCLUDE_DIR
  JSON_GLIB_GLIBCONFIG_INCLUDE_DIR
  JSON_GLIB_INCLUDE_DIRS)
