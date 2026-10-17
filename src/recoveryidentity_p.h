#ifndef RECOVERYIDENTITY_P_H
#define RECOVERYIDENTITY_P_H

#include <QByteArray>
#include <QString>

// Private ownership policy shared with the deterministic identity tests. PID
// liveness, public lock fields, and unchanged metadata are checked by the caller.
namespace RecoveryFiles::Internal {
struct Identity {
    QString hostname;
    QByteArray machine;
    QByteArray boot;
};

inline bool ownerIdentityIsLocal(const Identity &owner, const Identity &local) {
    // A known foreign machine always wins, including over a matching boot ID.
    if (!owner.machine.isEmpty() && !local.machine.isEmpty() && owner.machine != local.machine)
        return false;
    // Qt's lock-file and system hostname sources can differ (notably on
    // Windows). An exact machine identity does not depend on hostname spelling.
    if (!local.machine.isEmpty() && owner.machine == local.machine)
        return true;
    // If either machine identity is unavailable, use a deliberately stricter
    // boot-only fallback. A hostname by itself never establishes local identity.
    return !local.boot.isEmpty() && owner.boot == local.boot && !owner.hostname.isEmpty() &&
           owner.hostname == local.hostname;
}
} // namespace RecoveryFiles::Internal

#endif // RECOVERYIDENTITY_P_H
