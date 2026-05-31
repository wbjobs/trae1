-- PostgreSQL schema for the eBPF syscall monitor.
-- Run this once as a privileged user before starting the API.

CREATE ROLE ebpf WITH LOGIN PASSWORD 'ebpf';
CREATE DATABASE ebpf_monitor OWNER ebpf;

\c ebpf_monitor

CREATE TABLE IF NOT EXISTS syscall_events (
    id BIGSERIAL PRIMARY KEY,
    pid INTEGER NOT NULL,
    comm VARCHAR(64) NOT NULL,
    syscall_name VARCHAR(32) NOT NULL,
    timestamp TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    duration_ns BIGINT,
    return_value BIGINT,
    file_path TEXT,
    extra_args TEXT
);

CREATE INDEX IF NOT EXISTS ix_events_pid ON syscall_events (pid);
CREATE INDEX IF NOT EXISTS ix_events_pid_syscall ON syscall_events (pid, syscall_name);
CREATE INDEX IF NOT EXISTS ix_events_pid_time ON syscall_events (pid, timestamp);
CREATE INDEX IF NOT EXISTS ix_events_time ON syscall_events (timestamp);

CREATE TABLE IF NOT EXISTS alerts (
    id BIGSERIAL PRIMARY KEY,
    pid INTEGER NOT NULL,
    comm VARCHAR(64) NOT NULL,
    syscall_name VARCHAR(32) NOT NULL,
    timestamp TIMESTAMPTZ NOT NULL,
    duration_ns BIGINT NOT NULL,
    threshold_ns BIGINT NOT NULL,
    file_path TEXT,
    message TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS ix_alerts_pid ON alerts (pid);
CREATE INDEX IF NOT EXISTS ix_alerts_pid_time ON alerts (pid, timestamp);
CREATE INDEX IF NOT EXISTS ix_alerts_time ON alerts (timestamp);
