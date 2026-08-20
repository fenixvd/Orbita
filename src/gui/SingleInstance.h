#pragma once

#include <QObject>

class QLocalServer;
class QWindow;

namespace orbita {

/// Сторож единственного экземпляра.
///
/// Второй запуск не просто бесполезен, а вреден: обе копии полезут
/// монтировать одну и ту же папку. Поэтому новый процесс лишь просит
/// уже работающий показать окно и завершается.
class SingleInstance : public QObject {
    Q_OBJECT
public:
    explicit SingleInstance(QObject* parent = nullptr);

    /// true — мы первые и стали хозяином. false — копия уже работает,
    /// ей отправлена просьба показать окно, и нам следует выйти.
    bool takeOwnership();

    /// Окно, которое показываем по просьбе второго запуска.
    void setWindow(QWindow* window) { m_window = window; }

private:
    void showWindow();

    QLocalServer* m_server = nullptr;
    QWindow* m_window = nullptr;
};

} // namespace orbita
