#include "common/debug.h"

#include <stdarg.h>
#include <cstdio>
#include "common/SimpleMutex.h"
#include "common/system_utils.h"

#define LOG_NUM_SEVERITIES 5

constexpr std::array<const char *, LOG_NUM_SEVERITIES> g_logSeverityNames = {
    {"EVENT", "INFO", "WARN", "ERR", "FATAL"}};

constexpr const char *LogSeverityName(int severity)
{
    return (severity >= 0 && severity < LOG_NUM_SEVERITIES) ? g_logSeverityNames[severity]
                                                            : "UNKNOWN";
}

namespace gl
{

// This now perfectly mimics the original debug.cpp's pointer-based approach
// angle::SimpleMutex *g_debugMutex = nullptr;
DebugAnnotator *g_debugAnnotator = nullptr;

namespace priv
{
std::ostream *gSwallowStream;

bool ShouldCreatePlatformLogMessage(LogSeverity severity)
{
#if defined(ANGLE_TRACE_ENABLED)
    return true;
#else
    return severity != LOG_EVENT;
#endif
}

}

// --- Mutex and Initialization ---
void InitializeDebugMutexIfNeeded()
{

}

// We add a finalize function to be good citizens and clean up the mutex.
void FinalizeDebug()
{

}

void InitializeDebugAnnotations(DebugAnnotator *debugAnnotator) {}
void UninitializeDebugAnnotations() {}
bool DebugAnnotationsActive(const gl::Context *context) { return false; }
bool DebugAnnotationsInitialized() { return false; }

ScopedPerfEventHelper::ScopedPerfEventHelper(Context *context, angle::EntryPoint entryPoint)
    : mContext(context), mEntryPoint(entryPoint), mFunctionName(nullptr), mCalledBeginEvent(false)
{}
ScopedPerfEventHelper::~ScopedPerfEventHelper() {}
void ScopedPerfEventHelper::begin(const char *format, ...) {}

LogMessage::LogMessage(const char *file, const char *function, int line, LogSeverity severity)
    : mFile(file), mFunction(function), mLine(line), mSeverity(severity)
{
    InitializeDebugMutexIfNeeded();
    if (mSeverity >= LOG_WARN)
    {
        mStream << "ANGLE:" << LogSeverityName(mSeverity) << ":" << file << ":" << line << ": ";
    }
}

LogMessage::~LogMessage()
{
    std::string msg = mStream.str();
    if (!msg.empty())
    {
        // std::unique_lock<angle::SimpleMutex> lock(*g_debugMutex);
        fprintf((mSeverity >= LOG_WARN) ? stderr : stdout, "%s\n", msg.c_str());
    }
    if (mSeverity == LOG_FATAL)
    {
        fprintf(stderr, "ANGLE: A fatal error occurred.\n");
    }
}

LogSeverity LogMessage::getSeverity() const { return mSeverity; }
std::string LogMessage::getMessage() const { return mStream.str(); }

void Trace(LogSeverity severity, const char *message)
{
    InitializeDebugMutexIfNeeded();
    fprintf((severity >= LOG_WARN) ? stderr : stdout, "ANGLE:%s: %s\n", LogSeverityName(severity),
            message);
}

}  // namespace gl