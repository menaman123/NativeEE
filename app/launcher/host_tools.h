// app/launcher/host_tools.h -- what this machine already has and where the launcher's own copies go: the host
// compiler (macOS: the Xcode Command Line Tools), git, python3, and the portable CMake / Ninja under
// Toolchain/. Answers paths; never installs system-wide.
#pragma once
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
namespace ps2r::launcher {
class AppData;

class HostTools {
public:
    explicit HostTools(const AppData* appData);

    // macOS: the Command Line Tools (or Xcode) are present -- clang and git exist. Windows / Linux: a C++ compiler.
    bool hasCompiler(QString* description = nullptr) const;
    // The system installer that provides a step's tool: "compiler" (macOS: xcode-select --install; Windows: winget,
    // the Visual Studio 2022 Build Tools with the C++ Clang tools), "python" (Windows: winget, Python 3.12) and
    // "source" (macOS: git comes with the Command Line Tools). An attached command is waited for and its output
    // shown; a detached one (a system dialog, a download page) is watched for by re-checking. Empty program: none.
    struct InstallCommand {
        QString program;
        QStringList args;
        bool attached = true;
        QString what;   // "the Visual Studio Build Tools"
    };
    InstallCommand installCommand(const QString& stepId) const;
    // Forgets what was found so far (Visual Studio, CMake, Ninja) and, on Windows, takes the PATH the system has now
    // (an installer that just finished added to it): every check after this looks at the machine afresh.
    void refresh() const;
    // Windows: the Visual Studio 2022 (or Build Tools) installation that carries the C++ Clang tools, else empty.
    QString visualStudio() const;
    // The environment the game's build runs in. Windows: that installation's x64 developer environment (clang-cl,
    // the SDK, Ninja on PATH) without its own VCPKG_ROOT; elsewhere the system's.
    QProcessEnvironment buildEnvironment() const;
    QString git() const;       // the git executable (the machine's, else the launcher's portable copy) or empty
    QString python3() const;   // a python3 of at least 3.8, or empty
    // CMake / Ninja: the launcher's own copy under Toolchain/ when present, else one already on this machine that is new
    // enough (CMake 3.24+, Ninja 1.10+: PATH, Visual Studio's bundled copies, Homebrew), else empty
    QString cmake() const;
    QString ninja() const;
    static QString toolVersion(const QString& exe);   // "3.30.0" from `<exe> --version`, or empty
    QString portableCmakeDir() const;
    QString portableNinjaDir() const;
    QString portableGitDir() const;   // Windows: Toolchain/git (MinGit, the prebuilt Git for Windows, when there is no git)
    static QString which(const QString& name);
    static QString runForOutput(const QString& program, const QStringList& args, int timeoutMs = 10000, int* exitCode = nullptr);

private:
    QString systemCmake() const;
    QString systemNinja() const;
    const AppData* appData_;
    [[maybe_unused]] mutable QString vsCache_;      // Windows only (visualStudio())
    [[maybe_unused]] mutable bool vsChecked_ = false;
    mutable QString cmakeCache_, ninjaCache_;
    mutable bool cmakeChecked_ = false, ninjaChecked_ = false;
};

}  // namespace ps2r::launcher
