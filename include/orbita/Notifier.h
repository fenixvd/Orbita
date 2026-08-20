#pragma once

#include <QString>

namespace orbita {

/// Уведомления рабочего стола.
///
/// Через штатную службу (org.freedesktop.Notifications), а не через kdialog:
/// такие уведомления попадают в историю, подчиняются настройкам KDE
/// («не беспокоить» и прочее) и показываются со значком приложения.
/// Заодно это работает и из консольной части, где нет графического цикла.
class Notifier {
public:
    enum class Kind {
        Info,     ///< обычное сообщение
        Warning,  ///< что-то не получилось
    };

    /// Показывает уведомление. Пустой `channel` — обычное сообщение;
    /// одинаковый `channel` заменяет предыдущее вместо новой строки в истории,
    /// чтобы поток однотипных сообщений не заваливал экран.
    static void show(const QString& title,
                     const QString& body,
                     Kind kind = Kind::Info,
                     const QString& channel = {});
};

} // namespace orbita
