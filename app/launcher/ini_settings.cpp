// app/launcher/ini_settings.cpp -- the options file, the engine's grammar (sections optional, `#` and `;` comments).
#include "ini_settings.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
namespace ps2r::launcher {

namespace {
QString stripComment(const QString& s) {
    int p = -1;
    for (int i = 0; i < s.size(); ++i) if (s[i] == '#' || s[i] == ';') { p = i; break; }
    return p < 0 ? s : s.left(p);
}
// The key a line sets, in section.key form; empty for comments, blank lines and headers (which update `section`).
QString lineKey(const QString& line, QString& section) {
    const QString l = stripComment(line).trimmed();
    if (l.isEmpty()) return QString();
    if (l.startsWith('[') && l.endsWith(']')) { section = l.mid(1, l.size() - 2).trimmed(); return QString(); }
    const int eq = l.indexOf('=');
    if (eq < 0) return QString();
    QString key = l.left(eq).trimmed();
    if (!section.isEmpty() && !key.contains('.')) key = section + "." + key;
    return key;
}
}  // namespace

IniSettings::IniSettings(const QString& path) : path_(path) {}

bool IniSettings::exists() const { return QFileInfo::exists(path_); }

QString IniSettings::readAll() const {
    QFile f(path_);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    return QString::fromUtf8(f.readAll());
}

bool IniSettings::getFromText(const QString& text, const QString& key, QString* value) {
    QString section;
    for (const QString& line : text.split('\n')) {
        if (lineKey(line, section) != key) continue;
        const QString l = stripComment(line);
        *value = l.mid(l.indexOf('=') + 1).trimmed();
        return true;
    }
    return false;
}

QString IniSettings::setInText(const QString& text, const QString& key, const QString& value, bool* changed) {
    QStringList lines = text.split('\n');
    if (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
    QString section;
    bool replaced = false;
    for (QString& line : lines) {
        if (lineKey(line, section) != key) continue;
        const QString head = line.left(line.indexOf('=')).trimmed();
        const QString next = head + " = " + value;
        if (next != line) { line = next; if (changed) *changed = true; }
        replaced = true;
        break;
    }
    if (!replaced) {
        if (!lines.isEmpty() && !section.isEmpty()) lines << "[]";
        lines << key + " = " + value;
        if (changed) *changed = true;
    }
    QString out;
    for (const QString& l : lines) out += l + "\n";
    return out;
}

QString IniSettings::get(const QString& key, const QString& fallback) const {
    QString v;
    return getFromText(readAll(), key, &v) ? v : fallback;
}

bool IniSettings::has(const QString& key) const {
    QString v;
    return getFromText(readAll(), key, &v);
}

bool IniSettings::set(const QString& key, const QString& value, QString* error) {
    const QString text = setInText(readAll(), key, value);
    if (!QDir().mkpath(QFileInfo(path_).absolutePath())) {
        if (error) *error = QStringLiteral("cannot create the folder of %1").arg(path_);
        return false;
    }
    QSaveFile f(path_);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("cannot write %1").arg(path_);
        return false;
    }
    f.write(text.toUtf8());
    if (!f.commit()) {
        if (error) *error = QStringLiteral("cannot write %1").arg(path_);
        return false;
    }
    return true;
}

QStringList IniSettings::keys() const {
    QStringList out;
    QString section;
    for (const QString& line : readAll().split('\n')) {
        const QString k = lineKey(line, section);
        if (!k.isEmpty()) out << k;
    }
    return out;
}

}  // namespace ps2r::launcher
