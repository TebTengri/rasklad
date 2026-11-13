// Developer-only native field assertion client. Real Qt Quick window with two real
// fields, connected only to the private compositor socket given by the parent.
// Types invented words with synthetic key events delivered to its OWN window
// (no real input devices), then asserts on the actual field contents.
// Expected strings are independent hardcoded literals (not converter output).
// stdout: bounded metadata-only phase ACKs and one final JSON report; never field text.
#include <QGuiApplication>
#include <QInputMethod>
#include <QObject>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSocketNotifier>
#include <QTimer>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *const kScenarios[] = {"baseline", "prefix", "reverse",
                                         "typing-race", "focus-race", "selection-race",
                                         "post-send-race", "keyboard-before-edit",
                                         "keyboard-after-edit", "keyboard-acked-edit",
                                         "keyboard-applied-before-edit", "keyboard-guarded-after-key", "compose-baseline", "compose-fast-tail", "compose-known", "compose-backspace", "compose-overflow", "compose-focus", "fcitx-baseline", "fcitx-fast-tail", "fcitx-known", "fcitx-backspace", "fcitx-overflow", "fcitx-focus", "fcitx-cursor", "fcitx-selection", "fcitx-fields"};
static QString g_scenario;
static bool frameworkScenario() { return g_scenario.startsWith(QLatin1String("fcitx-")); }
static QString compositionKind() { return frameworkScenario() ? QStringLiteral("compose-") + g_scenario.mid(6) : g_scenario; }
static bool compositionScenario() { return g_scenario.startsWith(QLatin1String("compose-")) || frameworkScenario(); }
static QSocketNotifier *g_stdinNotifier = nullptr;
static bool g_handshakeDone = false;

static const QString kSecretTyped = QStringLiteral("ghbdtn ");      // same ASCII word, all scenarios
static const QString kMarker = QStringLiteral("zqxmarker");          // invented focus-race preset

static QString g_typed;     // what stage 2 types into the normal field
static QString g_expected;  // hardcoded expected final normal-field text

static QQuickWindow *g_win = nullptr;
static QJsonObject g_out;

static QQuickItem *field(const char *name)
{
    return g_win ? g_win->findChild<QQuickItem *>(QLatin1String(name)) : nullptr;
}

static void focusField(QQuickItem *item)
{
    if (!item)
        return;
    g_win->requestActivate();
    QMetaObject::invokeMethod(item, "forceActiveFocus");
    QGuiApplication::inputMethod()->update(Qt::ImQueryAll);
}

// Synthetic key events sent only to our own QQuickWindow (no devices, no grabs).
static void typeText(const QString &s)
{
    for (QChar c : s) {
        int key = int(Qt::Key_unknown);
        if (c == u' ')
            key = int(Qt::Key_Space);
        else if (c >= u'a' && c <= u'z')
            key = int(Qt::Key_A) + (c.unicode() - 'a');
        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, QString(c));
        QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, QString(c));
        QCoreApplication::sendEvent(g_win, &press);
        QCoreApplication::sendEvent(g_win, &release);
    }
    // Make sure the platform input context pushes fresh surrounding text.
    QGuiApplication::inputMethod()->update(Qt::ImQueryAll);
}

// Reads one '\n'-terminated whitelisted command from stdin, at most 16 bytes,
// with a steady_clock deadline (EINTR-safe). Returns true only on exact match.
static bool readExactCommand(const char *expected, int budgetMs)
{
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::milliseconds(budgetMs);
    char buf[16];
    size_t len = 0;
    while (len < sizeof(buf)) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - clock::now()).count();
        if (left <= 0)
            return false;
        pollfd pfd{STDIN_FILENO, POLLIN, 0};
        const int r = ::poll(&pfd, 1, int(left));
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0 || !(pfd.revents & POLLIN))
            return false; // timeout, error or hangup
        const ssize_t n = ::read(STDIN_FILENO, buf + len, 1);
        if (n < 0 && errno == EINTR)
            continue;
        if (n != 1)
            return false;
        if (buf[len++] == '\n')
            return len == std::strlen(expected) && std::memcmp(buf, expected, len) == 0;
    }
    return false; // oversized
}

