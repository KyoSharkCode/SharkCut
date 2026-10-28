#include "logging.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QStandardPaths>
#include <QSysInfo>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef Q_OS_WIN
// clang-format off
#include <windows.h>
#include <dbghelp.h>
// clang-format on
#include <io.h>
#endif

namespace logging {

namespace {

constexpr int kKeptLogs = 8; // sesiones que se conservan

QMutex mutex;
bool toFile = false;
#ifdef Q_OS_WIN
std::wstring dumpPath; // preparada al empezar: al fallar no se reserva memoria
#endif

void handler(QtMsgType type, const QMessageLogContext&, const QString& message) {
    const char* level = "";
    switch (type) {
    case QtDebugMsg:
        level = "depuración";
        break;
    case QtInfoMsg:
        level = "info";
        break;
    case QtWarningMsg:
        level = "aviso";
        break;
    case QtCriticalMsg:
        level = "error";
        break;
    case QtFatalMsg:
        level = "fatal";
        break;
    }
    const QByteArray line = QStringLiteral("%1 [%2] %3\n")
                                .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")),
                                     QLatin1String(level), message)
                                .toUtf8();
    QMutexLocker lock(&mutex);
    std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stderr);
    std::fflush(stderr);
}

// abort() (p. ej. una comprobación interna de whisper/ggml que falla): se apunta antes
// de cerrar. Solo funciones seguras dentro de una señal.
extern "C" void onAbort(int) {
    static const char text[] = "\n*** Cierre inesperado: abort() (ver las líneas anteriores) ***\n";
    std::fwrite(text, 1, sizeof text - 1, stderr);
    std::fflush(stderr);
    std::signal(SIGABRT, SIG_DFL);
    std::abort();
}

#ifdef Q_OS_WIN
LONG WINAPI onCrash(EXCEPTION_POINTERS* info) {
    char text[160];
    const int n = std::snprintf(
        text, sizeof text, "\n*** Cierre inesperado: excepción 0x%08lX en la dirección %p ***\n",
        info->ExceptionRecord->ExceptionCode, info->ExceptionRecord->ExceptionAddress);
    if (n > 0) {
        std::fwrite(text, 1, static_cast<size_t>(n), stderr);
        std::fflush(stderr);
    }
    const HANDLE file = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), info, FALSE};
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                          static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory |
                                                     MiniDumpWithThreadInfo),
                          &exception, nullptr, nullptr);
        CloseHandle(file);
        static const char saved[] = "Volcado guardado en ultimo-cierre.dmp\n";
        std::fwrite(saved, 1, sizeof saved - 1, stderr);
        std::fflush(stderr);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

// Borra los registros más antiguos (se quedan los kKeptLogs más recientes).
void prune(const QDir& dir) {
    const QFileInfoList logs = dir.entryInfoList({QStringLiteral("sharkcut-*.log")}, QDir::Files,
                                                 QDir::Name | QDir::Reversed);
    for (int i = kKeptLogs; i < logs.size(); ++i) {
        QFile::remove(logs[i].absoluteFilePath());
    }
}

} // namespace

QString folder() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("registros"));
}

void install() {
    const bool console = qEnvironmentVariable("SHARKCUT_LOG") == QLatin1String("consola");
    const QDir dir(folder());
    if (!console && QDir().mkpath(dir.absolutePath())) {
        prune(dir);
        const QString path = dir.filePath(
            QStringLiteral("sharkcut-%1.log")
                .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"))));
#ifdef Q_OS_WIN
        FILE* reopened = nullptr;
        toFile = _wfreopen_s(&reopened, reinterpret_cast<const wchar_t*>(path.utf16()), L"a",
                             stderr) == 0;
        dumpPath = dir.filePath(QStringLiteral("ultimo-cierre.dmp")).toStdWString();
        SetUnhandledExceptionFilter(onCrash);
#else
        toFile = std::freopen(QFile::encodeName(path).constData(), "a", stderr) != nullptr;
#endif
        if (toFile) {
            std::setvbuf(stderr, nullptr, _IONBF, 0); // nada se queda en memoria al caer
        }
    }
    qInstallMessageHandler(handler);
    std::signal(SIGABRT, onAbort);
    qInfo("SharkCut %s · Qt %s · %s · %s", qUtf8Printable(QCoreApplication::applicationVersion()),
          qVersion(), qUtf8Printable(QSysInfo::prettyProductName()),
          qUtf8Printable(QSysInfo::currentCpuArchitecture()));
}

void closing() {
    qInfo("Cierre normal");
}

} // namespace logging
