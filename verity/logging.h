// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by the GPL v2 license that can
// be found in the LICENSE file.
//
// Includes the correct logging library, either google-log or chrome logging.
#ifndef VERITY_LOGGING_H_
#define VERITY_LOGGING_H_

#if defined(WITH_CHROME)
#  include <base/logging.h>
#  define INIT_LOGGING(name, flags...) { \
   logging::InitLogging(NULL, \
                       logging::LOG_ONLY_TO_SYSTEM_DEBUG_LOG, \
                       logging::DONT_LOCK_LOG_FILE, \
                       logging::APPEND_TO_OLD_LOG_FILE); \
   }
#else
#  include "verity/logging/logging.h"
#  define INIT_LOGGING(name, flags...) { }
#endif

#endif   // VERITY_LOGGING_H_