static bool writeAck(const char *ack)
{
    const size_t n = std::strlen(ack); // < PIPE_BUF, atomic
    std::fflush(stdout);
    ssize_t w;
    do { w = ::write(STDOUT_FILENO, ack, n); } while (w < 0 && errno == EINTR);
    return w == ssize_t(n);
}

static bool keyboardScenario()
{
    return g_scenario.startsWith(QLatin1String("keyboard-"));
}

// Observes only synthetic events delivered to our own fixture client. Stores
// counts/order, never event text. Installed before opening the fixture window.
class DeliveryObserver final : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject *object, QEvent *event) override {
        if (!keyboardScenario() || !g_out.value("keyboard_resumed").toBool())
            return false;
        if (event->type() == QEvent::InputMethod && !m_editSeen) {
            m_editSeen = true;
            g_out.insert("edit_observed_before_keyboard", !m_keySeen);
        }
        if (object == g_win && event->type() == QEvent::KeyPress) {
            const auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_X && key->nativeScanCode() != 0 && key->spontaneous()) {
                m_keySeen = true;
                g_out.insert("native_keyboard_x_seen", true);
            }
        }
        return false;
    }
private:
    bool m_editSeen = false;
    bool m_keySeen = false;
};


// post-send-race: parent says "prepare" right before sending its edit. We ACK and
// then block here (no Qt event loop, so the queued Wayland edit is NOT applied)
// until "mutate". We then edit our own field synthetically, which advances the
// text-input-v3 commit serial, ACK, and return to the loop. The safety test
// deliberately requires preservation; Qt applies the stale edit in LAB-003.
static void onStdinCommand()
{
    g_stdinNotifier->setEnabled(false); // one-shot; never re-enabled
    if (g_handshakeDone)
        return;
    g_handshakeDone = true;
    if (!readExactCommand("prepare\n", 200) || !writeAck("{\"phase\":\"ready\"}\n"))
        return;
    if (keyboardScenario()) {
        if (!readExactCommand("resume\n", 700))
            return;
        g_out.insert("keyboard_resumed", true);
        if (!writeAck("{\"phase\":\"resumed\"}\n"))
            return;
        if (g_scenario == "keyboard-acked-edit" || g_scenario == "keyboard-applied-before-edit" || g_scenario == "keyboard-guarded-after-key") {
            QTimer::singleShot(50, Qt::PreciseTimer, [] {
                QQuickItem *normal = field("normalField");
                const bool keyFirst = g_scenario != "keyboard-acked-edit";
                const QString expected = keyFirst ? QStringLiteral("ghbdtn x") : QString::fromUtf8("привет ");
                const bool applied = normal && normal->hasActiveFocus()
                    && normal->property("text").toString() == expected
                    && normal->property("selectedText").toString().isEmpty();
                g_out.insert(keyFirst ? "key_ack_verified" : "edit_ack_verified", applied);
                writeAck(applied ? (keyFirst ? "{\"phase\":\"key_applied\"}\n" : "{\"phase\":\"applied\"}\n") : "{\"phase\":\"not_applied\"}\n");
            });
        }
        return;
    }
    g_out.insert(QStringLiteral("post_send_prepared"), true);
    if (!readExactCommand("mutate\n", 700))
        return;
    typeText(QStringLiteral("x"));
    QGuiApplication::inputMethod()->update(Qt::ImQueryAll);
    QQuickItem *normal = field("normalField");
    const bool localMatches = normal && normal->property("text").toString() == g_typed + QLatin1Char('x');
    g_out.insert(QStringLiteral("post_send_local_before_dispatch_matches"), localMatches);
    const bool ackSent = writeAck(localMatches ? "{\"phase\":\"mutated\"}\n" : "{\"phase\":\"mutation_failed\"}\n");
    if (localMatches && ackSent)
        g_out.insert(QStringLiteral("post_send_mutated"), true);
}

