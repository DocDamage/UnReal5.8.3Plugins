# DocWorldTerminals Plugin

**Module 33** in the DocModular suite (Phase 11: Player Activities and Media).

## Overview
`DocWorldTerminals` provides in-world computers, kiosks, consoles, and embedded device interfaces with virtual filesystems, fine-grained tag permissions, typed command dispatch, and session lifecycle tracking.

## Core Capabilities
- **Virtual Filesystem**: Canonical paths with depth limits, `..` traversal escape protection, illegal character filtering, and permission-filtered directory views (`TRM-01`, `TRM-02`).
- **Session Lifecycle & Exclusivity**: `Opening`, `Authorized`, `Active`, `Closing`, `Closed`, and `Revoked` states with `SingleWriter` lease enforcement preventing concurrent write conflicts (`TRM-04`, `TRM-05`).
- **Power Loss & Unload Safety**: Terminal power down or level unregistration immediately revokes active sessions and cleans up write leases (`TRM-06`).
- **Typed Command Dispatch & Idempotency**: Commands require explicit registered verbs, session validation, and optional idempotency keys preventing duplicate side effects (`TRM-03`, `TRM-07`).
- **Host Sandbox Protection**: Virtual files and command payloads are strictly treated as passive data models without arbitrary host console, shell, or file access (`TRM-09`).

## Dependencies
- `DocModularCoreRuntime`
- Engine: `Core`, `CoreUObject`, `Engine`, `GameplayTags`, `DeveloperSettings`
- No sibling plugin dependencies.
