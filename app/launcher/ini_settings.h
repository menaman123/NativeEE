// app/launcher/ini_settings.h -- the game's options file (ps2r.ini: one `key = value` per line, `#` comments) read
// and written one key at a time, exactly as the engine's SettingsFile does on the game's side, so a change in the
// launcher is a change in the game's Options menu and vice versa. Pure text; no engine dependency.
#pragma once
#include <QString>
#include <QStringList>
namespace ps2r::launcher {

class IniSettings {
public:
    explicit IniSettings(const QString& path);
    QString path() const { return path_; }
    bool exists() const;
    // The value of `key` or `fallback` when absent.
    QString get(const QString& key, const QString& fallback = QString()) const;
    bool has(const QString& key) const;
    // Replaces or appends `key = value`, preserving every other line; writes the file (creating its folder).
    bool set(const QString& key, const QString& value, QString* error = nullptr);
    QStringList keys() const;

    static QString setInText(const QString& text, const QString& key, const QString& value, bool* changed = nullptr);
    static bool getFromText(const QString& text, const QString& key, QString* value);

private:
    QString readAll() const;
    QString path_;
};

}  // namespace ps2r::launcher