static void finish(int code = 0)
{
    g_out.insert(QStringLiteral("completed"), code == 0);
    auto yes = [](const char *key) { return g_out.value(QLatin1String(key)).toBool(false); };
    bool passed = yes("window_ready") && yes("normal_had_active_focus")
        && yes("normal_typed_as_expected_before_fix") && yes("normal_matches_expected")
        && yes("secret_had_active_focus") && yes("secret_hints_sensitive")
        && yes("secret_field_unchanged");
    if (g_scenario == "selection-race") passed = passed && yes("selection_preserved");
    else passed = passed && yes("normal_no_selection");
    if (g_scenario == "focus-race" || compositionKind() == "compose-focus") passed = passed && yes("marker_unchanged");
    if (compositionScenario()) passed = passed && yes("composition_intermediate_verified") && yes("composition_final_preedit_empty");
    if (g_scenario == "fcitx-fields") passed = passed && yes("second_field_verified");
    if (g_scenario == "fcitx-selection") passed = passed && yes("navigation_selection_verified");
    if (compositionKind() == "compose-fast-tail") passed = passed && yes("composition_tail_verified");
    if (g_scenario == "post-send-race")
        passed = passed && yes("post_send_prepared") && yes("post_send_mutated");
    if (keyboardScenario())
        passed = passed && yes("keyboard_resumed") && yes("native_keyboard_x_seen");
    if (g_scenario == "keyboard-acked-edit") passed = passed && yes("edit_ack_verified");
    if (g_scenario == "keyboard-applied-before-edit" || g_scenario == "keyboard-guarded-after-key") passed = passed && yes("key_ack_verified");
    g_out.insert(QStringLiteral("field_assertions_passed"), passed);
    const QByteArray line = QJsonDocument(g_out).toJson(QJsonDocument::Compact);
    std::fwrite(line.constData(), 1, size_t(line.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
    if (code == 0)
        QCoreApplication::exit(passed ? 0 : 1);
}

static bool parseScenario(int argc, char **argv)
{
    const char *label = "baseline";
    if (argc == 3 && std::strcmp(argv[1], "--scenario") == 0)
        label = argv[2];
    else if (argc != 1)
        return false;
    for (const char *k : kScenarios) {
        if (std::strcmp(label, k) == 0) {
            g_scenario = QString::fromLatin1(k);
            return true;
        }
    }
    return false;
}

int main(int argc, char **argv)
{
    // Validate before any QGuiApplication / window / socket connection.
    if (!parseScenario(argc, argv)) {
        std::fputs("{\"completed\":false,\"scenario_valid\":false}\n", stdout);
        std::fflush(stdout);
        return 2;
    }
    if (compositionScenario()) {
        g_typed = compositionKind() == "compose-known" ? QStringLiteral("hello")
            : compositionKind() == "compose-overflow" ? QString(33, QLatin1Char('z')) : QStringLiteral("ghbdtn");
        g_expected = compositionKind() == "compose-known" ? QStringLiteral("hello ")
            : compositionKind() == "compose-overflow" ? g_typed + QLatin1Char(' ')
            : compositionKind() == "compose-cursor" ? QStringLiteral("ghbdt n")
            : compositionKind() == "compose-selection" ? QStringLiteral("ghbdtx ")
            : (compositionKind() == "compose-focus" || compositionKind() == "compose-fields") ? g_typed
            : compositionKind() == "compose-fast-tail" ? QString::fromUtf8("привет x ")
            : QString::fromUtf8("привет ");
    } else if (g_scenario == "prefix") {
        g_typed = QStringLiteral("hello ghbdtn ");
        g_expected = QString::fromUtf8("hello привет ");
    } else if (g_scenario == "reverse") {
        g_typed = QString::fromUtf8("привет ");
        g_expected = QStringLiteral("ghbdtn ");
    } else if (g_scenario == "typing-race" || g_scenario == "post-send-race") {
        g_typed = QStringLiteral("ghbdtn ");
        g_expected = QStringLiteral("ghbdtn x");
    } else if (g_scenario == "keyboard-applied-before-edit" || g_scenario == "keyboard-guarded-after-key") {
        g_typed = QStringLiteral("ghbdtn ");
        g_expected = QStringLiteral("ghbdtn x"); // preservation criterion, not expected corruption
    } else if (keyboardScenario()) {
        g_typed = QStringLiteral("ghbdtn ");
        g_expected = QString::fromUtf8("привет x");
    } else if (g_scenario == "baseline") {
        g_typed = QStringLiteral("ghbdtn ");
        g_expected = QString::fromUtf8("привет ");
    } else { // focus-race, selection-race: original must stay unchanged
        g_typed = QStringLiteral("ghbdtn ");
        g_expected = QStringLiteral("ghbdtn ");
    }

    if (qgetenv("RASKLAD_NATIVE_FIELD_FIXTURE") != "1"
        || qgetenv("XDG_RUNTIME_DIR") != "/run/lab"
        || qgetenv("WAYLAND_DISPLAY") != "rasklad-native-test"
        || qgetenv("QT_QPA_PLATFORM") != "wayland"
        || !qgetenv("WAYLAND_SOCKET").isEmpty()
        || !qgetenv("WAYLAND_DEBUG").isEmpty()
        || !qgetenv("DISPLAY").isEmpty()) {
        std::fputs("{\"fixture_environment_valid\":false,\"completed\":false}\n", stdout);
        return 2;
    }
    if (g_scenario == "post-send-race" || keyboardScenario()) {
        struct stat input{};
        if (::fstat(STDIN_FILENO, &input) != 0 || !S_ISFIFO(input.st_mode)) {
            std::fputs("{\"fixture_pipe_valid\":false,\"completed\":false}\n", stdout);
            return 2;
        }
    }
    QGuiApplication app(argc, argv);
    DeliveryObserver observer;
    app.installEventFilter(&observer);
    g_out = {{"completed", false},
             {"scenario", g_scenario},
             {"scenario_valid", true},
             {"window_ready", false},
             {"normal_had_active_focus", false},
             {"normal_typed_as_expected_before_fix", false},
             {"normal_matches_expected", false},
             {"normal_field_is_cyrillic", false},
             {"normal_field_changed", false},
             {"normal_no_selection", false},
             {"selection_preserved", false},
             {"marker_unchanged", false},
             {"secret_had_active_focus", false},
             {"secret_hints_sensitive", false},
             {"secret_field_unchanged", false},
             {"post_send_prepared", false},
             {"post_send_mutated", false}};
    if (g_scenario == "post-send-race" || keyboardScenario()) {
        g_stdinNotifier = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, &app);
        QObject::connect(g_stdinNotifier, &QSocketNotifier::activated, [] { onStdinCommand(); });
    }

    QQmlApplicationEngine engine;
    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));
    if (engine.rootObjects().isEmpty()) {
        finish(1);
        return 1;
    }
    g_win = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!g_win) {
        finish(1);
        return 1;
    }
    if (g_scenario == "focus-race")
        if (QQuickItem *s = field("secretField"))
            s->setProperty("text", kMarker);

    // Stage 1 (1.2 s): wait for configure/keyboard enter, focus normal field.
    QTimer::singleShot(1200, Qt::PreciseTimer, [] {
        QQuickItem *n = field("normalField");
        g_out.insert(QStringLiteral("window_ready"), n != nullptr && g_win->isExposed());
        focusField(n);
    });
    // Stage 2 (1.7 s): type into the focused normal field.
    QTimer::singleShot(1700, Qt::PreciseTimer, [] {
        QQuickItem *n = field("normalField");
        g_out.insert(QStringLiteral("normal_had_active_focus"), n && n->hasActiveFocus());
        g_out.insert(QStringLiteral("window_active"), g_win->isActive());
        g_out.insert(QStringLiteral("focus_is_normal"), QGuiApplication::focusObject() == n);
        g_out.insert(QStringLiteral("input_context_visible"), QGuiApplication::inputMethod()->isVisible());
        if (frameworkScenario()) {
            // The ordinary Fcitx Qt plugin handles source keys, not this client.
        } else if (compositionScenario()) {
            const QString preedit = n ? n->property("preeditText").toString() : QString();
            const QString committed = n ? n->property("text").toString() : QString();
            const bool matches = compositionKind() == "compose-overflow"
                ? committed == g_typed && preedit.isEmpty()
                : committed.isEmpty() && preedit == g_typed;
            g_out.insert("normal_typed_as_expected_before_fix", matches);
            g_out.insert("composition_intermediate_verified", matches);
        } else if (keyboardScenario()) {
            // Ask the private server for a fixed synthetic word through wl_keyboard.
            writeAck("{\"phase\":\"word_ready\"}\n");
            QTimer::singleShot(150, Qt::PreciseTimer, [] {
                QQuickItem *normal = field("normalField");
                g_out.insert("normal_typed_as_expected_before_fix",
                    normal && normal->property("text").toString() == g_typed);
            });
        } else {
            typeText(g_typed);
            // Synchronous local-event control from LAB-003.
            g_out.insert(QStringLiteral("normal_typed_as_expected_before_fix"),
                         n && n->property("text").toString() == g_typed);
        }
    });
    QTimer::singleShot(1750, Qt::PreciseTimer, [] {
        if (compositionKind() == "compose-focus" && !frameworkScenario()) {
            auto *secret = field("secretField");
            if (secret) secret->setProperty("text", kMarker);
            focusField(secret);
        }
    });
    QTimer::singleShot(2300, Qt::PreciseTimer, [] {
        if (compositionKind() == "compose-fast-tail" && !frameworkScenario()) {
            auto *normal = field("normalField");
            g_out.insert("composition_tail_verified", normal
                && normal->property("text").toString() == QString::fromUtf8("привет ")
                && normal->property("preeditText").toString() == QStringLiteral("x"));
        }
    });
    QTimer::singleShot(2100, Qt::PreciseTimer, [] {
        if (!frameworkScenario()) return;
        auto *normal = field("normalField");
        const QString preedit = normal ? normal->property("preeditText").toString() : QString();
        const QString committed = normal ? normal->property("text").toString() : QString();
        const bool matches = compositionKind() == "compose-overflow"
            ? committed == g_typed && preedit.isEmpty() : committed.isEmpty() && preedit == g_typed;
        g_out.insert("normal_typed_as_expected_before_fix", matches);
        g_out.insert("composition_intermediate_verified", matches);
    });
    QTimer::singleShot(2200, Qt::PreciseTimer, [] {
        if (frameworkScenario() && compositionKind() == "compose-focus") {
            auto *secret = field("secretField");
            if (secret) secret->setProperty("text", kMarker);
            focusField(secret);
        }
    });
    QTimer::singleShot(2200, Qt::PreciseTimer, [] {
        if (g_scenario == "fcitx-fields") focusField(field("secondField"));
    });
    QTimer::singleShot(2700, Qt::PreciseTimer, [] {
        if (g_scenario == "fcitx-selection") {
            auto *n = field("normalField");
            g_out.insert("navigation_selection_verified", n && n->property("selectedText").toString() == QStringLiteral("n"));
        }
    });
    QTimer::singleShot(2900, Qt::PreciseTimer, [] {
        if (frameworkScenario() && compositionKind() == "compose-fast-tail") {
            auto *normal = field("normalField");
            g_out.insert("composition_tail_verified", normal
                && normal->property("text").toString() == QString::fromUtf8("привет ")
                && normal->property("preeditText").toString() == QStringLiteral("x"));
        }
    });
    // Stage 2b (1.8 s): race actions inside the parent's 300 ms plan window.
    QTimer::singleShot(1800, Qt::PreciseTimer, [] {
        QQuickItem *n = field("normalField");
        if (g_scenario == "typing-race") {
            typeText(QStringLiteral("x"));
        } else if (g_scenario == "focus-race") {
            focusField(field("secretField"));
        } else if (g_scenario == "selection-race" && n) {
            QMetaObject::invokeMethod(n, "select", Q_ARG(int, 0), Q_ARG(int, 2));
            QGuiApplication::inputMethod()->update(Qt::ImQueryAll);
        }
    });
    // Stage 3 (3.4 s): verify actual field content, then switch to password field.
    QTimer::singleShot(3400, Qt::PreciseTimer, [] {
        QQuickItem *n = field("normalField");
        QQuickItem *s = field("secretField");
        const QString t = n ? n->property("text").toString() : QString();
        const QString sel = n ? n->property("selectedText").toString() : QString();
        g_out.insert(QStringLiteral("normal_matches_expected"), n && t == g_expected);
        if (g_scenario == "fcitx-fields") {
            auto *second = field("secondField");
            g_out.insert("second_field_verified", second && second->property("text").toString() == QStringLiteral("hello ")
                && second->property("preeditText").toString().isEmpty());
        }
        g_out.insert("composition_final_preedit_empty", n && n->property("preeditText").toString().isEmpty());
        g_out.insert("composition_original_pending_only", n && t.isEmpty() && n->property("preeditText").toString() == g_typed);
        g_out.insert(QStringLiteral("stale_edit_corrupted_new_text"), t == QString::fromUtf8("gпривет "));
        g_out.insert(QStringLiteral("correction_arrived_before_local_mutation"), t == QString::fromUtf8("привет x"));
        g_out.insert(QStringLiteral("normal_field_is_cyrillic"), t == QString::fromUtf8("привет "));
        g_out.insert(QStringLiteral("normal_field_changed"), !t.isEmpty() && t != g_typed);
        g_out.insert(QStringLiteral("normal_no_selection"), n && sel.isEmpty());
        g_out.insert(QStringLiteral("selection_preserved"),
                     n && sel == QStringLiteral("gh"));
        if (g_scenario == "focus-race" || compositionKind() == "compose-focus") {
            g_out.insert(QStringLiteral("marker_unchanged"),
                         s && s->property("text").toString() == kMarker);
            if (s)
                s->setProperty("text", QString()); // reset for the standard password stage
        }
        focusField(s);
    });
    // Stage 4 (3.9 s): type same fictional word into the protected field.
    QTimer::singleShot(3900, Qt::PreciseTimer, [] {
        QQuickItem *s = field("secretField");
        g_out.insert(QStringLiteral("secret_had_active_focus"), s && s->hasActiveFocus());
        const int hints = s ? s->property("inputMethodHints").toInt() : 0;
        g_out.insert(QStringLiteral("secret_hints_sensitive"),
                     (hints & Qt::ImhSensitiveData) && (hints & Qt::ImhNoPredictiveText));
        if (!frameworkScenario()) typeText(kSecretTyped); // framework cases receive real wl_keyboard keys
    });
    // Stage 5 (5.6 s): verify protected field untouched, report, quit.
    QTimer::singleShot(5600, Qt::PreciseTimer, [] {
        QQuickItem *s = field("secretField");
        g_out.insert(QStringLiteral("secret_field_unchanged"),
                     s && s->property("text").toString() == kSecretTyped);
        finish();
    });

    return app.exec();
}
