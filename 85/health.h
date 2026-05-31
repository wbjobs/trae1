#ifndef HEALTH_H
#define HEALTH_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

#include "config.h"
#include "spi.h"

#define HEALTH_MAX_HISTORY     4096
#define HEALTH_TEMP_MAX        85.0
#define HEALTH_TEMP_WARN       75.0
#define HEALTH_VOLTAGE_NOM     3.3
#define HEALTH_VOLTAGE_TOL     0.05
#define HEALTH_FREQ_TOL        0.10
#define HEALTH_RATE_JUMP       0.50

#define HEALTH_SCORE_WARN      60.0
#define HEALTH_SCORE_OK        80.0

#define HEALTH_CHIP_ID_UNKNOWN 0xFFFFFFFF

#define HEALTH_REG_CHIP_ID     0x00
#define HEALTH_REG_TEMP        0x04
#define HEALTH_REG_VOLTAGE     0x08
#define HEALTH_REG_FREQ        0x0C
#define HEALTH_REG_STATUS      0x10
#define HEALTH_REG_ALARM       0x14
#define HEALTH_REG_CONTROL     0x18

#define HEALTH_STATUS_READY    0x01
#define HEALTH_STATUS_BUSY     0x02
#define HEALTH_STATUS_ERROR    0x04
#define HEALTH_STATUS_ALARM    0x08
#define HEALTH_STATUS_FIFO_OV  0x10
#define HEALTH_STATUS_ENTROPY_OK 0x20
#define HEALTH_STATUS_TEST_DONE 0x40
#define HEALTH_STATUS_CRC_ERR  0x80

typedef enum {
    HEALTH_TEMP_NORMAL = 0,
    HEALTH_TEMP_WARN,
    HEALTH_TEMP_CRITICAL
} health_temp_state_t;

typedef enum {
    HEALTH_VOLTAGE_NORMAL = 0,
    HEALTH_VOLTAGE_WARN,
    HEALTH_VOLTAGE_CRITICAL
} health_voltage_state_t;

typedef enum {
    HEALTH_FREQ_NORMAL = 0,
    HEALTH_FREQ_WARN,
    HEALTH_FREQ_CRITICAL
} health_freq_state_t;

typedef struct {
    time_t  timestamp;
    double  temperature;
    double  voltage;
    uint32_t frequency_hz;
    double  output_rate_bytes_sec;
    double  health_score;
    uint8_t status_register;
    uint8_t alarm_register;
    uint8_t temp_state;
    uint8_t voltage_state;
    uint8_t freq_state;
    int     anomaly_detected;
    char    anomaly_reason[256];
} health_record_t;

typedef struct {
    uint32_t chip_id;
    char     chip_name[64];
    double   temperature;
    double   voltage;
    uint32_t frequency_hz;
    double   output_rate_bytes_sec;
    double   baseline_rate;
    uint8_t  status_register;
    uint8_t  alarm_register;

    health_record_t history[HEALTH_MAX_HISTORY];
    int      history_count;
    int      history_index;

    double   health_score;
    int      monitoring_active;
    int      anomaly_detected;
    int      pause_testing;

    int      samples_taken;
    double   min_temp;
    double   max_temp;
    double   min_voltage;
    double   max_voltage;
    double   min_freq;
    double   max_freq;
    double   avg_temp;
    double   avg_voltage;
    double   avg_freq;
    double   temp_sum;
    double   voltage_sum;
    double   freq_sum;

    time_t   start_time;
} health_monitor_t;

typedef struct {
    const char *filename;
    int        enabled;
    void      *db_handle;
} health_sqlite_t;

int  health_monitor_init(health_monitor_t *mon, spi_device_t *spi);
void health_monitor_cleanup(health_monitor_t *mon);

int  health_detect_chip(health_monitor_t *mon, spi_device_t *spi);
int  health_read_registers(health_monitor_t *mon, spi_device_t *spi);

int  health_evaluate_temperature(health_monitor_t *mon, double temp);
int  health_evaluate_voltage(health_monitor_t *mon, double volt);
int  health_evaluate_frequency(health_monitor_t *mon, uint32_t freq);
int  health_evaluate_output_rate(health_monitor_t *mon, double rate);

int  health_run_check(health_monitor_t *mon, spi_device_t *spi,
                       double current_rate);
double health_calculate_score(const health_monitor_t *mon);
const char *health_grade(double score);

void health_print_status(const health_monitor_t *mon);
void health_print_history(const health_monitor_t *mon, int last_n);
void health_print_report(const health_monitor_t *mon);

int  health_record_add(health_monitor_t *mon);
const health_record_t *health_record_get(const health_monitor_t *mon, int idx);

int  health_replay_from_file(const char *filename, health_monitor_t *mon);
int  health_save_replay_file(const char *filename, const health_monitor_t *mon);

int  health_sqlite_init(health_sqlite_t *sql, const char *filename);
void health_sqlite_close(health_sqlite_t *sql);
int  health_sqlite_insert(const health_sqlite_t *sql,
                          const health_record_t *rec,
                          const char *device);
int  health_sqlite_query_rma(const health_sqlite_t *sql,
                             const char *device,
                             double min_score);
int  health_sqlite_export_csv(const health_sqlite_t *sql,
                               const char *csv_path,
                               const char *device);

#endif
